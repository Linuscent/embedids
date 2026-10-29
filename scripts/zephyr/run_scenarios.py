#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the native_sim demo through its UART stdin/stdout command path.

Only Python's standard library is needed. Each scenario boots a fresh process;
timeouts use host monotonic time, while detection latency uses device time.
"""

import argparse
import json
import os
from pathlib import Path
import resource
import selectors
import subprocess
import time


class Device:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.events = []
        self.transcript = []

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.selector.close()
        self.process.stdin.close()
        self.process.stdout.close()

    def read(self, timeout):
        for key, _ in self.selector.select(max(0, timeout)):
            data = os.read(key.fd, 65536)
            if not data:
                raise AssertionError("Device exited unexpectedly:\n" + "\n".join(self.transcript[-20:]))
            self.buffer += data
            while b"\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                self.transcript.append(text)
                if text.startswith("{"):
                    event = json.loads(text)
                    if event.get("type") == "fatal":
                        raise AssertionError(f"Device fatal error: {event}")
                    self.events.append(event)

    def wait(self, predicate, timeout=5, start=0):
        deadline = time.monotonic() + timeout
        while True:
            for event in self.events[start:]:
                if predicate(event):
                    return event
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError("Timed out waiting for device event:\n" +
                                     "\n".join(self.transcript[-20:]))
            self.read(remaining)

    def quiet(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            self.read(deadline - time.monotonic())

    def command(self, text, ok=True, response_name=None):
        start = len(self.events)
        self.process.stdin.write((text + "\n").encode("ascii"))
        self.process.stdin.flush()
        command = response_name or text.split()[0]
        event = self.wait(lambda e: e.get("type") == "command" and
                          e.get("command") == command, start=start)
        if event["ok"] != ok:
            raise AssertionError(f"Unexpected command result: {text}: {event}")
        return event

    def status(self):
        start = len(self.events)
        self.process.stdin.write(b"status\n")
        self.process.stdin.flush()
        return self.wait(lambda e: e.get("type") == "status", start=start)

    def alert(self, rule, command):
        event = self.wait(lambda e: e.get("type") == "alert" and e.get("rule") == rule)
        if event["value"] <= event["limit"]:
            raise AssertionError(f"Alert has no violating evidence: {event}")
        latency = event["timestamp_ms"] - command["trigger_ms"]
        if not 0 <= latency <= 2 * self.sample_ms:
            raise AssertionError(f"{rule}: latency {latency}ms exceeds two sample periods")
        return {"rule": rule, "latency_after_trigger_ms": latency, "event": event}


SCENARIOS = ("normal", "command_state", "resource_abuse", "configuration",
             "outbound", "monitor", "queue_overflow")


def run(binary, scenario):
    device = Device(binary)
    try:
        ready = device.wait(lambda e: e.get("type") == "ready")
        device.sample_ms = ready["sample_ms"]
        device.quiet(0.3)
        if any(e.get("type") == "alert" for e in device.events):
            raise AssertionError("Alert during normal startup")
        evidence = []
        if scenario == "normal":
            if not ready["fault_injection"]:
                for command in ("fault config", "fault heartbeat", "fault overflow"):
                    device.command(command, ok=False, response_name="invalid")
            for malformed in ("infer 0", "infer -1", "infer 257", "config 101",
                              "send approved 42949672960", "mode active extra", "x" * 96):
                device.command(malformed, ok=False, response_name="invalid")
            device.command("config 12")
            device.command("mode active")
            for _ in range(12):
                device.command("infer 1")
                device.command("send approved 256")
                device.quiet(0.15)
            device.quiet(0.2)
            if any(e.get("type") == "alert" for e in device.events):
                raise AssertionError("False positive in defined normal workload")
            if device.status()["completed"] != 12:
                raise AssertionError("Normal inference workload did not complete")
        elif scenario == "command_state":
            command = device.command("infer 1", ok=False)
            evidence.append(device.alert("state_abuse", command))
        elif scenario == "resource_abuse":
            device.command("mode active")
            command = device.command("infer 256", ok=False)
            for rule in ("request_rate", "queue_rejects"):
                evidence.append(device.alert(rule, command))
        elif scenario == "configuration":
            command = device.command("fault config")
            evidence.append(device.alert("config_integrity", command))
        elif scenario == "outbound":
            command = device.command("send unknown 128", ok=False)
            evidence.append(device.alert("unknown_destination", command))
            command = device.command("send approved 8192")
            evidence.append(device.alert("tx_volume", command))
        elif scenario == "monitor":
            command = device.command("fault heartbeat")
            evidence.append(device.alert("heartbeat_age", command))
        elif scenario == "queue_overflow":
            command = device.command("fault overflow")
            evidence.append(device.alert("telemetry_drops", command))
            if not device.status()["dropped"]:
                raise AssertionError("Overflow was not counted")
        device.quiet(0.2)
        expected = {entry["rule"] for entry in evidence}
        actual = {e["rule"] for e in device.events if e.get("type") == "alert"}
        if actual != expected:
            raise AssertionError(f"Unexpected alert set: expected {expected}, got {actual}")
        return {"scenario": scenario, "passed": True, "detections": evidence,
                "status": device.status(), "events": device.events}
    finally:
        device.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Built zephyr.exe")
    parser.add_argument("--scenario", choices=("all", *SCENARIOS), default="all")
    parser.add_argument("--report", type=Path, help="Write full JSON results")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"Binary does not exist: {binary}")
    results = []
    for scenario in SCENARIOS if args.scenario == "all" else (args.scenario,):
        wall_start = time.monotonic()
        usage_start = resource.getrusage(resource.RUSAGE_CHILDREN)
        try:
            result = run(binary, scenario)
        except (AssertionError, OSError, ValueError) as error:
            result = {"scenario": scenario, "passed": False, "error": str(error)}
        usage_end = resource.getrusage(resource.RUSAGE_CHILDREN)
        wall = time.monotonic() - wall_start
        cpu = ((usage_end.ru_utime + usage_end.ru_stime) -
               (usage_start.ru_utime + usage_start.ru_stime))
        result["host_process"] = {"wall_seconds": round(wall, 4),
                                  "cpu_seconds": round(cpu, 4),
                                  "cpu_percent_one_core": round(100 * cpu / wall, 2)}
        results.append(result)
        print(json.dumps({k: v for k, v in result.items() if k != "events"}), flush=True)
    if args.report:
        args.report.write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
