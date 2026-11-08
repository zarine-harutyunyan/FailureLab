#!/usr/bin/env python3
"""Regenerate the committed recovery report and README table from the executable."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', default='build/release/failurelab')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
subprocess.run([str(Path(args.executable).resolve()), 'compare', '--config', str(root/'scenarios/recovery.json'), '--output', str(root/'examples/recovery'), '--overwrite'], check=True)
report = json.loads((root/'examples/recovery/failurelab-report.json').read_text())
rows = ['| Strategy | Successes / offered | Attempts | Amplification | Request p95 (ms) |',
        '| --- | ---: | ---: | ---: | ---: |']
for r in report['results']:
    rows.append(f"| {r['strategy']} | {r['successful_requests']} / {r['logical_requests_offered']} | {r['downstream_attempts']} | {r['attempt_amplification']:.3f} | {r['request_latency']['p95_ms']} |")
readme = root/'README.md'
text = readme.read_text()
begin, end = '<!-- generated-results:start -->', '<!-- generated-results:end -->'
head, rest = text.split(begin, 1)
_, tail = rest.split(end, 1)
readme.write_text(head+begin+'\n'+'\n'.join(rows)+'\n'+end+tail)
