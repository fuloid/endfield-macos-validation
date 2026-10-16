#!/usr/bin/env python3
"""Compile the exact helpers extracted from patched Wine, on Linux or macOS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
HANDOFF = ROOT / 'port/mac-handoff'
REV = '455e3509b98a6919fd4ad1def4803e08c41c03b2'
SIGNAL = 'dlls/ntdll/unix/signal_x86_64.c'


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='wine-helpers-'))
    if args.source:
        source = (args.source / SIGNAL).read_text()
    else:
        target = work / SIGNAL
        target.parent.mkdir(parents=True)
        url = f'https://raw.githubusercontent.com/wine-mirror/wine/{REV}/{SIGNAL}'
        with urllib.request.urlopen(url, timeout=90) as response:
            target.write_bytes(response.read())
        for patch in sorted((HANDOFF / 'patches').glob('000[12]-*.patch')):
            run(['git', 'apply', '--check', patch], cwd=work)
            run(['git', 'apply', patch], cwd=work)
        source = target.read_text()
    compiler = shlex.split(os.environ.get('HOST_CC', 'cc'))
    report = {'revision': REV, 'source': str(args.source) if args.source else 'pinned download + patches',
              'patched_signal_sha256': hashlib.sha256(source.encode()).hexdigest(), 'tests': {}}
    for name, helper, filename, summary in [
        ('noptest', 'rosetta_skip_long_nop', 'extracted.c', 'NOP checks: 4378, failures: 0'),
        ('xgetbvtest', 'rosetta_emulate_xgetbv', 'extracted_xgetbv.c', 'XGETBV checks: 11, failures: 0')]:
        match = re.search(r'^static (?:inline )?BOOL ' + helper + r'\(.*?^\}', source, re.M | re.S)
        if not match:
            raise RuntimeError(f'Helper missing: {helper}')
        body = match.group(0) + '\n'
        if body != (HANDOFF / filename).read_text():
            raise RuntimeError(f'Helper differs from audited copy: {helper}')
        (work / filename).write_text(body, encoding='utf-8', newline='\n')
        shutil.copyfile(HANDOFF / f'{name}.c', work / f'{name}.c')
        flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        exe = work / (name + ('.exe' if os.name == 'nt' else ''))
        run([*compiler, *flags, work / f'{name}.c', '-o', exe])
        result = run([exe], capture_output=True, text=True)
        print(result.stdout, flush=True)
        (args.output / f'{name}.log').write_text(result.stdout)
        if summary not in result.stdout:
            raise RuntimeError(f'Expected summary missing: {name}')
        report['tests'][name] = {'passed': True, 'helper_sha256': hashlib.sha256(body.encode()).hexdigest()}
    (args.output / 'helpers.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
