# Architecture

EmbedIDS EdgeML keeps telemetry collection, normalization, detection,
correlation, alerting and visualization in separate layers. Only the collector
layer knows whether it is running in simulation, on generic Linux, or on an
NVIDIA Jetson.

```text
+---------------------------------------------------+
|  Dashboard (nginx + static JS)       :8080        |
|  Security state | Telemetry | Alerts | Timeline   |
+--------------------------+------------------------+
                           | /api  (reverse proxy)
+--------------------------v------------------------+
|  REST API (cpp-httplib, inside embedids-agent)    |   :8081
|  Alert sinks: console | JSON lines | syslog |     |
|               webhook | SQLite event store        |
+--------------------------+------------------------+
                           |
+--------------------------v------------------------+
|  Correlator        rule-based, windowed           |   core/correlation
|  DetectionEngine   11 deterministic rules         |   core/detection
|  SignalEngine      EmbedIDS C core: thresholds +  |   core/embedids
|                    sustained-condition algorithms |
|  Policies          process / destination / model  |
|                    reader allow lists             |
+--------------------------+------------------------+
                           |  TelemetryBatch (core/schema)
+--------------------------v------------------------+
|  TelemetryManager  normalization, heartbeats      |   agent/
+--------------------------+------------------------+
                           |  MetricCollector (HAL)
      +--------------------+-------------------+
      |                    |                   |
+-----v------+     +-------v------+    +-------v--------+
| Simulation |     | Linux /proc  |    | Jetson         |
| (HTTP or   |     | /sys         |    | tegrastats,    |
| in-process)|     |              |    | GPU load sysfs |
+-----+------+     +--------------+    +----------------+
      |
+-----v-----------------------------+
| edgeml-simulator  :9100           |
| GPU / inference / network /       |
| process / system models, attacks, |
| owns /models/model.engine         |
+-----------------------------------+
```

Platform-neutral collectors run everywhere: model integrity (SHA-256 of
the deployed engine), configuration integrity, and agent self-monitoring.

## Code layout

| Path | Contents | Depends on |
|------|----------|------------|
| `core/` | Schema, config, SHA-256 (C), EmbedIDS bridge, rules, correlation | EmbedIDS C core, nlohmann/json |
| `simulator/` | Device models, attack scenarios, simulator HTTP API, `edgeml-simulator` | `core/` |
| `collectors/` | `MetricCollector` HAL, simulation / Linux / Jetson backends, factory | `core/`, `simulator/` (simulation backend only) |
| `agent/` | Pipeline, telemetry manager, alert manager and sinks, SQLite store, `embedids-agent` | all of the above, yaml-cpp, SQLite |
| `api/` | REST server and JSON schemas | `agent/` |
| `dashboard/` | Static frontend and nginx config | REST API |

CMake builds these as separate static libraries (`edgeml_core`,
`edgeml_sim`, `edgeml_collectors`, `edgeml_agent`). `edgeml_core` cannot
link against collectors, so the detection engine cannot depend on Jetson
APIs.

## Data flow per cycle

The default `sampling.interval_ms` is 1000.

1. **Collect.** `TelemetryManager` calls `collect()` on every initialized
   collector. Every new `MetricSample` is stamped with the device id and the
   cycle timestamp. Process, connection and file-access inventories are
   attached to the same `TelemetryBatch`.
2. **Heartbeat.** Each collector that reports successfully records a
   heartbeat at most every `heartbeat.interval_seconds`. The manager adds
   two metrics:
   - `security.agent_heartbeat`: age in seconds of the oldest required
     heartbeat, with a `stale` label listing the late collectors.
   - `security.telemetry_health`: percentage of collectors that reported.
3. **Policy enrichment.** Allow lists turn the inventories into numbers:
   - `security.unknown_processes`
   - `security.privileged_processes`
   - `network.new.destinations`
   - `security.model_file_access`
4. **EmbedIDS.** `SignalEngine` registers 12 metrics and 15 signals with the
   EmbedIDS C library. All history buffers are allocated once at startup.
   - Instantaneous `>`/`<` conditions use the built-in threshold algorithm.
   - Sustained conditions ("FOR > 10 seconds") use a custom EmbedIDS
     algorithm. It checks that the condition held for every sample in the
     newest streak of the history ring buffer, and that the streak spans the
     required duration.
   - `embedids_analyze_metric_detailed()` reports every algorithm on a
     metric, so one history can feed several independent signals. This
     function was added to the core library for this work.
5. **Rules.** `DetectionEngine` combines signals into findings. Each
   finding carries severity, category, a human-readable description, an
   evidence object and the metrics involved.
6. **Alerts.** `AlertManager` turns findings into an alert lifecycle:
   - a new finding is raised as an alert;
   - a finding whose severity rises updates the existing alert as
     escalated;
   - a finding that disappears is resolved.

   Findings are keyed by rule and subject (process or destination), so
   repeated cycles never produce duplicate alerts.
7. **Correlation.** `Correlator` evaluates the active alerts and those
   resolved recently. A requirement matches an alert's rule name or one of
   its signal metrics, so the spec's metric-based correlation config works
   unchanged. Correlated incidents are tracked as alerts in their own scope.
8. **Export.** Changes go to the console, JSON lines, syslog (local or
   RFC 5424 over UDP), webhook (asynchronous) and SQLite. Raw telemetry is
   kept in memory for `sampling.history_seconds`. SQLite stores only
   per-minute summaries, with rolling retention.

## Detection rules

| Rule | Condition | Severity |
|------|-----------|----------|
| `unexpected_gpu_workload` | `gpu.utilization > 85` AND `ai.inference.rate < 30 x 0.9`, for 10 s | HIGH |
| `rogue_process` | process not on the allow list, running for at least 5 s | MEDIUM; HIGH when UID 0 |
| `unknown_destination` | outbound destination not on the allow list | MEDIUM; HIGH when `bytes.out > escalation_bytes_out` |
| `outbound_traffic_anomaly` | `network.bytes.out > baseline x 5`, for 3 s | MEDIUM |
| `data_exfiltration` | unknown destination AND `bytes.out > baseline x 5` | HIGH |
| `model_file_access` | model read by a process outside `allowed_readers` | MEDIUM |
| `model_integrity_violation` | SHA-256 of model differs from the approved hash, or the file is missing | CRITICAL |
| `config_integrity_violation` | agent config changed after startup | HIGH |
| `inference_degradation` | `ai.inference.rate < 20`, for 10 s | MEDIUM |
| `resource_exhaustion` | CPU > 95 % AND memory > 90 %, for 5 s | HIGH |
| `telemetry_heartbeat_lost` | required collector heartbeat older than 15 s | CRITICAL |

Default correlations (`Config::default_correlations()`, `configs/demo.yaml`):

| Correlation | Requires | Severity |
|-------------|----------|----------|
| `unauthorized_gpu_workload` | unknown process + GPU anomaly + inference degradation | HIGH |
| `model_data_exfiltration` | unknown destination + outbound anomaly + model file access | CRITICAL |
| `privileged_resource_hijack` | privileged unknown process + resource exhaustion | CRITICAL |

## Threading

`embedids-agent` runs two threads that matter:

- the detection loop, which runs `Agent::run_cycle()` on a steady-clock
  schedule;
- the HTTP server pool.

Two mutexes protect shared state:

- `pipeline_mutex_` covers collectors, the detection engine and the
  correlator. It is used by the cycle and by demo reset.
- `state_mutex_` covers alerts, events, metric history and collector status.
  It is held briefly when the API reads them.

Slow I/O stays off the detection path:

- simulator HTTP calls happen outside `state_mutex_`;
- webhook delivery runs on its own worker thread.

## Self-protection

| Control | Implementation |
|---------|----------------|
| Configuration integrity | SHA-256 of the YAML at load; the file is re-checked every cycle (stat) and fully re-hashed at least every 30 s |
| Binary integrity | `agent.binary_sha256` (hash of `/proc/self/exe`) in `/status` and the startup event |
| Heartbeat | per-collector, every 5 s, timeout 15 s |
| Crash restart | Docker `restart: unless-stopped`; systemd `Restart=always` |
| Least privilege | runs as user `embedids`; read-only root filesystem, `cap_drop: ALL` and `no-new-privileges` in Docker; hardened systemd unit |
