---
title: Zephyr Integration
description: Build the optional EmbedIDS Zephyr module and run a repeatable simulated Edge AI security demo.
---

EmbedIDS can be built as an external Zephyr module without porting the larger
EdgeML agent. The integration keeps the core C library OS-independent and adds
a bounded, allocation-free sample queue. It does not automatically start threads.

## Source files

| Path | Contents |
|---|---|
| [`zephyr/`](https://github.com/Linuscent/EmbedIDS/tree/main/zephyr) | Module metadata ([`module.yml`](https://github.com/Linuscent/EmbedIDS/blob/main/zephyr/module.yml)), build and [`Kconfig`](https://github.com/Linuscent/EmbedIDS/blob/main/zephyr/Kconfig) options |
| [`ports/zephyr/`](https://github.com/Linuscent/EmbedIDS/tree/main/ports/zephyr) | Queue adapter: [`embedids_zephyr.c`](https://github.com/Linuscent/EmbedIDS/blob/main/ports/zephyr/embedids_zephyr.c) and its [header](https://github.com/Linuscent/EmbedIDS/blob/main/ports/zephyr/include/embedids/zephyr.h) |
| [`examples/zephyr/edge_ai_security/`](https://github.com/Linuscent/EmbedIDS/tree/main/examples/zephyr/edge_ai_security) | Simulated Edge AI demo: [`src/main.c`](https://github.com/Linuscent/EmbedIDS/blob/main/examples/zephyr/edge_ai_security/src/main.c), [`prj.conf`](https://github.com/Linuscent/EmbedIDS/blob/main/examples/zephyr/edge_ai_security/prj.conf) |
| [`scripts/zephyr/run_scenarios.py`](https://github.com/Linuscent/EmbedIDS/blob/main/scripts/zephyr/run_scenarios.py) | Scenario runner |
| [`tests/zephyr/adapter/`](https://github.com/Linuscent/EmbedIDS/tree/main/tests/zephyr/adapter) | Ztest suite for the adapter |
| [`.github/workflows/zephyr.yml`](https://github.com/Linuscent/EmbedIDS/blob/main/.github/workflows/zephyr.yml) | CI on `native_sim` |

## Supported baseline

The initial target is **Zephyr v4.2.0**, board `native_sim/native/64`, on Linux.
Use a normal Zephyr development environment with host GCC/G++, Make, Ninja, Python 3.12
and CMake 3.x (3.31 is suitable). A cross-compilation SDK is not needed for this
native target. Other Zephyr releases and physical boards require validation.

The standalone CMake build and the EdgeML build remain separate and unchanged
by enabling this module.

## Build and run the demo

From the EmbedIDS repository, with your Zephyr Python environment activated and
`ZEPHYR_BASE` pointing to the Zephyr v4.2.0 checkout:

```bash
west build -b native_sim/native/64 examples/zephyr/edge_ai_security -d build/zephyr-demo
./build/zephyr-demo/zephyr/zephyr.exe
```

The sample adds this repository to `EXTRA_ZEPHYR_MODULES` automatically. If not
using a west workspace, use CMake directly:

```bash
cmake -G Ninja -S examples/zephyr/edge_ai_security -B build/zephyr-demo \
  -DBOARD=native_sim/native/64 -DZEPHYR_MODULES="$PWD" \
  -DZEPHYR_TOOLCHAIN_VARIANT=host
cmake --build build/zephyr-demo
```

UART input/output uses the process's standard input/output. Boot messages are
followed by a JSON `ready` event. Commands are newline-delimited ASCII. Each
command is bounded to 95 characters; malformed and oversized commands are rejected.

```text
config 12
mode active
infer 1
send approved 256
status
```

`config` changes synthetic work units (1–100), only while idle. `infer` accepts
1–256 requests; its queue has eight slots. Requests already accepted are drained
even if the device subsequently enters idle mode. `send` models a dispatcher,
not network sockets. Approved sends accept 1–1,048,576 bytes; an `unknown`
destination is rejected and recorded as a policy violation.

## Automated scenarios

```bash
python3 scripts/zephyr/run_scenarios.py \
  --binary build/zephyr-demo/zephyr/zephyr.exe \
  --report build/zephyr-demo/scenarios.json
```

The [runner](https://github.com/Linuscent/EmbedIDS/blob/main/scripts/zephyr/run_scenarios.py) uses only Python's standard library. It boots a fresh device for
each scenario, exercises the real command parser and queues, verifies expected
alerts and evidence, and exits nonzero on failure. Use `--scenario normal` or
another scenario name to run just one.

| Scenario | Input | Expected rule |
|---|---|---|
| `normal` | Configure while idle, then submit paced inference and approved sends | No alerts; 12 completed requests |
| `command_state` | `infer 1` while idle | `state_abuse` |
| `resource_abuse` | `mode active`, then `infer 256` | `request_rate`, `queue_rejects` |
| `configuration` | `fault config` | `config_integrity` |
| `outbound` | `send unknown 128`, then `send approved 8192` | `unknown_destination`, `tx_volume` |
| `monitor` | `fault heartbeat` | `heartbeat_age` |
| `queue_overflow` | `fault overflow` | `telemetry_drops` |

The five threat categories have seven scenarios including normal operation and
the additional queue-overflow check. The sample enables
`CONFIG_EMBEDIDS_DEMO_FAULT_INJECTION` explicitly. Its default is off. Fault
commands are **test-only mechanisms**, not claims of exploitable production
interfaces or defenses against arbitrary code execution.

## Detection policy

A single detector thread samples every 100 ms. The independent telemetry
producer updates its heartbeat every 50 ms. Rules are numeric thresholds:

- State violations, queue rejections, unknown destinations and telemetry drops:
  alert when the count increases.
- Inference request count: more than 32 in a sample interval.
- Approved outbound volume: more than 4,096 bytes in a sample interval.
- Configuration: synthetic work units differ from the approved value.
- Heartbeat: older than 500 ms.

Counter thresholds are **per observed interval**, not normalized rates. Under
heavy scheduling delays, intervals can lengthen; a production adapter should
measure interval length and detector deadlines explicitly. Cumulative counters
use modulo-32-bit deltas, assuming fewer than one full wrap between samples.

The detector emits `alert` on entry and `clear` on recovery rather than repeating
an alert on every sample. Evidence includes the value, limit and sample timestamp.
`status` reports completed inference, queue occupancy, lost samples, history and
adapter sizes, and maximum observed simulated detection-cycle time
(`max_detection_sim_us`). This uses Zephyr's virtual cycle clock and may read
zero even while the host executes work. It includes alert output and excludes
collection; it is not a hardware WCET or CPU-overhead measurement. The runner
also reports measured CPU time and wall time for the **entire host process**,
not isolated detector overhead. Hardware/baseline benchmarking is deferred.

The acceptance target is detection within two sample periods after the trigger
condition. The runner checks sample timestamps relative to the command's
`trigger_ms`; for heartbeat loss that is the last producer heartbeat plus the
timeout plus one millisecond (the rule uses strict greater-than). This is a
reproducible demo check, not a hardware scheduler-latency benchmark. A short normal run without
alerts does not establish a field false-positive rate.

## Reuse in another application

Add the repository as a west project at a reviewed, pinned commit, or append its
absolute path to `EXTRA_ZEPHYR_MODULES` **before** `find_package(Zephyr ...)`.
The module is external; inclusion in Zephyr's default manifest is not required.

```ini
CONFIG_EMBEDIDS=y
CONFIG_EMBEDIDS_ZEPHYR_ADAPTER=y
CONFIG_EMBEDIDS_MAX_METRICS=8
CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE=32
CONFIG_EMBEDIDS_FLOATING_POINT=n
```

Allocate a static `struct embedids_zephyr`, metric configurations and histories.
Initialize them with `embedids_zephyr_init()` before starting producers.
Producers call `embedids_zephyr_submit()` with a metric index and value; the
function is nonblocking and can run in thread or ordinary ISR context. A full
queue returns `-ENOMSG` and increments `embedids_zephyr_dropped()`.

One consumer thread calls `embedids_zephyr_poll()`. It processes at most one
queue capacity per call and evaluates every configured algorithm after **each**
sample, so a subsequent normal value cannot hide a queued anomalous value.
The callback receives normal results too, allowing recovery tracking. It must
not block in production. Anomaly results are delivered to the callback;
the poll return value reports ingestion errors.

Configuration must outlive the adapter and remain immutable while it is running.
Only the consumer may access the core context and histories. The core itself is
not thread-safe. Initialize/reconfigure only when all producers and the consumer
are stopped. The application owns thread creation, priorities, alert transport,
overflow response and lifecycle. Do not log or transmit from ISR producers.

The adapter adds structural validation and rejects the currently unfinished
built-in trend algorithm and Boolean thresholds. Custom callbacks can implement
state-aware or Boolean policies. Floating-point metric types can be disabled,
but the core's optional `embedids_get_trend()` still contains float arithmetic.
Do not treat that option as a guarantee that every core helper is integer-only.

Kconfig limits propagate to both the library and callers, preserving structure
layout. The generated public header uses EmbedIDS's version, not the enclosing
application version. Keep edits in [`include/embedids.h.in`](https://github.com/Linuscent/EmbedIDS/blob/main/include/embedids.h.in).

## Tests and footprint

```bash
python "$ZEPHYR_BASE/scripts/twister" -T tests/zephyr/adapter \
  -p native_sim/native/64 -O build/zephyr-tests --inline-logs \
  --extra-args="ZEPHYR_MODULES=$PWD"
```

The [tests](https://github.com/Linuscent/EmbedIDS/blob/main/tests/zephyr/adapter/src/main.c) cover per-sample evaluation, all-algorithm reporting, ISR submission,
concurrent producers, bounded overflow, circular histories, invalid inputs/configuration, custom
callbacks and integer/float build configurations.

The explicit module argument also works with a standalone Zephyr checkout,
without a west workspace. Zephyr 4.2's Twister predates Python 3.14's changed
multiprocessing defaults; use Python 3.12 for the documented test workflow.

History RAM is `metric_count × history_depth × sizeof(datapoint)`. The sample's
eight metrics and four samples use 640 bytes with the current packed 20-byte
datapoint. Adapter storage, metric configuration, application state, inference
queue, kernel objects and the four thread stacks are additional. Inspect the
Zephyr map and `ram_report`/`rom_report` targets for full-image totals. Native
executable sizes include host-specific components and are not MCU flash budgets.

## Security boundaries and deferred work

- Inference is deterministic synthetic work plus a 30 ms service delay, not ML.
- Configuration integrity is an in-memory comparison for a small demo config,
  not a cryptographic root of trust or authenticated update mechanism.
- Outbound checks model application policy; they do not inspect real packets.
- Heartbeat loss detects a stopped producer while the detector is alive. A
  stopped detector/device needs an independent watchdog or external observer.
- All threads share a trusted firmware image. No userspace isolation, secure-world
  monitor, automatic containment, or resistance to privileged compromise is claimed.
- Alerts indicate observable policy violations, not proof of an intrusion.

Future stages can add a physical board, real inference, network transport,
statistical baselines, protected monitor state and an independent heartbeat observer.

The [Zephyr RTOS intrusion detection case study](../case-studies/zephyr-runtime-security.mdx)
walks through these scenarios with recorded results and an animated replay.
