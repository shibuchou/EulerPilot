#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


WIDTH = 900
HEIGHT = 420
MARGIN_LEFT = 70
MARGIN_RIGHT = 30
MARGIN_TOP = 50
MARGIN_BOTTOM = 70


def read_redis(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def read_single(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            return row
    return {}


def bar_chart(labels, series, title, ylabel, output: Path):
    chart_w = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
    chart_h = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
    max_value = max(v for _, values in series for v in values) if series else 1
    max_value = max(max_value, 1)
    group_count = len(labels)
    series_count = len(series)
    group_w = chart_w / max(group_count, 1)
    bar_w = group_w / max(series_count + 1, 2)
    colors = ["#1976d2", "#ef6c00", "#388e3c"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}">',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:12px;fill:#222}.title{font-size:18px;font-weight:bold}</style>',
        f'<text class="title" x="{WIDTH/2}" y="28" text-anchor="middle">{title}</text>',
        f'<line x1="{MARGIN_LEFT}" y1="{HEIGHT-MARGIN_BOTTOM}" x2="{WIDTH-MARGIN_RIGHT}" y2="{HEIGHT-MARGIN_BOTTOM}" stroke="#444"/>',
        f'<line x1="{MARGIN_LEFT}" y1="{MARGIN_TOP}" x2="{MARGIN_LEFT}" y2="{HEIGHT-MARGIN_BOTTOM}" stroke="#444"/>',
        f'<text x="18" y="{MARGIN_TOP+10}" transform="rotate(-90,18,{MARGIN_TOP+10})">{ylabel}</text>',
    ]

    for i, label in enumerate(labels):
        gx = MARGIN_LEFT + i * group_w
        parts.append(f'<text x="{gx + group_w/2}" y="{HEIGHT-MARGIN_BOTTOM+20}" text-anchor="middle">{label}</text>')

        for sidx, (name, values) in enumerate(series):
            value = values[i]
            bh = (value / max_value) * (chart_h - 10)
            x = gx + bar_w * (sidx + 0.5)
            y = HEIGHT - MARGIN_BOTTOM - bh
            color = colors[sidx % len(colors)]
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w*0.7:.1f}" height="{bh:.1f}" fill="{color}"/>')
            parts.append(f'<text x="{x + bar_w*0.35:.1f}" y="{y-4:.1f}" text-anchor="middle">{value:.2f}</text>')

    legend_x = WIDTH - MARGIN_RIGHT - 180
    legend_y = 42
    for idx, (name, _) in enumerate(series):
        color = colors[idx % len(colors)]
        parts.append(f'<rect x="{legend_x}" y="{legend_y + idx*18}" width="12" height="12" fill="{color}"/>')
        parts.append(f'<text x="{legend_x+18}" y="{legend_y+10 + idx*18}">{name}</text>')

    parts.append("</svg>")
    output.write_text("\n".join(parts), encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 6:
        print("usage: render_candidate_figures.py <redis_compare.csv> <nginx_default.csv> <nginx_active.csv> <out_dir> <prefix>", file=sys.stderr)
        return 1

    redis_rows = read_redis(Path(sys.argv[1]))
    nginx_default = read_single(Path(sys.argv[2]))
    nginx_active = read_single(Path(sys.argv[3]))
    out_dir = Path(sys.argv[4])
    prefix = sys.argv[5]
    out_dir.mkdir(parents=True, exist_ok=True)

    redis_labels = [row["test"] for row in redis_rows]
    redis_rps_default = [float(row["default_noisy_rps_avg"]) for row in redis_rows]
    redis_rps_active = [float(row["active_noisy_rps_avg"]) for row in redis_rows]
    redis_p99_default = [float(row["default_noisy_p99_ms_avg"]) for row in redis_rows]
    redis_p99_active = [float(row["active_noisy_p99_ms_avg"]) for row in redis_rows]

    bar_chart(
        redis_labels,
        [("default_noisy", redis_rps_default), ("active_noisy", redis_rps_active)],
        "Redis 吞吐对比",
        "Requests/sec",
        out_dir / f"{prefix}_redis_rps.svg",
    )
    bar_chart(
        redis_labels,
        [("default_noisy", redis_p99_default), ("active_noisy", redis_p99_active)],
        "Redis P99 对比",
        "P99 (ms)",
        out_dir / f"{prefix}_redis_p99.svg",
    )

    nginx_labels = ["default_noisy", "active_noisy"]
    nginx_rps = [float(nginx_default["requests_per_sec"]), float(nginx_active["requests_per_sec"])]
    def parse_ms(value: str) -> float:
        if value.endswith("us"):
            return float(value[:-2]) / 1000.0
        if value.endswith("ms"):
            return float(value[:-2])
        return float(value)

    nginx_p99 = [parse_ms(nginx_default["p99_latency"]), parse_ms(nginx_active["p99_latency"])]
    bar_chart(
        nginx_labels,
        [("Nginx", nginx_rps)],
        "Nginx 吞吐对比",
        "Requests/sec",
        out_dir / f"{prefix}_nginx_rps.svg",
    )
    bar_chart(
        nginx_labels,
        [("Nginx", nginx_p99)],
        "Nginx P99 对比",
        "P99 (ms)",
        out_dir / f"{prefix}_nginx_p99.svg",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
