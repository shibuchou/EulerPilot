#!/usr/bin/env python3
import csv
import json
import sys
from pathlib import Path


WIDTH = 1100
HEIGHT = 420


def load_redis_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def load_nginx_row(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            return row
    return {}


def simple_bar_svg(labels, values_a, values_b, name_a, name_b, title, ylabel, output: Path):
    margin_left = 80
    margin_right = 30
    margin_top = 50
    margin_bottom = 80
    chart_w = WIDTH - margin_left - margin_right
    chart_h = HEIGHT - margin_top - margin_bottom
    max_value = max(max(values_a), max(values_b), 1.0)
    group_w = chart_w / max(len(labels), 1)
    bar_w = group_w / 3
    colors = ["#1f77b4", "#ff7f0e"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}">',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:12px;fill:#222}.title{font-size:18px;font-weight:bold}</style>',
        f'<text class="title" x="{WIDTH/2}" y="28" text-anchor="middle">{title}</text>',
        f'<line x1="{margin_left}" y1="{HEIGHT-margin_bottom}" x2="{WIDTH-margin_right}" y2="{HEIGHT-margin_bottom}" stroke="#444"/>',
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{HEIGHT-margin_bottom}" stroke="#444"/>',
        f'<text x="20" y="{margin_top+20}" transform="rotate(-90,20,{margin_top+20})">{ylabel}</text>',
    ]

    for i, label in enumerate(labels):
        gx = margin_left + i * group_w
        parts.append(f'<text x="{gx + group_w/2}" y="{HEIGHT-margin_bottom+24}" text-anchor="middle">{label}</text>')
        for j, value in enumerate((values_a[i], values_b[i])):
            bh = (value / max_value) * (chart_h - 10)
            x = gx + bar_w * (j + 0.75)
            y = HEIGHT - margin_bottom - bh
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w*0.75:.1f}" height="{bh:.1f}" fill="{colors[j]}"/>')
            parts.append(f'<text x="{x + bar_w*0.37:.1f}" y="{y-4:.1f}" text-anchor="middle">{value:.2f}</text>')

    legend_x = WIDTH - margin_right - 180
    legend_y = 44
    for idx, name in enumerate((name_a, name_b)):
        parts.append(f'<rect x="{legend_x}" y="{legend_y + idx*18}" width="12" height="12" fill="{colors[idx]}"/>')
        parts.append(f'<text x="{legend_x+18}" y="{legend_y+10 + idx*18}">{name}</text>')

    parts.append("</svg>")
    output.write_text("\n".join(parts), encoding="utf-8")


def timeline_svg(events, title, output: Path):
    margin_left = 80
    margin_right = 30
    margin_top = 60
    margin_bottom = 70
    chart_w = WIDTH - margin_left - margin_right
    chart_h = HEIGHT - margin_top - margin_bottom
    colors = {
        "NORMAL": "#1f77b4",
        "ARMED": "#ff7f0e",
        "ACTIVE": "#d62728",
        "COOLDOWN": "#2ca02c",
    }

    if not events:
        output.write_text("<svg xmlns='http://www.w3.org/2000/svg' width='400' height='80'></svg>", encoding="utf-8")
        return

    labels = [event["next_state"] for event in events]
    x_step = chart_w / max(len(events), 1)
    y_map = {"NORMAL": 0, "ARMED": 1, "ACTIVE": 2, "COOLDOWN": 3}

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}">',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:12px;fill:#222}.title{font-size:18px;font-weight:bold}</style>',
        f'<text class="title" x="{WIDTH/2}" y="28" text-anchor="middle">{title}</text>',
    ]

    for name, idx in y_map.items():
        y = HEIGHT - margin_bottom - idx * (chart_h / 3)
        parts.append(f'<line x1="{margin_left}" y1="{y:.1f}" x2="{WIDTH-margin_right}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(f'<text x="{margin_left-10}" y="{y+4:.1f}" text-anchor="end">{name}</text>')

    prev_x = None
    prev_y = None
    for i, event in enumerate(events):
        state = event["next_state"]
        x = margin_left + i * x_step + x_step / 2
        y = HEIGHT - margin_bottom - y_map[state] * (chart_h / 3)
        color = colors.get(state, "#666")
        if prev_x is not None:
            parts.append(f'<line x1="{prev_x:.1f}" y1="{prev_y:.1f}" x2="{x:.1f}" y2="{y:.1f}" stroke="#555" stroke-width="2"/>')
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="6" fill="{color}"/>')
        parts.append(f'<text x="{x:.1f}" y="{HEIGHT-margin_bottom+20}" text-anchor="middle">g{event["generation"]}</text>')
        prev_x, prev_y = x, y

    parts.append("</svg>")
    output.write_text("\n".join(parts), encoding="utf-8")


def load_trace(path: Path):
    events = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if not line.strip():
            continue
        events.append(json.loads(line))
    return events


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: render_extra_figures.py <redis_csv> <nginx_csv> <psi_trace_jsonl> <out_dir>", file=sys.stderr)
        return 1

    redis_csv = Path(sys.argv[1])
    nginx_csv = Path(sys.argv[2])
    trace_path = Path(sys.argv[3])
    out_dir = Path(sys.argv[4])
    out_dir.mkdir(parents=True, exist_ok=True)

    redis_rows = load_redis_rows(redis_csv)
    tests = [row["test"] for row in redis_rows]
    quiet_default = [float(row["quiet_default_rps_avg"]) for row in redis_rows]
    quiet_scx_normal = [float(row["quiet_scx_normal_rps_avg"]) for row in redis_rows]
    simple_bar_svg(tests, quiet_default, quiet_scx_normal, "quiet_default", "quiet_scx_normal", "Redis quiet 开销消融", "Requests/sec", out_dir / "redis_quiet_overhead.svg")

    nginx_row = load_nginx_row(nginx_csv)
    simple_bar_svg(
        ["Nginx"],
        [float(nginx_row["quiet_default_rps_avg"])],
        [float(nginx_row["quiet_scx_normal_rps_avg"])],
        "quiet_default",
        "quiet_scx_normal",
        "Nginx quiet 开销消融",
        "Requests/sec",
        out_dir / "nginx_quiet_overhead.svg",
    )

    trace_events = load_trace(trace_path)
    timeline_svg(trace_events, "PsiGate 状态时间线", out_dir / "psigate_timeline.svg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
