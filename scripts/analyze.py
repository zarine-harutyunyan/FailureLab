#!/usr/bin/env python3
"""Run the installed Clang compiler's static analyzer on every production source."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build/debug')
args = parser.parse_args()
database = json.loads((Path(args.build)/'compile_commands.json').read_text())
for entry in database:
    source = Path(entry['file'])
    if source.parent.name != 'src':
        continue
    command = shlex.split(entry['command'])
    filtered = []
    skip = False
    for arg in command:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT'):
            skip = True
            continue
        if arg in ('-c', '-MD', '-MMD'):
            continue
        filtered.append(arg)
    subprocess.run([*filtered, '--analyze', '-Xanalyzer', '-analyzer-output=text', '-Xanalyzer', '-analyzer-werror'], cwd=entry['directory'], check=True)
    print('Analyzed:', source.name, flush=True)
