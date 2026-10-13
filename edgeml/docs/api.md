# REST API

`embedids-agent` serves the API on `api.bind:api.port`. The default port is
8081; in `configs/default.yaml` it is 8080. Every route is also available
under the `/api` prefix, which is the path the dashboard and its nginx proxy
use. Responses are JSON unless noted. CORS is open (`*`) so external tools
can poll the API.

JSON Schemas are in [`../api/schemas/`](../api/schemas):

| Schema | Describes |
|--------|-----------|
| `metric.schema.json` | metric samples (spec section 8) |
| `alert.schema.json` | alerts (spec section 12) |
| `event.schema.json` | timeline events |

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/health` | Liveness and whether telemetry is flowing |
| GET | `/status` | Device, security state, integrity, agent self-metrics, collectors, thresholds, simulator state |
| GET | `/metrics` | Latest value of every metric. `?history=N` adds the last N seconds per metric as `[timestamp_ms, value]` pairs. `?format=prometheus` returns Prometheus text format instead |
| GET | `/alerts` | Newest first. `?status=active` (or `?active=true`) and `?limit=N` (default 100) |
| GET | `/alerts/{id}` | One alert with full evidence; 404 if unknown |
| GET | `/events` | Incident timeline, newest first. `?limit=N` (default 100, max 500) |
| GET | `/rules` | Built-in rule catalog and configured correlations |
| POST | `/demo/scenario/{name}` | Inject a simulated attack (simulation platform only) |
| POST | `/demo/reset` | Stop attacks, restore the model, resolve alerts and clear detection history |

Scenario names for `/demo/scenario/{name}`:

- `normal`: stops all attacks and lets values ramp back.
- `cryptominer`
- `model-tamper`
- `network-exfil`
- `inference-degradation`
- `rogue-process`
- `privilege-escalation`
- `dos`
- `telemetry-loss`

Scenarios stack: activating `model-tamper` while `cryptominer` runs keeps
both active.

Demo endpoints return:

- **403** when `api.demo_controls` is false;
- **409** on hardware platforms, or when the simulator is unreachable;
- **404** for unknown scenarios.

## Examples

```bash
curl -s localhost:8081/status | jq '{security_state, active_alerts, model_integrity}'
```

```json
{
  "security_state": "CRITICAL",
  "active_alerts": 1,
  "model_integrity": {
    "status": "violated",
    "path": "/models/model.engine",
    "expected": "57869ad7a571163daf939d5e3ebe1dcfcb81437654d1636b0cdfdc233d92f56c",
    "current": "e406c9968759cdf53726f05c7b51e8408a3c858e93a30df2da62966fde22eae2",
    "baseline": "config"
  }
}
```

```bash
curl -s -X POST localhost:8081/demo/scenario/cryptominer
curl -s 'localhost:8081/alerts?status=active' | jq '.[] | {alert_id, severity, title}'
curl -s localhost:8081/alerts/ALT-000004 | jq .evidence
curl -s 'localhost:8081/metrics?format=prometheus' | grep gpu_utilization
```

## Alert export formats

Each change to an alert's lifecycle is exported with an extra
`"event": "raised" | "escalated" | "resolved"` field:

| Sink | Config | Format |
|------|--------|--------|
| Console | `alerts.console` | `TIME [SEVERITY] ALT-ID Title - description` |
| JSON lines | `alerts.json_file`, `alerts.json_file_path` | one alert JSON object per line |
| Local syslog | `alerts.syslog` | `syslog(3)`, facility `daemon`, priority mapped from severity |
| Remote syslog | `alerts.syslog_server: "host:514"` | RFC 5424 over UDP, MSGID = rule |
| Webhook | `alerts.webhook_url: "http://host/path"` | HTTP POST of the alert JSON, from a background thread |
| SQLite | `storage.enabled` | `alerts` table (upserted), plus `events`, `metrics_summary`, `agent_state` |

The webhook client uses plain HTTP; terminate TLS at a local proxy if
needed.
