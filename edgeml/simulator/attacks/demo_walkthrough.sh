#!/usr/bin/env bash
# Copyright 2026 Linuscent Ltd
# SPDX-License-Identifier: Apache-2.0
#
# Scripted version of the 5-stage demo (docs/demo-guide.md), paced for screen
# recordings. Keep the dashboard (http://localhost:8080) visible while it runs.
#
#   demo_walkthrough.sh            # default pacing
#   PAUSE=2 demo_walkthrough.sh    # shorter pauses between narration steps

set -euo pipefail

API=${EMBEDIDS_API:-http://localhost:8080/api}
PAUSE=${PAUSE:-5}

say() { printf '\n\033[1;36m>> %s\033[0m\n' "$*"; }
post() { curl -fsS -X POST "$API/$1" >/dev/null; }
state() {
  curl -fsS "$API/status" | python3 -c '
import json, sys
s = json.load(sys.stdin)
print(f"   Security State: {s[\"security_state\"]}   active alerts: {s[\"active_alerts\"]}"
      f"   model integrity: {s[\"model_integrity\"][\"status\"].upper()}")'
}
alerts() {
  curl -fsS "$API/alerts?status=active" | python3 -c '
import json, sys
for a in reversed(json.load(sys.stdin)):
    print(f"   [{a[\"severity\"].upper()}] {a[\"title\"]}")'
}

say "Stage 1 - Healthy device"
post demo/reset
sleep "$PAUSE"
state
echo "   EmbedIDS is monitoring both system behavior and AI workload health."

say "Stage 2 - Launch attack: rogue GPU workload"
post demo/scenario/cryptominer
echo "   GPU utilization climbs while inference throughput collapses..."
sleep 15

say "Stage 3 - Detection"
alerts
state
sleep "$PAUSE"

say "Stage 4 - Modify the deployed AI model"
post demo/scenario/model-tamper
sleep 2
alerts
state
sleep "$PAUSE"

say "Stage 5 - Recovery"
post demo/reset
sleep 3
state
echo
echo "The same security engine runs on NVIDIA Jetson with native collectors."
