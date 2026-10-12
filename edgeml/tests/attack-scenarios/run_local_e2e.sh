#!/usr/bin/env bash
# Copyright 2026 Linuscent Ltd
# SPDX-License-Identifier: Apache-2.0
#
# Real-process end-to-end test: starts edgeml-simulator and embedids-agent as
# separate processes talking over HTTP (the Docker topology without Docker),
# then runs every attack scenario against the agent API.
#
#   run_local_e2e.sh <edgeml-simulator> <embedids-agent> [attack_scenarios args]

set -euo pipefail

SIMULATOR=${1:?usage: run_local_e2e.sh <edgeml-simulator> <embedids-agent>}
AGENT=${2:?usage: run_local_e2e.sh <edgeml-simulator> <embedids-agent>}
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d /tmp/edgeml-e2e-XXXXXX)

free_port() {
  python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'
}
SIM_PORT=$(free_port)
API_PORT=$(free_port)

cleanup() {
  [[ -n "${AGENT_PID:-}" ]] && kill "$AGENT_PID" 2>/dev/null || true
  [[ -n "${SIM_PID:-}" ]] && kill "$SIM_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  if [[ "${KEEP_WORKDIR:-0}" != "1" ]]; then rm -rf "$WORK"; fi
}
trap cleanup EXIT

MODEL="$WORK/models/model.engine"
cat > "$WORK/config.yaml" <<EOF
device:
  id: e2e-edge-001
  platform: simulation
simulation:
  mode: http
  endpoint: "http://127.0.0.1:${SIM_PORT}"
integrity:
  model:
    path: "${MODEL}"
    sha256: "$("$SIMULATOR" --print-model-hash)"
alerts:
  console: true
  json_file: true
  json_file_path: "${WORK}/alerts.jsonl"
storage:
  path: "${WORK}/embedids.db"
api:
  bind: "127.0.0.1"
  port: ${API_PORT}
EOF

"$SIMULATOR" --bind 127.0.0.1 --port "$SIM_PORT" --model-path "$MODEL" \
  > "$WORK/simulator.log" 2>&1 &
SIM_PID=$!
for _ in $(seq 1 50); do
  curl -fsS "http://127.0.0.1:${SIM_PORT}/health" >/dev/null 2>&1 && break
  sleep 0.2
done

"$AGENT" --config "$WORK/config.yaml" > "$WORK/agent.log" 2>&1 &
AGENT_PID=$!

status=0
python3 "$HERE/attack_scenarios.py" --api "http://127.0.0.1:${API_PORT}" "$@" \
  || status=$?

if [[ $status -ne 0 ]]; then
  echo "---- agent log (tail) ----"
  tail -n 40 "$WORK/agent.log" || true
fi

# Alerts must also have been exported as JSON lines and persisted in SQLite.
if [[ $status -eq 0 ]]; then
  lines=$(wc -l < "$WORK/alerts.jsonl")
  echo "alerts.jsonl: ${lines} alert events exported"
  [[ "$lines" -gt 0 ]] || { echo "no JSON alert export"; status=1; }
  [[ -s "$WORK/embedids.db" ]] || { echo "no SQLite event store"; status=1; }
fi
exit $status
