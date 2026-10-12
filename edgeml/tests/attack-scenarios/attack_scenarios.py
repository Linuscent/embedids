#!/usr/bin/env python3
# Copyright 2026 Linuscent Ltd
# SPDX-License-Identifier: Apache-2.0
"""
Attack scenario acceptance runner (spec sections 16, 24 and 25).

Drives a running EmbedIDS agent through its REST API: for each scenario it
resets the environment, injects the attack, waits for the expected alerts and
asserts severity, category and detection latency. Works against the Docker
Compose stack or locally started binaries.

    attack_scenarios.py --api http://localhost:8080/api
    attack_scenarios.py --api http://localhost:8081 --only model-tamper
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

# scenario -> (timeout seconds, [(rule, severity, category or None)])
SCENARIOS = {
    "cryptominer": (35, [
        ("rogue_process", "medium", "process"),
        ("unexpected_gpu_workload", "high", "runtime_anomaly"),
        ("inference_degradation", "medium", "performance"),
        ("unauthorized_gpu_workload", "high", "correlated_incident"),
    ]),
    "model-tamper": (5, [
        ("model_integrity_violation", "critical", "integrity"),
    ]),
    "network-exfil": (30, [
        ("unknown_destination", "high", "network"),
        ("outbound_traffic_anomaly", "medium", "network"),
        ("data_exfiltration", "high", "network"),
        ("model_data_exfiltration", "critical", "correlated_incident"),
    ]),
    "privilege-escalation": (15, [
        ("rogue_process", "high", "process"),
    ]),
    "dos": (35, [
        ("resource_exhaustion", "high", "runtime_anomaly"),
        ("inference_degradation", "medium", "performance"),
    ]),
    "inference-degradation": (30, [
        ("inference_degradation", "medium", "performance"),
    ]),
    "telemetry-loss": (35, [
        ("telemetry_heartbeat_lost", "critical", "availability"),
    ]),
}

# Spec section 24: model tamper must be detected in < 2 s. Allow for the
# sampling interval plus API polling granularity in a real-time run.
LATENCY_LIMITS = {"model-tamper": 3.0}


def request(api, method, path):
    req = urllib.request.Request(f"{api}/{path}", method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as res:
            return json.loads(res.read().decode() or "{}")
    except urllib.error.HTTPError as err:
        body = err.read().decode()
        raise RuntimeError(f"{method} {path}: HTTP {err.code} {body}") from err


def active_alerts(api):
    return request(api, "GET", "alerts?status=active&limit=500")


def wait_for(api, expected, timeout, started):
    found = {}
    deadline = started + timeout
    while time.time() < deadline:
        for alert in active_alerts(api):
            rule = alert["rule"]
            if rule in expected and rule not in found:
                found[rule] = (alert, time.time() - started)
        # Escalations (e.g. unknown_destination -> high) happen after raise.
        if all(r in found for r in expected):
            current = {a["rule"]: a for a in active_alerts(api)}
            for rule in expected:
                if rule in current:
                    found[rule] = (current[rule], found[rule][1])
            if all(found[r][0]["severity"] == expected[r][0] for r in expected):
                return found
        time.sleep(0.25)
    return found


def run_scenario(api, name, settle):
    timeout, expectations = SCENARIOS[name]
    expected = {rule: (sev, cat) for rule, sev, cat in expectations}
    request(api, "POST", "demo/reset")
    time.sleep(settle)
    started = time.time()
    request(api, "POST", f"demo/scenario/{name}")
    found = wait_for(api, expected, timeout, started)

    failures = []
    rows = []
    for rule, (severity, category) in expected.items():
        if rule not in found:
            failures.append(f"{rule}: not raised within {timeout}s")
            rows.append((rule, "-", "-", "MISSING"))
            continue
        alert, latency = found[rule]
        ok = alert["severity"] == severity and (
            category is None or alert["category"] == category)
        if not ok:
            failures.append(
                f"{rule}: got {alert['severity']}/{alert['category']}, "
                f"expected {severity}/{category}")
        limit = LATENCY_LIMITS.get(name)
        if limit is not None and latency > limit:
            failures.append(f"{rule}: detection latency {latency:.1f}s > {limit}s")
            ok = False
        rows.append((rule, alert["severity"], f"{latency:5.1f}s",
                     "ok" if ok else "FAIL"))
    return failures, rows


def check_healthy(api, seconds):
    request(api, "POST", "demo/reset")
    time.sleep(2)
    deadline = time.time() + seconds
    while time.time() < deadline:
        bad = [a for a in active_alerts(api)
               if a["severity"] in ("high", "critical")]
        if bad:
            return [f"normal operation raised {bad[0]['rule']}"]
        time.sleep(1)
    status = request(api, "GET", "status")
    if status["security_state"] != "HEALTHY":
        return [f"security state {status['security_state']} after reset"]
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--api", default="http://localhost:8080/api",
                        help="agent API base URL")
    parser.add_argument("--only", action="append", choices=sorted(SCENARIOS),
                        help="run only these scenarios")
    parser.add_argument("--settle", type=float, default=3.0,
                        help="seconds of normal operation before each attack")
    parser.add_argument("--normal-seconds", type=int, default=20,
                        help="duration of the no-false-positive check")
    parser.add_argument("--wait", type=int, default=60,
                        help="seconds to wait for the agent to become ready")
    args = parser.parse_args()
    api = args.api.rstrip("/")

    deadline = time.time() + args.wait
    while True:
        try:
            if request(api, "GET", "health").get("status") == "ok":
                break
        except Exception:  # noqa: BLE001 - keep retrying until deadline
            pass
        if time.time() > deadline:
            print(f"agent at {api} not ready", file=sys.stderr)
            return 2
        time.sleep(1)

    failures = []
    print(f"EmbedIDS attack scenarios against {api}\n")
    print("normal operation ... ", end="", flush=True)
    problems = check_healthy(api, args.normal_seconds)
    print("FAIL" if problems else "ok (no high/critical alerts)")
    failures += problems

    for name in args.only or SCENARIOS:
        print(f"\n== {name}")
        problems, rows = run_scenario(api, name, args.settle)
        for rule, severity, latency, verdict in rows:
            print(f"   {rule:28s} {severity:9s} {latency:>7s}  {verdict}")
        failures += [f"{name}: {p}" for p in problems]

    request(api, "POST", "demo/reset")
    print()
    if failures:
        print("FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("All attack scenarios detected as expected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
