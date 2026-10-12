# Demo Guide

The demo takes 5-10 minutes. A non-engineer can run it from a laptop with
Docker; no NVIDIA hardware is needed.

## 1. Start the environment

```bash
cd edgeml/deployment
docker compose up --build -d      # one command; first build takes a few minutes
```

Open **http://localhost:8080**. Within a few seconds the dashboard should
show:

```text
Security State: HEALTHY      Model Integrity: VERIFIED
Inference: ~30 FPS           GPU: 55-70 %         heartbeat 0-5 s
```

| Service | Purpose | Port |
|---------|---------|------|
| `edgeml-simulator` | Simulated Jetson-class device running a vision inference workload | 9100 (internal) |
| `embedids-agent` | EmbedIDS agent and REST API | 8081 |
| `embedids-dashboard` | Dashboard, proxies `/api` to the agent | 8080 |

### Without Docker

You can also run everything in a single process:

```bash
cmake -S . -B build -DBUILD_EDGEML=ON && cmake --build build -j
./build/edgeml/embedids-agent --config edgeml/configs/default.yaml
# open http://localhost:8080
```

## 2. Demo script

### Stage 1 - Healthy device

Point at the overview cards and the live charts. The dashed lines on the
charts are the detection thresholds.

> EmbedIDS is monitoring both system behavior and AI workload health:
> processes, network activity, AI performance, model integrity and compute
> behavior.

### Stage 2 - Launch attack

Press **Simulate Cryptominer**. Over about 3 seconds:

- GPU utilization rises from ~65 % to ~98 %;
- inference falls from 30 FPS to ~11 FPS;
- a new process, `unknown_worker`, appears.

### Stage 3 - Detection

Alerts appear in **Security Events**, and a banner announces each new one:

| After | Alert |
|-------|-------|
| ~5 s | `[MEDIUM]` Unknown process detected (`unknown_worker`) |
| ~12 s | `[HIGH]` GPU workload outside expected profile |
| ~12 s | `[MEDIUM]` Inference performance degradation |
| ~12 s | `[HIGH]` Possible unauthorized GPU workload (correlated) |

Click the correlated alert. The **Evidence** panel lists the contributing
alerts, the GPU utilization, the inference rate and the offending process.
The **Incident Timeline** shows the order of events.

### Stage 4 - Model tamper

Press **Modify Model**. Within about a second:

```text
[CRITICAL] AI MODEL INTEGRITY VIOLATION
EXPECTED: 57869ad7a571163daf939d5e3ebe1dcfcb81437654d1636b0cdfdc233d92f56c
CURRENT:  e406c9968759cdf53726f05c7b51e8408a3c858e93a30df2da62966fde22eae2
```

The simulator really modifies `/models/model.engine` on a shared volume.
The agent detects it by re-hashing the file, not from a simulated flag.

### Stage 5 - Recovery

Press **Reset Environment**. The attacks stop, the approved model is
restored, and alerts are resolved. The state returns to **HEALTHY**. Resolved
alerts stay in the table, greyed out, as an audit trail.

> The same security engine is designed to run on NVIDIA Jetson devices using
> native Jetson telemetry collectors.

## 3. Other scenarios

| Button | Scenario | Expected alerts |
|--------|----------|-----------------|
| Simulate Data Exfiltration | `network-exfil` | Unknown destination (MEDIUM, then HIGH), Outbound traffic anomaly, Possible data exfiltration, Unexpected model file access, **Possible model/data exfiltration** (CRITICAL, correlated) |
| Launch Rogue Process | `rogue-process` | Unknown process detected (MEDIUM) |
| Privilege Escalation | `privilege-escalation` | Unauthorized privileged process (HIGH) |
| Degrade Inference | `inference-degradation` | Inference performance degradation (MEDIUM) |
| Denial of Service | `dos` | Resource exhaustion (HIGH), Inference degradation |
| Stop Telemetry | `telemetry-loss` | Telemetry heartbeat lost (CRITICAL) after 12-16 s |
| Start Normal Workload | `normal` | Stops attacks; alerts resolve once values return to normal |

Scenarios can be combined. For example, **Privilege Escalation** followed by
**Denial of Service** produces the correlated incident *Privileged process
exhausting device resources*.

There are two more ways to show agent tampering:

- `docker compose stop simulator` raises the heartbeat alert, exactly as a
  killed collector would.
- Editing `edgeml/configs/demo.yaml` on the host while the stack is running
  raises **Configuration integrity violation**. The agent hashed the
  configuration at startup.

## 4. Command-line control

These are useful for scripted demos and recordings:

```bash
edgeml/simulator/attacks/attack.sh list
edgeml/simulator/attacks/attack.sh cryptominer          # inject and watch alerts
edgeml/simulator/attacks/attack.sh reset
edgeml/simulator/attacks/demo_walkthrough.sh            # paced 5-stage demo
python3 edgeml/tests/attack-scenarios/attack_scenarios.py   # acceptance run
```

Set `EMBEDIDS_API=http://<host>:8080/api` to target a remote stack.

## 5. Recording instructions

1. Run `docker compose up -d`, wait for HEALTHY, and press **Reset
   Environment** so the alert list is empty.
2. Set a browser window of 1600x1000 at 100 % zoom. Hide bookmarks and
   extensions.
3. Start the screen recorder, for example OBS (1080p, 30 fps) or
   `ffmpeg -f x11grab`.
4. Narrate along with `demo_walkthrough.sh` running in a second terminal
   with `PAUSE=6`, or press the buttons by hand following section 2. The
   whole flow fits in about 3 minutes.
5. Screenshots for marketing are in `docs/screenshots/`:
   - healthy state;
   - correlated GPU incident with evidence;
   - model integrity violation;
   - data exfiltration incident.

   To regenerate them, take browser screenshots after each stage.

## 6. Measured agent resource usage

These figures come from the Docker Compose stack at a 1 s sampling interval.
Live values are on the **Agent Overhead** card and in `/status` under
`.agent`.

| Measure | Target (spec section 23) | Measured |
|---------|--------------------------|----------|
| Agent CPU | < 5 % | ~0.5 % of one core on average (≤ 3 % peaks while serving the live dashboard) |
| Agent memory (RSS) | < 100 MB | ~8-10 MB (6 threads) |
| Processing latency per cycle | < 100 ms | ≤ 3 ms (detection + correlation + alerting) |
| Alert generation after detection | < 1 s | same cycle |
| Model tamper detection latency | < 2 s | 1 sampling interval (1 s) |

These are engineering measurements from the simulation environment, not
marketing claims. Repeat them on target hardware before publishing.

## 7. Shut down

```bash
docker compose down            # keep alert history (agent-data volume)
docker compose down -v         # also delete the volumes
```
