#!/usr/bin/env python3
"""Extract a local CPack archive and run it without relying on the source checkout."""
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

if len(sys.argv) != 2:
    raise SystemExit('Usage: python3 scripts/package_smoke.py PATH_TO_ARCHIVE.tar.gz')
archive = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='failurelab-package-') as temporary:
    location = Path(temporary)
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            relative = Path(member.name)
            if relative.is_absolute() or '..' in relative.parts or not (member.isfile() or member.isdir()):
                raise SystemExit('Unexpected archive entry: '+member.name)
        package.extractall(location)
    roots = list(location.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise SystemExit('Expected one archive root directory')
    root = roots[0]
    exe = root/'bin/failurelab'
    subprocess.run([str(exe), '--version'], cwd=root, check=True)
    # macOS tar archives may include AppleDouble resource metadata alongside real files.
    scenarios = sorted(p for p in root.joinpath('share/failurelab/scenarios').glob('*.json')
                       if not p.name.startswith('._'))
    if len(scenarios) != 5:
        raise SystemExit('Package must include all five scenarios')
    for config in scenarios:
        subprocess.run([str(exe), 'validate', '--config', str(config)], cwd=root, check=True)
    subprocess.run([str(exe), 'compare', '--config', 'share/failurelab/scenarios/recovery.json',
                    '--output', 'results/comparison'], cwd=root, check=True)
    report = json.loads(root.joinpath('results/comparison/failurelab-report.json').read_text())
    if len(report['results']) != 4:
        raise SystemExit('Comparison did not contain four strategies')
    for required in ('share/failurelab/licenses/nlohmann-json.LICENSE.MIT',
                     'share/doc/failurelab/THIRD_PARTY.md'):
        if not (root/required).is_file():
            raise SystemExit('Missing package notice: '+required)
    if list(root.rglob('doctest.h')):
        raise SystemExit('Test framework should not be installed in application package')
print('Package smoke check passed: extracted executable, five scenarios, reports and notices.')
