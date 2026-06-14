#!/usr/bin/env python3
import csv
import json
import sys
from pathlib import Path


WIDTH = 1100
HEIGHT = 460
MARGIN_LEFT = 90
MARGIN_RIGHT = 30
MARGIN_TOP = 50
MARGIN_BOTTOM = 90


def load_csv_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def load_single_row(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            return row
    return {}


def bar_chart(labels, series, title, ylabel, output: Path):
    chart_w = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
    chart_h = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
    max_value = max(v for _, values in series for v in values) if series else 1.0
    max_value = max(max_value, 1.0)
    group_count = len(labels)
    series_count = len(series)
    group_w = chart_w / max(group_count, 1)
    bar_w = group_w / max(series_count + 0.6, 1.6)
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#17becf"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}">',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:12px;fill:#222}.title{font-size:18px;font-weight:bold}</style>',
        f'<text class="title" x="{WIDTH/2}" y="28" text-anchor="middle">{title}</text>',
        f'<line x1="{MARGIN_LEFT}" y1="{HEIGHT-MARGIN_BOTTOM}" x2="{WIDTH-MARGIN_RIGHT}" y2="{HEIGHT-MARGIN_BOTTOM}" stroke="#444"/>',
        f'<line x1="{MARGIN_LEFT}" y1="{MARGIN_TOP}" x2="{MARGIN_LEFT}" y2="{HEIGHT-MARGIN_BOTTOM}" stroke="#444"/>',
        f'<text x="20" y="{MARGIN_TOP+20}" transform="rotate(-90,20,{MARGIN_TOP+20})">{ylabel}</text>',
    ]

    for i, label in enumerate(labels):
        gx = MARGIN_LEFT + i * group_w
        parts.append(f'<text x="{gx + group_w/2}" y="{HEIGHT-MARGIN_BOTTOM+26}" text-anchor="middle">{label}</text>')
        for sidx, (name, values) in enumerate(series):
            value = values[i]
            bh = (value / max_value) * (chart_h - 12)
            x = gx + bar_w * (sidx + 0.35)
            y = HEIGHT - MARGIN_BOTTOM - bh
            color = colors[sidx % len(colors)]
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w*0.72:.1f}" height="{bh:.1f}" fill="{color}"/>')
            parts.append(f'<text x="{x + bar_w*0.36:.1f}" y="{y-4:.1f}" text-anchor="middle">{value:.2f}</text>')

    legend_x = WIDTH - MARGIN_RIGHT - 220
    legend_y = 42
    for idx, (name, _) in enumerate(series):
        color = colors[idx % len(colors)]
        parts.append(f'<rect x="{legend_x}" y="{legend_y + idx*18}" width="12" height="12" fill="{color}"/>')
        parts.append(f'<text x="{legend_x+18}" y="{legend_y+10 + idx*18}">{name}</text>')

    parts.append("</svg>")
    output.write_text("\n".join(parts), encoding="utf-8")


def render_redis(redis_csv: Path, out_dir: Path):
    rows = load_csv_rows(redis_csv)
    tests = [row["test"] for row in rows]
    labels = [
        "quiet_default",
        "quiet_scx_normal",
        "noisy_default",
        "noisy_cgroup_v2",
        "noisy_scx_normal",
        "noisy_scx_always_active",
        "noisy_scx_psi",
    ]
    series_rps = []
    series_p99 = []
    for label in labels:
        series_rps.append((label, [float(row[f"{label}_rps_avg"]) for row in rows]))
        series_p99.append((label, [float(row[f"{label}_p99_ms_avg"]) for row in rows]))
    bar_chart(tests, series_rps, "Redis sched_ext 后端对照 RPS", "Requests/sec", out_dir / "redis_sched_ext_rps.svg")
    bar_chart(tests, series_p99, "Redis sched_ext 后端对照 P99", "P99 (ms)", out_dir / "redis_sched_ext_p99.svg")


def render_nginx(nginx_csv: Path, out_dir: Path):
    row = load_single_row(nginx_csv)
    labels = [
        "quiet_default",
        "quiet_scx_normal",
        "noisy_default",
        "noisy_cgroup_v2",
        "noisy_scx_normal",
        "noisy_scx_always_active",
        "noisy_scx_psi",
    ]
    series_rps = [("Requests/sec", [float(row[f"{label}_rps_avg"]) for label in labels])]
    series_p99 = [("P99 (ms)", [float(row[f"{label}_p99_ms_avg"]) for label in labels])]
    bar_chart(labels, series_rps, "Nginx sched_ext 后端对照吞吐", "Requests/sec", out_dir / "nginx_sched_ext_rps.svg")
    bar_chart(labels, series_p99, "Nginx sched_ext 后端对照 P99", "P99 (ms)", out_dir / "nginx_sched_ext_p99.svg")


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: render_sched_ext_figures.py <redis_compare.csv> <nginx_compare.csv> <out_dir>", file=sys.stderr)
        return 1

    redis_csv = Path(sys.argv[1])
    nginx_csv = Path(sys.argv[2])
    out_dir = Path(sys.argv[3])
    out_dir.mkdir(parents=True, exist_ok=True)

    render_redis(redis_csv, out_dir)
    render_nginx(nginx_csv, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
