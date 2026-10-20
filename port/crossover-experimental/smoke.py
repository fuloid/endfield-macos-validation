#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run independent CPU/syscall/runtime-library checks; never launch a game."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    prefix = work / 'wine-install'
    out = work / 'smoke'
    out.mkdir(exist_ok=True)
    env = dict(os.environ, WINEPREFIX=str(work / 'probe-prefix'), WINEARCH='win64',
               DYLD_FALLBACK_LIBRARY_PATH=str(prefix / 'lib/external') + ':/usr/lib',
               WINEDLLOVERRIDES='mscoree,mshtml,winedbg.exe=;winemenubuilder.exe=d',
               ROSETTA_ADVERTISE_AVX='1', WINEDEBUG='+seh',
               WINEESYNC='0', WINEFSYNC='0', WINENTSYNC='0')
    wine = prefix / 'bin/wine'
    tests = {}

    def run(name, command, timeout=120, contains=None):
        with (out / (name + '.log')).open('w') as log:
            try:
                result = subprocess.run(list(map(str, command)), env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=timeout)
                tests[name] = {'exit_code': result.returncode, 'passed': result.returncode == 0}
            except subprocess.TimeoutExpired:
                tests[name] = {'passed': False, 'timeout_seconds': timeout}
        if contains:
            tests[name]['expected_marker'] = contains
            tests[name]['marker_found'] = contains in (out / (name + '.log')).read_text(errors='replace')
            tests[name]['passed'] &= tests[name]['marker_found']
        print(f'{name}: {tests[name]}', flush=True)
        return tests[name]['passed']

    try:
        for name, src in [('syscall', ROOT / 'tools/syscall-probe/syscall-probe.c'),
                          ('rosetta', ROOT / 'port/mac-handoff/rosetta_probe.c')]:
            run('compile-' + name, ['x86_64-w64-mingw32-gcc', '-O2', '-static', '-Wall',
                '-Wextra', '-Werror', src, '-o', out / (name + '.exe')])
        run('version', [wine, '--version'])
        run('wineboot', [wine, 'wineboot', '-u'], timeout=240)
        run('cmd', [wine, 'cmd', '/c', 'echo CROSSOVER_STANDALONE_OK'], contains='CROSSOVER_STANDALONE_OK')
        if tests['compile-syscall']['passed']:
            run('syscall', [wine, out / 'syscall.exe'], timeout=90,
                contains='PASS direct syscall on main and worker threads')
        if tests['compile-rosetta']['passed']:
            run('rosetta', [wine, out / 'rosetta.exe'], contains='Runtime checks: 8, failures: 0')
        # Load every bundled dylib in a separate x86_64 process, catching closure
        # gaps even when the Wine smoke doesn't reach the corresponding feature.
        if run('compile-dlopen', ['/usr/bin/clang', '-arch', 'x86_64', '-O2', '-Wall',
                '-Wextra', '-Werror', ROOT / 'scripts/mac_dlopen_probe.c', '-o', out / 'dlopen-probe']):
            run('dlopen', [out / 'dlopen-probe', *sorted((prefix / 'lib/external').glob('*.dylib'))])
    finally:
        subprocess.run([str(prefix / 'bin/wineserver'), '-k'], env=env, timeout=30, check=False)
        report = {'scope': 'Compilation, startup, dylib closure, CPU exceptions and direct syscalls only',
                  'gameplay_verified': False, 'tests': tests,
                  'passed': bool(tests) and all(t['passed'] for t in tests.values())}
        (out / 'smoke.json').write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
