# Hardware Porting Guide (NVIDIA Jetson)

The detection engine (`edgeml/core`) only consumes the normalized
`TelemetryBatch`. Moving from simulation to hardware means choosing a
different collector set (`device.platform`); the detection logic does not
change.

```cpp
class MetricCollector {
public:
  virtual bool initialize() = 0;              // probe the backend
  virtual bool collect(MetricBatch &metrics) = 0;
  virtual const char *name() const = 0;
  virtual bool heartbeat_required() const { return true; }
  virtual ~MetricCollector() = default;
};
```

## Collector inventory

| Domain | Simulation | Linux (phase 1) | Jetson (phases 2-3) |
|--------|------------|-----------------|---------------------|
| CPU / load | `SimulatedSystemCollector` | `ProcCpuCollector` (`/proc/stat`, `/proc/loadavg`) | same as Linux |
| Memory | `SimulatedSystemCollector` | `ProcMemoryCollector` (`/proc/meminfo`) | same as Linux |
| Processes | `SimulatedProcessCollector` | `ProcProcessCollector` (`/proc/<pid>/stat`, `status`) | same as Linux |
| Network | `SimulatedNetworkCollector` | `LinuxNetworkCollector` (`/proc/net/tcp{,6}`, `/proc/net/dev`) | same as Linux |
| Temperature | `SimulatedSystemCollector` | `LinuxThermalCollector` (`/sys/class/thermal`) | `JetsonThermalCollector` (tegrastats `tj@`) |
| GPU | `SimulatedGpuCollector` | n/a | `JetsonGpuCollector` (GPU `load` sysfs node, fallback tegrastats `GR3D_FREQ`; unified RAM as GPU memory) |
| Power | `SimulatedGpuCollector` | n/a | `JetsonPowerCollector` (tegrastats `VDD_GPU_SOC` / `VDD_CPU_GPU_CV` / `POM_5V_GPU`) |
| Inference | `SimulatedInferenceCollector` | `InferenceStatsCollector` | `JetsonInferenceCollector` (same JSON contract) |
| Model / config integrity | `FileIntegrityCollector` (all platforms) | same | same |
| Agent self-metrics | `AgentSelfCollector` (all platforms) | same | same |

Status of the non-simulated collectors:

- The `/proc` parsers and the tegrastats parser have unit tests built from
  real output captured on Ubuntu 22.04, JetPack 4 (Nano) and JetPack 6
  (Orin).
- The Jetson collectors are built against documented L4T interfaces but
  **have not yet been validated on hardware**. Expect to adjust rail names
  and sysfs paths per module (`jetson.gpu_load_paths`).

## Phase 1 - Generic Linux telemetry

```bash
./build/edgeml/embedids-agent --config edgeml/configs/linux-example.yaml
```

- Processes are read from `/proc/<pid>/stat`:
  - the comm name;
  - the start time, from `btime` + `starttime`;
  - the UID, from `status`.

  Kernel threads (children of `kthreadd`) are skipped by default.
- Outbound connections are ESTABLISHED TCP sockets whose local port is not
  listening. Byte rates come from `/proc/net/dev` deltas; use
  `network.interface` to restrict them to one NIC.
- Real hosts run many processes. Start with `allowed: ["*"]`, record a
  baseline from `GET /status`, then tighten the list. Allow-list entries
  support a trailing `*` wildcard, e.g. `systemd*`.

## Phase 2 - Jetson platform metrics

Set `device.platform: jetson` (see `configs/jetson-example.yaml`).

- `TegrastatsSource` runs `tegrastats --interval <sampling.interval_ms>` once
  and shares the latest parsed line with the GPU, power and thermal
  collectors. Samples older than 5 s count as a collector failure, which
  feeds the heartbeat rule.
- GPU utilization is read directly from the load node when it exists (per
  mille, converted to percent). Typical paths:
  - `/sys/devices/platform/gpu.0/load` (Orin);
  - `/sys/devices/gpu.0/load` (Nano / Xavier).

  Otherwise it falls back to tegrastats `GR3D_FREQ`.
- Jetson has no discrete GPU memory. `gpu.memory.*` reports unified RAM
  usage with the label `memory=unified`.
- NVML is not available on Jetson; on dGPU platforms an NVML collector can
  be added as another `MetricCollector`.

Validation checklist on first hardware bring-up:

1. Run `tegrastats --interval 1000` once and compare a line with
   `edgeml/tests/unit/test_tegrastats.cpp`. Add it as a new fixture if the
   format differs.
2. Run `cat /sys/devices/platform/gpu.0/load` under load. The value should
   track `GR3D_FREQ`.
3. Run `embedids-agent --config ... --once`. Every collector should be
   listed as `"healthy": true`.
4. Measure the normal operating range for 30 minutes (`GET /metrics?history=1800`).
   Tune `gpu.high_utilization`, `ai.expected_fps` and
   `network.baseline_bytes_out` for the deployed model.

## Phase 3 - AI workload integration

The inference application (camera, then CUDA pre-processing, then TensorRT)
publishes its statistics at least every 5 s by atomically rewriting a small
JSON file:

```json
{"fps": 29.8, "latency_ms": 31.2, "errors": 0, "healthy": true}
```

```python
# Python / DeepStream probe example
import json, os
def publish(stats, path="/run/edgeml/inference.json"):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(stats, f)
    os.replace(tmp, path)
```

The file's location is set by `ai.stats_file`. If the file becomes stale,
the heartbeat rule fires.

The model is identified by hashing the TensorRT engine
(`integrity.model.path`). Pin the hash your release pipeline produces
(`sha256sum model.engine`) in `integrity.model.sha256`. If it is left
empty, the hash present at first start is trusted.

## Phase 4 - System service

```bash
sudo install -m 0755 build/edgeml/embedids-agent /usr/local/bin/
sudo install -D -m 0640 edgeml/configs/jetson-example.yaml /etc/embedids/config.yaml
sudo useradd --system --home /var/lib/embedids --shell /usr/sbin/nologin embedids
sudo cp edgeml/deployment/systemd/embedids.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now embedids
systemctl status embedids
journalctl -u embedids -f
```

The unit runs as `embedids` with no capabilities, `ProtectSystem=strict`,
`MemoryDenyWriteExecute`, `Restart=always`, and resource limits of
`CPUQuota=10%` and `MemoryMax=96M`.

On Jetson, `tegrastats` may need the `video` group. Reading other users'
`/proc/<pid>/status` needs no privilege. Mapping sockets to owning
processes would need `CAP_SYS_PTRACE` through a narrowly scoped helper, so
it is deliberately not done in the MVP.

### Cross-compiling for aarch64

```bash
cmake -S . -B build-arm64 -DBUILD_EDGEML=ON \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
cmake --build build-arm64 --target embedids-agent
```

Alternatively, run `docker buildx build --platform linux/arm64` with
`edgeml/deployment/docker/Dockerfile`, or build natively on the device.

## Performance targets

| Target | Simulation (measured) | Jetson (goal) |
|--------|-----------------------|---------------|
| CPU overhead | ~0.5 % average | < 3 % |
| Memory | ~8-10 MB RSS | < 50 MB |

On hardware, `ProcProcessCollector` scans `/proc` every cycle. On devices
with thousands of processes, raise `sampling.interval_ms` or move process
scanning to a slower cadence.
