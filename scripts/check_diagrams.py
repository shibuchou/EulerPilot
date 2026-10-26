#!/usr/bin/env python3
"""Check diagram links and exported Office SVG fidelity without kernel access."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
FIGURES = ROOT / 'docs/assets/figures'
DOCUMENTS = ['README.md', 'design/INTERFACES.md', 'docs/EXPERIMENT_PROTOCOL.md',
             'docs/MANUAL_SP3.md', 'docs/REPORT.md', 'docs/项目说明书.md',
             'docs/设计文档.md', 'docs/用户手册.md', 'docs/部署文档.md', 'docs/测试文档.md']
EXPORTS = {'项目说明书':'项目说明书', '设计文档':'设计文档', '用户手册':'用户手册',
           '部署文档':'部署文档', '测试文档':'测试文档', 'REPORT':'参赛文档'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exports', type=Path, help='also check six DOCX/PDF pairs in this directory')
    args = parser.parse_args()
    models = json.loads((FIGURES/'diagrams.json').read_text(encoding='utf-8'))
    assert len({m['slug'] for m in models}) == len(models), 'duplicate diagram slug'
    for model in models:
        slug = model['slug']
        svg = ET.parse(FIGURES/f'{slug}.svg').getroot()
        assert svg.tag.endswith('}svg') and svg.get('viewBox'), slug
        assert not any(e.tag.endswith('}script') or e.tag.endswith('}foreignObject') for e in svg.iter()), slug
        cells = ET.parse(FIGURES/f'{slug}.drawio').findall('.//mxCell')
        ids = {c.get('id') for c in cells}
        assert len(ids) == len(cells), f'duplicate cell ids: {slug}'
        for cell in cells:
            if cell.get('edge') == '1':
                assert cell.get('source') in ids and cell.get('target') in ids, slug
                assert cell.find('mxGeometry') is not None, slug
    linked = set()
    references = 0
    for name in DOCUMENTS:
        md = ROOT/name
        for target in re.findall(r'!\[[^\]]*\]\(([^)]+\.svg)\)', md.read_text(encoding='utf-8')):
            svg = (md.parent/target).resolve()
            assert svg.is_file(), f'missing image: {name}: {target}'
            ET.parse(svg)
            linked.add(svg.stem)
            references += 1
    assert linked == {m['slug'] for m in models}, f'unused or unregistered SVG: {linked}'
    report = {'figures':len(models), 'markdown_files':len(DOCUMENTS), 'image_references':references}
    if args.exports:
        summaries=[]
        for source, label in EXPORTS.items():
            docx=args.exports/f'EulerPilot-v2{label}-v1.1.docx'
            pdf=docx.with_suffix('.pdf')
            assert pdf.read_bytes().startswith(b'%PDF-'), pdf
            count=len(re.findall(r'!\[[^\]]*\]\([^)]+\.svg\)',(ROOT/'docs'/f'{source}.md').read_text(encoding='utf-8')))
            with zipfile.ZipFile(docx) as archive:
                assert archive.testzip() is None, docx
                document=ET.fromstring(archive.read('word/document.xml'))
                drawings=list(document.iter('{http://schemas.microsoft.com/office/drawing/2016/SVG/main}svgBlip'))
                assert len(drawings)==count,(docx,len(drawings),count)
                rels=ET.fromstring(archive.read('word/_rels/document.xml.rels'))
                by_id={r.get('Id'):r.get('Target') for r in rels}
                for blip in drawings:
                    rid=blip.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}embed')
                    part='word/'+by_id[rid]
                    expected=(FIGURES/Path(part).name).read_bytes()
                    assert hashlib.sha256(archive.read(part)).digest()==hashlib.sha256(expected).digest(),(docx,part)
                for part in archive.namelist():
                    if part.endswith('.xml') or part.endswith('.rels'):
                        ET.fromstring(archive.read(part))
            summaries.append({'source':source,'svg_drawings':count,'docx':str(docx),'pdf':str(pdf)})
        report['exports']=summaries
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
