#!/usr/bin/env bash
# Copyright 2026 Linuscent Ltd
# SPDX-License-Identifier: Apache-2.0
#
# Trigger a simulated attack through the EmbedIDS agent API and watch the
# resulting alerts.
#
#   attack.sh list
#   attack.sh cryptominer            # inject, then watch alerts for 30 s
#   attack.sh model-tamper --no-watch
#   attack.sh normal | reset
#
# EMBEDIDS_API defaults to the dashboard proxy (http://localhost:8080/api).

set -euo pipefail

API=${EMBEDIDS_API:-http://localhost:8080/api}
SCENARIO=${1:-}
WATCH_SECONDS=${WATCH_SECONDS:-30}

scenarios=(cryptominer model-tamper network-exfil inference-degradation
           rogue-process privilege-escalation dos telemetry-loss)

usage() {
  echo "usage: $0 <scenario|normal|reset|list> [--no-watch]"
  echo "scenarios: ${scenarios[*]}"
}

if [[ -z "$SCENARIO" || "$SCENARIO" == "-h" || "$SCENARIO" == "--help" ]]; then
  usage
  exit 1
fi

if [[ "$SCENARIO" == "list" ]]; then
  printf '%s\n' "${scenarios[@]}"
  exit 0
fi

if [[ "$SCENARIO" == "reset" ]]; then
  curl -fsS -X POST "$API/demo/reset" >/dev/null
  echo "environment reset"
  exit 0
fi

curl -fsS -X POST "$API/demo/scenario/$SCENARIO" >/dev/null
echo "scenario '$SCENARIO' injected at $(date +%T)"

if [[ "${2:-}" == "--no-watch" || "$SCENARIO" == "normal" ]]; then
  exit 0
fi

echo "watching alerts for ${WATCH_SECONDS}s (Ctrl-C to stop)..."
seen=""
end=$((SECONDS + WATCH_SECONDS))
while (( SECONDS < end )); do
  alerts=$(curl -fsS "$API/alerts?status=active&limit=50" || echo "[]")
  while IFS=$'\t' read -r id severity title; do
    [[ -z "$id" || " $seen " == *" $id "* ]] && continue
    seen="$seen $id"
    printf '%s  [%s] %s %s\n' "$(date +%T)" "${severity^^}" "$id" "$title"
  done < <(python3 -c '
import json, sys
for a in reversed(json.load(sys.stdin)):
    print(a["alert_id"], a["severity"], a["title"], sep="\t")
' <<<"$alerts")
  sleep 1
done
