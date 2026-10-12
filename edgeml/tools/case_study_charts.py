#!/usr/bin/env python3
# Copyright 2026 Linuscent Ltd
# SPDX-License-Identifier: Apache-2.0
"""
Record real EdgeML telemetry from a running agent and render the SVG charts
used by the EdgeML case study on the EmbedIDS website.

    # 1. record datasets from a running stack (docker compose up)
    case_study_charts.py record --api http://localhost:8081 \
        --scenario cryptominer --then model-tamper@25 --out gpu.json
    case_study_charts.py record --api http://localhost:8081 \
        --scenario network-exfil --duration 30 --out exfil.json

    # 2. render
    case_study_charts.py render-gpu gpu.json gpu.svg
    case_study_charts.py render-exfil exfil.json exfil.svg
    case_study_charts.py render-latency latency.json latency.svg

Only the Python standard library is used.
"""

import argparse
import json
import sys
import time
import urllib.request
from html import escape

# ----------------------------------------------------------------- palette
BG = "#0b1020"
PANEL = "#121a2e"
BORDER = "#24314f"
GRID = "#24314f"
TEXT = "#e6ebf5"
MUTED = "#8b97b3"
ATTACK = "#ff6b4a"
SEVERITY = {
    "critical": "#e0398f",
    "high": "#ff6b4a",
    "medium": "#f5b83d",
    "low": "#4ea1ff",
    "info": "#4ea1ff",
}
FONT = "Inter, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif"
MONO = "'JetBrains Mono', Menlo, Consolas, monospace"


# Metrics kept in recorded datasets (the charts use a subset).
KEEP_METRICS = (
    "gpu.utilization", "gpu.memory.percent", "ai.inference.rate",
    "ai.inference.latency", "system.cpu.utilization", "network.bytes.out",
    "network.connections.outbound", "security.model_integrity",
)


# ------------------------------------------------------------------ record
def api_call(api, method, path):
    req = urllib.request.Request(f"{api.rstrip('/')}/{path}", method=method)
    with urllib.request.urlopen(req, timeout=10) as res:
        return json.loads(res.read().decode() or "{}")


def record(args):
    api = args.api
    follow_ups = []
    for item in args.then or []:
        name, _, offset = item.partition("@")
        follow_ups.append((float(offset), name))
    follow_ups.sort()

    api_call(api, "POST", "demo/reset")
    print(f"baseline {args.baseline}s ...", flush=True)
    time.sleep(args.baseline)

    events = []
    start_ms = int(time.time() * 1000)
    api_call(api, "POST", f"demo/scenario/{args.scenario}")
    events.append({"t_ms": start_ms, "scenario": args.scenario})
    print(f"scenario {args.scenario} started", flush=True)

    t0 = time.time()
    for offset, name in follow_ups:
        time.sleep(max(0.0, offset - (time.time() - t0)))
        events.append({"t_ms": int(time.time() * 1000), "scenario": name})
        api_call(api, "POST", f"demo/scenario/{name}")
        print(f"scenario {name} started at +{offset}s", flush=True)
    time.sleep(max(0.0, args.duration - (time.time() - t0)))

    window = int(args.baseline + args.duration + 5)
    metrics = api_call(api, "GET", f"metrics?history={window}")
    alerts = api_call(api, "GET", "alerts?limit=200")
    status = api_call(api, "GET", "status")
    api_call(api, "POST", "demo/reset")

    since = start_ms - int(args.baseline * 1000)
    data = {
        "recorded": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "device_id": status.get("device_id"),
        "attack_start_ms": start_ms,
        "baseline_seconds": args.baseline,
        "duration_seconds": args.duration,
        "events": events,
        "thresholds": status.get("thresholds", {}),
        "series": {
            name: [p for p in points if p[0] >= since]
            for name, points in metrics["history"].items()
            if name in KEEP_METRICS
        },
        "alerts": sorted(
            [
                {k: a[k] for k in ("timestamp_ms", "severity", "title",
                                   "rule", "category", "alert_id")}
                for a in alerts if a["timestamp_ms"] >= start_ms
            ],
            # Same-cycle alerts keep raise order (ids are sequential).
            key=lambda a: (a["timestamp_ms"], a["alert_id"]),
        ),
    }
    with open(args.out, "w") as f:
        json.dump(data, f, indent=1)
    print(f"wrote {args.out}: {len(data['alerts'])} alerts")


# ------------------------------------------------------------------ render
class Svg:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.parts = []

    def add(self, fragment):
        self.parts.append(fragment)

    def text(self, x, y, content, size=13, fill=TEXT, weight=400,
             anchor="start", family=FONT, opacity=1.0):
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" '
                 f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
                 f'text-anchor="{anchor}" opacity="{opacity}">'
                 f'{escape(str(content))}</text>')

    def render(self, title):
        body = "\n".join(self.parts)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" '
                f'viewBox="0 0 {self.width} {self.height}" '
                f'width="{self.width}" height="{self.height}" role="img" '
                f'aria-label="{escape(title)}">\n<title>{escape(title)}</title>\n'
                f'<rect width="100%" height="100%" rx="18" fill="{BG}"/>\n'
                f'{body}\n</svg>\n')


def x_scale(data, left, right):
    start = data["attack_start_ms"]
    lo = -data["baseline_seconds"]
    hi = data["duration_seconds"]

    def fx(t_ms):
        rel = (t_ms - start) / 1000.0
        return left + (rel - lo) / (hi - lo) * (right - left)

    return fx, lo, hi


def draw_panel(svg, data, fx, top, height, left, right, cfg):
    """Line chart of one metric with thresholds and an attack band."""
    svg.add(f'<rect x="{left - 70}" y="{top - 34}" width="{right - left + 100}" '
            f'height="{height + 58}" rx="12" fill="{PANEL}" stroke="{BORDER}"/>')
    svg.text(left - 50, top - 12, cfg["title"], 15, TEXT, 600)
    if cfg.get("subtitle"):
        svg.text(right + 20, top - 12, cfg["subtitle"], 12, MUTED,
                 anchor="end")

    points = data["series"].get(cfg["metric"], [])
    scale = cfg.get("scale", 1.0)
    lo, hi = cfg["range"]

    def fy(v):
        v = min(max(v * scale, lo), hi)
        return top + height - (v - lo) / (hi - lo) * height

    # grid + y labels
    for tick in cfg["ticks"]:
        y = fy(tick / scale)
        svg.add(f'<line x1="{left}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}" '
                f'stroke="{GRID}" stroke-width="1"/>')
        svg.text(left - 10, y + 4, cfg["tick_format"](tick), 11, MUTED,
                 anchor="end", family=MONO)

    # attack band
    start_x = fx(data["attack_start_ms"])
    svg.add(f'<rect x="{start_x:.1f}" y="{top}" width="{right - start_x:.1f}" '
            f'height="{height}" fill="{ATTACK}" opacity="0.07"/>')

    # thresholds
    for value, label, color in cfg.get("thresholds", []):
        y = fy(value / scale)
        svg.add(f'<line x1="{left}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}" '
                f'stroke="{color}" stroke-width="1.5" stroke-dasharray="6 5"/>')
        svg.text(left + 8, y - 6, label, 11, color, 600)

    if len(points) > 1:
        coords = " ".join(f"{fx(t):.1f},{fy(v):.1f}" for t, v in points)
        first_x, last_x = fx(points[0][0]), fx(points[-1][0])
        area = (f"{first_x:.1f},{top + height} {coords} "
                f"{last_x:.1f},{top + height}")
        svg.add(f'<polygon points="{area}" fill="{cfg["color"]}" '
                f'opacity="0.14"/>')
        svg.add(f'<polyline points="{coords}" fill="none" '
                f'stroke="{cfg["color"]}" stroke-width="2.4" '
                f'stroke-linejoin="round" stroke-linecap="round"/>')

    # before/after annotations
    for label, at_seconds in cfg.get("annotate", []):
        target = data["attack_start_ms"] + at_seconds * 1000
        nearest = min(points, key=lambda p: abs(p[0] - target), default=None)
        if nearest is None:
            continue
        x, y = fx(nearest[0]), fy(nearest[1])
        svg.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{BG}" '
                f'stroke="{cfg["color"]}" stroke-width="2.4"/>')
        svg.text(x, y - 12, label.format(v=nearest[1] * scale), 12, TEXT,
                 600, anchor="middle")


def draw_events(svg, data, fx, top, bottom):
    for event in data["events"]:
        x = fx(event["t_ms"])
        svg.add(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{bottom}" '
                f'stroke="{ATTACK}" stroke-width="1.5" stroke-dasharray="3 4" '
                f'opacity="0.9"/>')
        svg.add(f'<rect x="{x - 2:.1f}" y="{top - 26}" '
                f'width="{len(event["scenario"]) * 7.4 + 34:.1f}" height="22" '
                f'rx="11" fill="{ATTACK}" opacity="0.18"/>')
        svg.text(x + 12, top - 11, f'▶ {event["scenario"]}', 12, ATTACK, 700,
                 family=MONO)


def draw_alert_lane(svg, data, fx, top, left, right):
    alerts = data["alerts"]
    lane_h = 30 * max(len(alerts), 1) + 56
    svg.add(f'<rect x="{left - 70}" y="{top - 34}" width="{right - left + 100}" '
            f'height="{lane_h}" rx="12" fill="{PANEL}" stroke="{BORDER}"/>')
    svg.text(left - 50, top - 12, "EmbedIDS alerts", 15, TEXT, 600)
    start = data["attack_start_ms"]
    for i, alert in enumerate(alerts):
        y = top + 14 + i * 30
        x = fx(alert["timestamp_ms"])
        color = SEVERITY.get(alert["severity"], MUTED)
        svg.add(f'<line x1="{x:.1f}" y1="{top - 2}" x2="{x:.1f}" y2="{y:.1f}" '
                f'stroke="{color}" stroke-width="1" opacity="0.5"/>')
    for i, alert in enumerate(alerts):
        y = top + 14 + i * 30
        x = fx(alert["timestamp_ms"])
        color = SEVERITY.get(alert["severity"], MUTED)
        delay = (alert["timestamp_ms"] - start) / 1000.0
        severity = alert["severity"].upper()
        tag = "  · correlated" if alert["category"] == "correlated_incident" else ""
        meta = f"+{delay:.0f}s{tag}"
        # Approximate text width so a backing plate can hide crossing stems.
        width = 7.9 * len(severity) + 7.0 * len(alert["title"]) + \
            6.8 * len(meta) + 28
        anchor_right = x > (left + right) / 2
        tx = x - 14 if anchor_right else x + 14
        plate_x = tx - width - 4 if anchor_right else tx - 4
        svg.add(f'<rect x="{plate_x:.1f}" y="{y - 11:.1f}" '
                f'width="{width + 8:.1f}" height="22" rx="6" fill="{PANEL}"/>')
        svg.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="6" fill="{color}"/>')
        anchor = "end" if anchor_right else "start"
        svg.add(f'<text x="{tx:.1f}" y="{y + 4:.1f}" font-family="{FONT}" '
                f'font-size="13" text-anchor="{anchor}" fill="{TEXT}">'
                f'<tspan fill="{color}" font-weight="700">'
                f'{escape(severity)}</tspan>'
                f'<tspan dx="8">{escape(alert["title"])}</tspan>'
                f'<tspan fill="{MUTED}" dx="8" font-family="{MONO}" '
                f'font-size="11">{escape(meta)}</tspan></text>')
    return top - 34 + lane_h


def draw_x_axis(svg, fx, lo, hi, y, step=10):
    t = int(lo // step * step)
    while t <= hi:
        if t >= lo:
            x = fx_rel(fx, t)
            svg.text(x, y, f"{t:+d}s" if t else "0s", 11, MUTED,
                     anchor="middle", family=MONO)
        t += step


def fx_rel(fx, seconds):
    # fx works on absolute ms; reconstruct from the closure's attack start
    return fx(fx.attack_start + seconds * 1000)


def chart_frame(title, subtitle, height):
    svg = Svg(1200, height)
    svg.text(40, 46, title, 24, TEXT, 700)
    svg.text(40, 72, subtitle, 14, MUTED)
    return svg


def render_timeline(data, out, title, subtitle, panels):
    left, right = 120, 1130
    fx, lo, hi = x_scale(data, left, right)
    fx.attack_start = data["attack_start_ms"]

    panel_h = 150
    top = 150
    tops = []
    for _ in panels:
        tops.append(top)
        top += panel_h + 78
    lane_top = top
    height = lane_top + 30 * max(len(data["alerts"]), 1) + 80
    svg = chart_frame(title, subtitle, height)

    for panel_top, cfg in zip(tops, panels):
        draw_panel(svg, data, fx, panel_top, panel_h, left, right, cfg)
        draw_x_axis(svg, fx, lo, hi, panel_top + panel_h + 18)
    draw_events(svg, data, fx, tops[0] - 4, lane_top - 40)
    end = draw_alert_lane(svg, data, fx, lane_top, left, right)
    svg.text(1160, end + 26,
             f"Recorded from the EmbedIDS EdgeML agent · {data['recorded']} · "
             "1 s sampling", 11, MUTED, anchor="end")
    with open(out, "w") as f:
        f.write(svg.render(title))
    print(f"wrote {out}")


def render_gpu(args):
    data = json.load(open(args.data))
    t = data["thresholds"]
    panels = [
        {"title": "GPU utilization", "metric": "gpu.utilization",
         "range": (0, 100), "ticks": [0, 25, 50, 75, 100],
         "tick_format": lambda v: f"{v:.0f}%", "color": "#76b900",
         "thresholds": [(t.get("gpu_high_utilization", 85),
                         "detection threshold 85%", ATTACK)],
         "annotate": [("{v:.0f}%", -10), ("{v:.0f}%", 18)]},
        {"title": "Inference throughput", "metric": "ai.inference.rate",
         "subtitle": "frames per second", "range": (0, 40),
         "ticks": [0, 10, 20, 30, 40], "tick_format": lambda v: f"{v:.0f}",
         "color": "#3ccf7a",
         "thresholds": [(t.get("minimum_fps", 20), "minimum 20 FPS", ATTACK)],
         "annotate": [("{v:.0f} FPS", -10), ("{v:.0f} FPS", 18)]},
    ]
    render_timeline(
        data, args.out, "Rogue GPU workload, detected and correlated",
        "A cryptominer starts on a vision-inference device, then the "
        "deployed model is modified. Real telemetry, real alerts.", panels)


def render_exfil(args):
    data = json.load(open(args.data))
    t = data["thresholds"]
    panels = [
        {"title": "Outbound traffic", "metric": "network.bytes.out",
         "subtitle": "MB/s", "scale": 1e-6, "range": (0, 3.0),
         "ticks": [0, 1, 2, 3], "tick_format": lambda v: f"{v:.0f} MB/s",
         "color": "#ff8a65",
         "thresholds": [(t.get("bytes_out_anomaly", 200000) / 1e6,
                         "anomaly threshold (5× baseline)", ATTACK)],
         "annotate": [("{v:.2f} MB/s", -10), ("{v:.1f} MB/s", 15)]},
        {"title": "Outbound connections", "metric":
         "network.connections.outbound", "subtitle": "count", "range": (0, 4),
         "ticks": [0, 1, 2, 3, 4], "tick_format": lambda v: f"{v:.0f}",
         "color": "#22d3ee",
         "annotate": [("{v:.0f} allow-listed", -10), ("+1 unknown", 15)]},
    ]
    render_timeline(
        data, args.out, "Model exfiltration over a hijacked process",
        "An allow-listed MQTT client reads the model and streams it to an "
        "unknown host. No signature needed.", panels)


def render_latency(args):
    rows = json.load(open(args.data))["scenarios"]
    width, row_h = 1200, 46
    height = 170 + row_h * len(rows) + 60
    svg = chart_frame("Time to detect, per attack scenario",
                      "Seconds from attack start to the first alert of each "
                      "kind. Includes intentional sustain windows.", height)
    left, right = 420, 1100
    max_s = 20
    top = 130
    for s in range(0, max_s + 1, 5):
        x = left + s / max_s * (right - left)
        svg.add(f'<line x1="{x:.1f}" y1="{top - 10}" x2="{x:.1f}" '
                f'y2="{top + row_h * len(rows)}" stroke="{GRID}"/>')
        svg.text(x, top + row_h * len(rows) + 22, f"{s}s", 11, MUTED,
                 anchor="middle", family=MONO)
    for i, row in enumerate(rows):
        y = top + i * row_h
        color = SEVERITY[row["severity"]]
        svg.text(left - 20, y + 20, row["alert"], 14, TEXT, 600, anchor="end")
        svg.text(left - 20, y + 36, row["scenario"], 11, MUTED, anchor="end",
                 family=MONO)
        w = max(row["seconds"] / max_s * (right - left), 4)
        svg.add(f'<rect x="{left}" y="{y + 8}" width="{w:.1f}" height="24" '
                f'rx="6" fill="{color}" opacity="0.85"/>')
        note = f'{row["seconds"]:.0f} s'
        if row.get("note"):
            note += f'  ·  {row["note"]}'
        svg.text(left + w + 10, y + 25, note, 12, TEXT, family=MONO)
    # severity legend
    lx = 760
    for name in ("critical", "high", "medium"):
        svg.add(f'<rect x="{lx}" y="92" width="14" height="14" rx="4" '
                f'fill="{SEVERITY[name]}" opacity="0.85"/>')
        svg.text(lx + 22, 104, name, 12, MUTED)
        lx += 110
    svg.text(1160, height - 24, "Measured on the Docker Compose demo stack "
             "via the REST API (attack_scenarios.py)", 11, MUTED, anchor="end")
    with open(args.out, "w") as f:
        f.write(svg.render("Time to detect per scenario"))
    print(f"wrote {args.out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawTextHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    rec = sub.add_parser("record", help="record a scenario from a live agent")
    rec.add_argument("--api", default="http://localhost:8081")
    rec.add_argument("--scenario", required=True)
    rec.add_argument("--then", action="append",
                     help="follow-up scenario as name@seconds")
    rec.add_argument("--baseline", type=float, default=40)
    rec.add_argument("--duration", type=float, default=45)
    rec.add_argument("--out", required=True)
    rec.set_defaults(func=record)

    for name, func in (("render-gpu", render_gpu),
                       ("render-exfil", render_exfil),
                       ("render-latency", render_latency)):
        p = sub.add_parser(name)
        p.add_argument("data")
        p.add_argument("out")
        p.set_defaults(func=func)

    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
