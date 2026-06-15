#!/usr/bin/env python3
"""Build the EulerPilot static release dashboard from existing data."""
import base64
import csv
import html
import pathlib
import json

ROOT = pathlib.Path(__file__).resolve().parent.parent
DASHBOARD_DIR = ROOT / "reports" / "dashboard"
FIGURES_DIR = ROOT / "reports" / "final_figures"

SCENARIOS = [
    "quiet_default", "quiet_scx_normal",
    "noisy_default", "noisy_cgroup_v2",
    "noisy_scx_normal", "noisy_scx_always_active", "noisy_scx_psi"
]
SCENARIO_LABELS = {
    "quiet_default":         "quiet (default)",
    "quiet_scx_normal":      "quiet (scx normal)",
    "noisy_default":         "noisy (default)",
    "noisy_cgroup_v2":       "noisy (cgroup v2)",
    "noisy_scx_normal":      "noisy (scx normal)",
    "noisy_scx_always_active": "noisy (scx always active)",
    "noisy_scx_psi":         "noisy (scx psi)",
}
BEST_NOTES = {
    "noisy_cgroup_v2":       "cgroup v2 GET/INCR/SET RPS clearly improved",
    "noisy_scx_normal":      "scx normal GET/INCR/SET RPS improved; SET P99 improved",
    "noisy_scx_psi":         "scx psi GET throughput improved; P99 not consistent",
    "noisy_default":         "baseline",
    "quiet_default":         "quiet baseline (overhead reference)",
    "quiet_scx_normal":      "quiet scx normal (measurable overhead)",
    "noisy_scx_always_active": "always-active mode: unstable, higher P99 cost",
}


def load_skills(path):
    """Parse skills.yaml without PyYAML dep."""
    skills = []
    current = None
    for line in path.read_text().splitlines():
        s = line.strip()
        if s.startswith("- config:") or s.startswith("- name:"):
            if current:
                skills.append(current)
            current = {}
            if s.startswith("- name:"):
                current["name"] = s.split(":", 1)[1].strip()
        elif current is not None and s.startswith("name:"):
            current["name"] = s.split(":", 1)[1].strip()
        elif current is not None and s.startswith("enabled:"):
            current["enabled"] = s.split(":", 1)[1].strip()
        elif current is not None and s.startswith("kind:"):
            current["kind"] = s.split(":", 1)[1].strip()
    if current:
        skills.append(current)
    return skills


def embed_svg(path):
    raw = path.read_text()
    return base64.b64encode(raw.encode()).decode()


def parse_redis_csv(path):
    rows = list(csv.DictReader(path.read_text().splitlines()))
    return [r for r in rows if r.get("test") in ("GET", "INCR", "SET", "PING_INLINE")]


def parse_nginx_csv(path):
    rows = list(csv.DictReader(path.read_text().splitlines()))
    return rows[:1] if rows else []


def fmt(v):
    try:
        return f"{float(v):.1f}"
    except (ValueError, TypeError):
        return "-"


def best_scenario(row):
    best_rps = 0.0
    best = ""
    for s in SCENARIOS:
        key = f"{s}_rps_avg"
        if key in row:
            try:
                val = float(row[key])
                if val > best_rps:
                    best_rps = val
                    best = s
            except (ValueError, TypeError):
                pass
    return best


def build_redis_table(rows):
    lines = []
    lines.append('<table class="result-table">')
    lines.append("<thead><tr>")
    lines.append('<th class="op-col">op</th>')
    for s in SCENARIOS:
        label = s.replace("_", " ")
        lines.append(f'<th>{html.escape(label)}</th>')
    lines.append("<th>best</th></tr></thead><tbody>")

    for row in rows:
        lines.append("<tr>")
        op = row.get("test", "-")
        lines.append(f'<td class="op-col"><b>{html.escape(op)}</b></td>')
        best = best_scenario(row)
        for s in SCENARIOS:
            rps_key = f"{s}_rps_avg"
            p99_key = f"{s}_p99_ms_avg"
            rps = fmt(row.get(rps_key, "-"))
            p99 = fmt(row.get(p99_key, "-"))
            cls = "best" if s == best else ""
            td = f'<td class="{cls}">RPS: {rps}<br>P99: {p99}ms</td>'
            lines.append(td)
        lines.append(f'<td class="note">{html.escape(BEST_NOTES.get(best, "-"))}</td>')
        lines.append("</tr>")

    lines.append("</tbody></table>")
    return "\n".join(lines)


def build_nginx_table(rows):
    if not rows:
        return "<p>No Nginx CSV data available</p>"
    row = rows[0]
    best = best_scenario(row)
    lines = []
    lines.append('<div class="nginx-summary">')

    opts = [("RPS", "rps_avg"), ("P99", "p99_ms_avg")]
    for label, suffix in opts:
        lines.append(f'<h4>{label}</h4>')
        lines.append('<table class="result-table">')
        lines.append("<thead><tr>")
        lines.append("<th>scenario</th><th>{}</th></tr></thead><tbody>".format(label))
        for s in SCENARIOS:
            key = f"{s}_{suffix}"
            val = fmt(row.get(key, "-"))
            cls = "best" if s == best else ""
            sl = html.escape(SCENARIO_LABELS.get(s, s))
            lines.append(f'<tr><td class="{cls}">{sl}</td><td class="{cls}">{val}</td></tr>')
        lines.append("</tbody></table>")

    lines.append(f'<p class="note">best: {html.escape(SCENARIO_LABELS.get(best, best))} — {html.escape(BEST_NOTES.get(best, "-"))}</p>')
    lines.append("</div>")
    return "\n".join(lines)


def build():
    DASHBOARD_DIR.mkdir(parents=True, exist_ok=True)

    skills = load_skills(ROOT / "configs" / "skills.yaml")
    redis_rows = parse_redis_csv(ROOT / "results" / "final" / "redis-scx-compare-20260612-191543" / "compare_summary_avg.csv")
    nginx_rows = parse_nginx_csv(ROOT / "results" / "final" / "nginx-scx-compare-20260612-194018" / "compare_summary_avg.csv")

    svgs = {}
    for f in sorted(FIGURES_DIR.glob("*.svg")):
        svgs[f.stem] = embed_svg(f)

    # Build skill cards
    skill_cards = ""
    for sk in skills:
        enabled = sk.get("enabled") == "true"
        color = "#27ae60" if enabled else "#7f8c8d"
        label = "enabled" if enabled else "disabled"
        icon = "&#x2713;" if enabled else "&#x2013;"
        skill_cards += (
            f'<div class="skill-card" style="border-left: 4px solid {color}">'
            f'<span class="skill-icon" style="color:{color}">{icon}</span>'
            f'<b>{html.escape(sk["name"])}</b>'
            f'<span class="skill-status" style="color:{color}">{label}</span>'
            f'</div>\n'
        )

    # Build HTML
    html_content = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EulerPilot Release Dashboard</title>
<style>
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
body {{ font-family: -apple-system, "Segoe UI", Roboto, sans-serif; background: #ffffff; color: #333333; line-height: 1.6; max-width: 1200px; margin: 0 auto; padding: 20px; }}
h1 {{ color: #00838f; font-size: 2em; margin-bottom: 5px; }}
h2 {{ color: #00838f; border-bottom: 2px solid #e0e0e0; padding-bottom: 5px; margin: 40px 0 15px; }}
h3 {{ color: #555555; margin: 15px 0 8px; }}
h4 {{ color: #666666; margin: 10px 0 5px; }}
.subtitle {{ color: #999999; font-size: 1em; margin-bottom: 20px; }}
.meta {{ display: flex; gap: 20px; flex-wrap: wrap; margin-bottom: 20px; }}
.meta-item {{ background: #f5f5f5; padding: 8px 16px; border-radius: 6px; font-size: 0.9em; border: 1px solid #e0e0e0; }}
.skills {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 10px; margin: 10px 0; }}
.skill-card {{ background: #f5f5f5; padding: 12px 16px; border-radius: 6px; display: flex; align-items: center; gap: 10px; border: 1px solid #e0e0e0; }}
.skill-icon {{ font-size: 1.2em; }}
.skill-status {{ margin-left: auto; font-size: 0.85em; }}
.result-table {{ width: 100%; border-collapse: collapse; margin: 10px 0; font-size: 0.85em; }}
.result-table th {{ background: #e8e8e8; padding: 8px 6px; text-align: center; font-weight: 600; color: #333; }}
.result-table td {{ background: #fafafa; padding: 6px; text-align: center; border: 1px solid #e0e0e0; color: #333; }}
.result-table .op-col {{ font-weight: 600; min-width: 70px; }}
.result-table .best {{ background: #e8f5e9; font-weight: 600; }}
.result-table .note {{ font-size: 0.8em; color: #666666; max-width: 200px; text-align: left; }}
.chart {{ margin: 15px 0; text-align: center; }}
.chart img {{ max-width: 100%; height: auto; border-radius: 8px; }}
.demo-card {{ background: #f9f9f9; border: 1px solid #e0e0e0; border-radius: 8px; padding: 15px; margin: 10px 0; }}
.demo-card h4 {{ color: #00838f; margin-bottom: 8px; }}
.demo-step {{ display: flex; gap: 15px; flex-wrap: wrap; margin: 8px 0; }}
.demo-step div {{ background: #f0f0f0; padding: 8px 14px; border-radius: 4px; font-family: monospace; font-size: 0.9em; border: 1px solid #ddd; }}
.demo-result-ok {{ color: #27ae60; }}
.demo-result-deny {{ color: #e74c3c; }}
.footer {{ margin-top: 40px; padding-top: 15px; border-top: 1px solid #e0e0e0; color: #999999; font-size: 0.8em; text-align: center; }}
.nginx-summary table {{ max-width: 600px; }}
.nginx-summary .result-table td {{ text-align: right; padding-right: 12px; }}
</style>
</head>
<body>

<h1>EulerPilot</h1>
<p class="subtitle">openEuler Adaptive Resource Control Agent &mdash; Release Candidate Dashboard</p>

<div class="meta">
  <div class="meta-item">Version: v0.1-rc1</div>
  <div class="meta-item">OS: openEuler 24.03 LTS SP3</div>
  <div class="meta-item">Backend: cgroup_v2 + sched_ext</div>
  <div class="meta-item">Skills: 4 runtime skills</div>
</div>

<h2>Skills Status</h2>
<div class="skills">
{skill_cards}
</div>

<h2>Redis Results (RUNS=5)</h2>
{build_redis_table(redis_rows)}

<h3>Redis Charts</h3>
{''.join(f'<div class="chart"><img src="data:image/svg+xml;base64,{svgs[k]}" alt="{k}"></div>' for k in sorted(svgs) if 'redis' in k)}

<h2>Nginx Results (RUNS=5)</h2>
{build_nginx_table(nginx_rows)}

<h3>Nginx Charts</h3>
{''.join(f'<div class="chart"><img src="data:image/svg+xml;base64,{svgs[k]}" alt="{k}"></div>' for k in sorted(svgs) if 'nginx' in k)}

<h2>PsiGate Timeline</h2>
{''.join(f'<div class="chart"><img src="data:image/svg+xml;base64,{svgs[k]}" alt="{k}"></div>' for k in sorted(svgs) if 'psigate' in k)}

<h2>eBPF Extension Demos</h2>

<div class="demo-card">
  <h4>Network Policy Demo (cgroup/connect4)</h4>
  <div class="demo-step">
    <div>Before attach: <span class="demo-result-ok">curl -> 200</span></div>
    <div>During agent: <span class="demo-result-deny">curl -> 000 (denied)</span></div>
    <div>After rollback: <span class="demo-result-ok">curl -> 200</span></div>
  </div>
  <p style="font-size:0.8em;color:#78909c">Hook: cgroup/connect4 | Port: 18080 | Cgroup: demo-net</p>
</div>

<div class="demo-card">
  <h4>Security Policy Demo (BPF LSM file_open)</h4>
  <div class="demo-step">
    <div>Before attach: <span class="demo-result-ok">cat -> "TOP SECRET..."</span></div>
    <div>During agent: <span class="demo-result-deny">cat -> Operation not permitted</span></div>
    <div>After rollback: <span class="demo-result-ok">cat -> "TOP SECRET..."</span></div>
  </div>
  <p style="font-size:0.8em;color:#78909c">Hook: BPF LSM file_open | Target: demo/security_policy_demo/secret.txt</p>
</div>

<div class="footer">
  EulerPilot Release Dashboard &middot; Generated from RUNS=5 candidate results &middot; openEuler 24.03 LTS SP3 + OLK-6.6
</div>

</body>
</html>"""

    (DASHBOARD_DIR / "index.html").write_text(html_content, encoding="utf-8")
    print(f"Dashboard written: {DASHBOARD_DIR / 'index.html'} ({len(html_content)} bytes)")
    print(f"  Redis rows: {len(redis_rows)}, Nginx rows: {len(nginx_rows)}")
    print(f"  SVGs embedded: {len(svgs)}")
    print(f"  Skills: {len(skills)}")


if __name__ == "__main__":
    build()
