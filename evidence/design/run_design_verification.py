import json
from pathlib import Path
import re
import subprocess
import sys

root = Path('/mnt/d/code/Ubuntu/EulerPilot-v2')
evidence = root / 'evidence/design'
checks = []
for script, args, expected, output_name in [
    ('validate_design.py', [], 0, 'contract-validation.json'),
    ('doctor.py', [], 0, 'ubuntu22-doctor.json'),
    ('doctor.py', ['--require-scx'], 2, None),
]:
    proc = subprocess.run([sys.executable, str(root / 'scripts' / script), *args], capture_output=True, text=True)
    if proc.returncode != expected:
        raise RuntimeError(f'{script} returned {proc.returncode}, expected {expected}: {proc.stderr}')
    result = json.loads(proc.stdout)
    if output_name:
        (evidence / output_name).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    checks.append({'tool': script, 'args': args, 'exit_code': proc.returncode, 'expected': expected})

broken = []
checked = 0
for path in root.rglob('*.md'):
    if 'reference' in path.parts:
        continue
    body = path.read_text(encoding='utf-8-sig')
    for match in re.finditer(r'\[[^\]]*\]\(([^)]+)\)', body):
        dest = match.group(1).strip('<>')
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', dest) or dest.startswith('#'):
            continue
        dest = dest.split('#', 1)[0]
        checked += 1
        if not (path.parent / dest).exists():
            broken.append({'file': str(path.relative_to(root)), 'target': dest})
if broken:
    raise RuntimeError(json.dumps(broken, ensure_ascii=False))
summary = {'layer': 'design-only', 'checks': checks, 'local_markdown_links_checked': checked, 'broken_local_links': broken, 'kernel_runtime_verified': False, 'v2_performance_verified': False}
(evidence / 'verification.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
