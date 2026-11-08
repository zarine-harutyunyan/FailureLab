#!/usr/bin/env python3
"""Validate every bundled scenario, then run the documented recovery comparison."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', default='build/release/failurelab')
parser.add_argument('--output', default='results/comparison')
parser.add_argument('--overwrite', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
exe = str(Path(args.executable).resolve())
for scenario in sorted((root/'scenarios').glob('*.json')):
    subprocess.run([exe, 'validate', '--config', str(scenario)], check=True)
subprocess.run([exe, 'compare', '--config', str(root/'scenarios/recovery.json'), '--output', args.output, *(['--overwrite'] if args.overwrite else [])], check=True)
