#!/usr/bin/env python3
"""Initialize the installed Wine and run the CPU probe on an Apple Silicon host."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HANDOFF = ROOT / 'port/mac-handoff'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('This test must run on native Apple Silicon macOS.')
    out = args.work / 'smoke'
    out.mkdir(parents=True, exist_ok=True)
    prefix = args.work / 'wine-install'
    wine = prefix / 'bin/wine'
    if not wine.is_file():
        wine = prefix / 'bin/wine64'
    if not wine.is_file():
        raise SystemExit('Installed Wine loader is missing.')
    # Runtime libraries are bundled in wine-install/lib/external and found through
    # DYLD_FALLBACK_LIBRARY_PATH, as in the app. Wine and the probes are started directly (they
    # are x86_64, so Rosetta runs them): /usr/bin/arch is SIP-protected and would strip DYLD_*.
    external = prefix / 'lib/external'
    env = dict(os.environ, WINEPREFIX=str(args.work / 'ci-prefix'), WINEARCH='win64',
               ROSETTA_ADVERTISE_AVX='1', WINEFSYNC='0', WINEESYNC='0', WINENTSYNC='0',
               WINEDLLOVERRIDES='mscoree,mshtml,winedbg.exe=', WINEDEBUG='+seh',
               DYLD_FALLBACK_LIBRARY_PATH=f'{external}:/usr/lib')
    report = {'host': platform.platform(), 'architecture': platform.machine(), 'tests': {},
              'scope': 'Wine/Rosetta CPU/exception smoke tests only; no graphics or game.'}

    def execute(name, command, timeout, extra_env=None):
        print(f'Running {name}', flush=True)
        with (out / f'{name}.log').open('w') as log:
            try:
                result = subprocess.run([str(a) for a in command], stdout=log,
                                        stderr=subprocess.STDOUT, timeout=timeout,
                                        env=dict(env, **(extra_env or {})))
                report['tests'][name] = {'exit_code': result.returncode}
                if result.returncode:
                    raise RuntimeError(f'{name} exited {result.returncode}; see artifact log')
            except subprocess.TimeoutExpired:
                report['tests'][name] = {'timed_out': True, 'timeout_seconds': timeout}
                raise

    try:
        # config.h exists after a real build; cached builds carry a copy in wine-install. Builds
        # cached before that copy existed fall back to the paths build-mac.sh configures.
        config = ''
        for candidate in (args.work / 'wine-build/include/config.h', prefix / 'BUILD-config.h'):
            if candidate.is_file():
                config = candidate.read_text()
                break
        libraries = []
        for macro in ['SONAME_LIBFREETYPE', 'SONAME_LIBGNUTLS', 'SONAME_LIBVULKAN']:
            match = re.search(r'^#define ' + macro + r' "([^"]+)"', config, re.M)
            if not match:
                raise RuntimeError(f'{macro} missing from the build configuration')
            path = Path(match.group(1))
            if not path.is_absolute():   # leaf name: must be bundled in lib/external
                path = external / path.name
            if not path.is_file():
                raise RuntimeError(f'{macro}: {path} is not bundled')
            libraries.append(path)
        report['bundled_libraries'] = sorted(p.name for p in external.glob('*.dylib'))
        execute('compile-dlopen-probe', ['/usr/bin/clang', '-arch', 'x86_64', '-O2', '-Wall', '-Wextra',
                                        '-Werror', ROOT / 'scripts/mac_dlopen_probe.c', '-o', out / 'dlopen-probe'], 60)
        execute('load-runtime-libraries', [out / 'dlopen-probe', *libraries], 60)
        execute('compile-probe', ['x86_64-w64-mingw32-gcc', '-O2', '-static', '-Wall', '-Wextra',
                                 '-Werror', HANDOFF / 'rosetta_probe.c', '-o', out / 'rosetta_probe.exe'], 120)
        execute('version', [wine, '--version'], 60)
        execute('wineboot', [wine, 'wineboot', '-u'], 240)
        execute('probe', [wine, out / 'rosetta_probe.exe'], 120)
        content = (out / 'probe.log').read_text(errors='replace')
        if 'Runtime checks: 8, failures: 0' not in content or content.count('PASS ') != 8:
            raise RuntimeError('Probe did not report all eight passing checks')
        report.update(nop_skip_trace_found='-byte NOP' in content,
                      xgetbv_emulation_trace_found='emulated xgetbv' in content)
        print('\n'.join(line for line in content.splitlines() if any(
            word in line for word in ['CPUID:', 'PASS ', 'FAIL ', 'INFO ', 'Runtime checks:', 'emulated xgetbv', '-byte NOP', 'executable page'])), flush=True)
        # Endfield's protector makes direct syscalls; a failing one exits the game with 0xDEADC0DE (0016).
        execute('compile-syscall-probe', ['x86_64-w64-mingw32-gcc', '-O2', '-static', '-Wall', '-Wextra',
                                         '-Werror', ROOT / 'tools/syscall-probe/syscall-probe.c',
                                         '-o', out / 'syscall-probe.exe'], 120)
        execute('syscall-probe', [wine, out / 'syscall-probe.exe'], 90)
        content = (out / 'syscall-probe.log').read_text(errors='replace')
        print('\n'.join(line for line in content.splitlines() if ':trace:' not in line), flush=True)
        if 'PASS direct syscall on main and worker threads' not in content:
            raise RuntimeError('Direct syscall probe did not pass')
        report.update(direct_syscall_passed=True)
        # 0017 hides builtin version resources only when asked; 0018 execs child processes
        # through a link named after the exe in $TMPDIR/winetemp-*/.
        execute('compile-version-probe', ['x86_64-w64-mingw32-gcc', '-O2', '-static', '-municode', '-Wall',
                                         '-Wextra', '-Werror', ROOT / 'tools/version-probe/version-probe.c',
                                         '-o', out / 'version-probe.exe', '-lversion'], 120)
        execute('version-probe', [wine, out / 'version-probe.exe'], 90)
        execute('version-probe-hidden', [wine, out / 'version-probe.exe'], 90,
                {'CX_HIDE_BUILTIN_VERSION_RESOURCES': '1'})
        shown = (out / 'version-probe.log').read_text(errors='replace')
        hidden = (out / 'version-probe-hidden.log').read_text(errors='replace')
        print(shown + hidden, flush=True)
        size = re.search(r'^version-size=(\d+) error=(\d+)', shown, re.M)
        if not size or int(size.group(1)) == 0:
            raise RuntimeError('kernel32.dll version resource is not readable by default')
        if not re.search(r'^version-size=0 error=1814\r?$', hidden, re.M):
            raise RuntimeError('CX_HIDE_BUILTIN_VERSION_RESOURCES=1 did not hide the builtin version resource')
        if 'child exit=42' not in shown:
            raise RuntimeError('version-probe child process did not run')
        temp = subprocess.run(['getconf', 'DARWIN_USER_TEMP_DIR'], capture_output=True, text=True).stdout.strip()
        links = sorted(str(p) for p in Path(temp).glob('winetemp-*/version-probe'))
        print('process links:', links, flush=True)
        if not links:
            raise RuntimeError('no winetemp link named after the child process (0018)')
        # 0021 pads keys saved with RegSaveKey, which ACE reads back looking for a binary hive.
        execute('compile-regsave-probe', ['x86_64-w64-mingw32-gcc', '-O2', '-static', '-Wall', '-Wextra',
                                         '-Werror', ROOT / 'tools/regsave-probe/regsave-probe.c',
                                         '-o', out / 'regsave-probe.exe', '-ladvapi32'], 120)
        execute('regsave-probe', [wine, out / 'regsave-probe.exe'], 90)
        saved = (out / 'regsave-probe.log').read_text(errors='replace')
        print(saved, flush=True)
        match = re.search(r'^regsave size=(\d+)', saved, re.M)
        if not match or int(match.group(1)) <= 0x1020:
            raise RuntimeError('RegSaveKey output is not padded past a hive header (0021)')
        report.update(regsave_padded=True)
        report.update(builtin_version_hiding_passed=True, process_name_link=links[0], passed=True)
    finally:
        (out / 'smoke.json').write_text(json.dumps(report, indent=2) + '\n')
        server = prefix / 'bin/wineserver'
        if server.is_file():
            subprocess.run([str(server), '-k'], env=env, timeout=30, check=False)


if __name__ == '__main__':
    main()
