#!/usr/bin/env python3
"""Render a markdown document as a large-type teleprompter PDF.

One shot per page, narration at reading distance, on-screen cues visually
separate from the words to be spoken. Requires pandoc and a Chromium-family
browser for the print step.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

CSS = """
@page { size: A4; margin: 14mm 16mm; }
body { font-family: "Microsoft YaHei", "Segoe UI", sans-serif; color: #1a1a1a;
       line-height: 1.55; font-size: 12.5pt; max-width: none; margin: 0; }
h1 { font-size: 26pt; color: #1F4E79; margin: 0 0 6pt; }
h2 { font-size: 18pt; color: #1F4E79; border-bottom: 2px solid #9BB7D4;
     padding-bottom: 3pt; page-break-before: always; page-break-after: avoid; }
h3 { font-size: 17pt; color: #1F4E79; page-break-before: always; margin: 0 0 8pt; }
h3.nobreak { page-break-before: auto; }
h1 + p, h2 + p, h3 + p { margin-top: 0; }
blockquote { font-size: 19pt; line-height: 1.75; margin: 0 0 12pt;
             padding: 8pt 14pt; border-left: 6px solid #1F4E79; background: #F4F6F8; }
blockquote p { margin: 0 0 8pt; }
code { font-family: Consolas, monospace; font-size: 0.86em; background: #EDF3F9;
       padding: 0 3px; border-radius: 3px; }
table { border-collapse: collapse; width: 100%; font-size: 10.5pt; margin: 8pt 0 14pt; }
th { background: #1F4E79; color: #fff; text-align: left; }
th, td { border: 1px solid #9BB7D4; padding: 4pt 6pt; vertical-align: top; }
ul, ol { font-size: 12.5pt; }
hr { display: none; }
"""


def to_html(md: Path, out_dir: Path) -> Path:
    head = out_dir / "prompter.css.html"
    head.write_text(f"<style>{CSS}</style>\n", encoding="utf-8")
    html = out_dir / (md.stem + ".html")
    subprocess.run([
        "pandoc", str(md), "-s", "-f", "markdown+east_asian_line_breaks",
        "--metadata", f"title={md.stem} 提词版",
        "-H", str(head), "-o", str(html),
    ], check=True)
    # A section heading that is immediately followed by a shot must not strand a page.
    marked = html.read_text(encoding="utf-8")
    marked = re.sub(r"(</h2>)\s*<h3", r'\1\n<h3 class="nobreak"', marked)
    html.write_text(marked, encoding="utf-8")
    return html


def to_pdf(html: Path, pdf: Path, browser: str) -> None:
    subprocess.run([
        browser, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
        f"--print-to-pdf={pdf}", html.as_uri(),
    ], check=True, capture_output=True)


def find_browser(explicit: str | None) -> str:
    if explicit:
        return explicit
    candidates = [
        r"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        r"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
    ]
    for path in candidates:
        if Path(path).exists():
            return path
    raise SystemExit("no Chrome/Edge found; pass --browser <path>")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=str(REPO / "docs" / "VIDEO_SCRIPT.md"))
    ap.add_argument("--out", default=str(REPO / "docs" / "EulerPilot-v2演示提词版-v1.1.pdf"))
    ap.add_argument("--browser")
    ap.add_argument("--keep-html", action="store_true")
    args = ap.parse_args()

    md = Path(args.source)
    if not md.exists():
        raise SystemExit(f"missing source: {md}")
    browser = find_browser(args.browser)
    with tempfile.TemporaryDirectory() as td:
        html = to_html(md, Path(td))
        to_pdf(html, Path(args.out), browser)
        if args.keep_html:
            html.replace(md.with_suffix(".prompter.html"))
    size = Path(args.out).stat().st_size
    page_marks = re.compile(rb"/Type\s*/Page[^s]")
    with open(args.out, "rb") as handle:
        count = len(page_marks.findall(handle.read()))
    print(f"{Path(args.out).relative_to(REPO).as_posix()}  ({size // 1024} KiB, ~{count} pages)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
