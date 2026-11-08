#!/usr/bin/env python3
"""Check or apply the project's pinned C++ formatter."""
import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--fix', action='store_true')
parser.add_argument('--executable', default='clang-format')
args = parser.parse_args()
version = subprocess.check_output([args.executable, '--version'], text=True)
if 'version 18.1.8' not in version:
    raise SystemExit('clang-format 18.1.8 is required; see docs/development.md')
root = pathlib.Path(__file__).resolve().parents[1]
files = sorted([*root.joinpath('src').glob('*.cpp'), *root.joinpath('include').rglob('*.hpp'), *root.joinpath('tests').glob('*.cpp')])
subprocess.run([args.executable, *(['-i'] if args.fix else ['--dry-run', '--Werror']), *map(str, files)], check=True)
