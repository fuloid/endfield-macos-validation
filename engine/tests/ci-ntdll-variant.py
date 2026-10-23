#!/usr/bin/env python3
"""Exercise the experimental ntdll pair without making a rendering or gameplay claim."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time


def run(command, env, log, timeout):
    with log.open('w') as output:
        process = subprocess.Popen([str(x) for x in command], env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            return {'exit_code': code, 'timed_out': False}
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            return {'exit_code': process.returncode, 'timed_out': True}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_factory_sample(command, env, log, sample):
    with log.open('w') as output:
        process = subprocess.Popen([str(x) for x in command], env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 90
        factory_seen = None
        sampled = False
        while process.poll() is None:
            now = time.monotonic()
            if factory_seen is None and re.search(r'^factory=starting\r?$', log.read_text(errors='replace'), re.M):
                factory_seen = now
            if factory_seen is not None and now >= factory_seen + 3 and not sampled:
                subprocess.run(['/usr/bin/sample', str(process.pid), '2', '10', '-file', str(sample)],
                               stdout=output, stderr=subprocess.STDOUT, timeout=20, check=False)
                sampled = True
            if now >= deadline:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                return {'exit_code': process.returncode, 'timed_out': True, 'factory_sampled': sampled}
            time.sleep(0.25)
        return {'exit_code': process.returncode, 'timed_out': False, 'factory_sampled': sampled}


def reports():
    root = Path.home() / 'Library/Logs/DiagnosticReports'
    return {p: p.stat().st_mtime_ns for p in root.rglob('*') if p.is_file()}


def main():
    engine, tools, out = [Path(p).resolve() for p in sys.argv[1:]]
    out.mkdir(parents=True, exist_ok=True)
    results = {'trees': {}, 'spawn': {}, 'variant_controls': {}, 'passed': False}
    failures = []

    def save():
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')

    def require(condition, label):
        if not condition:
            failures.append(label)
        print(f'{"PASS" if condition else "FAIL"} {label}', flush=True)

    def environment(tree, prefix):
        return dict(os.environ, WINEPREFIX=str(prefix), WINEARCH='win64',
                    ROSETTA_ADVERTISE_AVX='1', WINEESYNC='0', WINEFSYNC='0', WINENTSYNC='0',
                    WINEDLLOVERRIDES='mscoree,mshtml,winedbg.exe=;winemenubuilder.exe=d',
                    WINEDEBUG='fixme-all,err+all,warn+seh,+seh,+rosetta',
                    DYLD_FALLBACK_LIBRARY_PATH=f'{engine}/moltenvk:{engine}/wine/lib/external:/usr/lib',
                    D3DMETAL_FRAMEWORK_PATH=str(tree / 'lib/external/D3DMetal.framework/D3DMetal'),
                    DYLD_FALLBACK_FRAMEWORK_PATH=str(tree / 'lib/external'))

    try:
        for name in ['wine', 'wine-dxmt', 'wine-d3dmetal']:
            tree = engine / name
            evidence = out / name
            evidence.mkdir()
            env = environment(tree, out / (name + '-prefix'))
            wine, server = tree / 'bin/wine', tree / 'bin/wineserver'
            result = results['trees'][name] = {'tests': {}}
            result['ntdll_sha256'] = {arch: digest(tree / 'lib/wine' / arch / file)
                                     for arch, file in [('x86_64-unix', 'ntdll.so'),
                                                        ('x86_64-windows', 'ntdll.dll')]}
            result['variant_marker'] = (tree / 'EXPERIMENTAL-D3DMETAL-NTDLL.txt').is_file()
            require(result['variant_marker'] == (name == 'wine-d3dmetal'), name + ' isolation marker')
            try:
                run([server, '-p'], env, evidence / 'persist.log', 30)
                boot = run([wine, 'wineboot', '-u'], env, evidence / 'wineboot.log', 240)
                result['tests']['wineboot'] = boot
                require(boot['exit_code'] == 0, name + ' wineboot')
                for label, exe, marker in [
                    ('syscall', 'syscall-probe.exe', 'PASS direct syscall on main and worker threads'),
                    ('teb', 'teb-probe.exe', 'PASS TEB mirrors, TLS growth, localtime, SEH and CreateThread'),
                    ('rosetta', 'rosetta-probe.exe', 'Runtime checks: 8, failures: 0'),
                ]:
                    command = [wine, tools / exe]
                    if name == 'wine-d3dmetal' and label == 'teb':
                        command += ['--variant']
                    test = run(command, env, evidence / (label + '.log'), 120)
                    content = (evidence / (label + '.log')).read_text(errors='replace')
                    test['passed'] = test['exit_code'] == 0 and marker in content
                    result['tests'][label] = test
                    require(test['passed'], name + ' ' + label)
                    if not test['passed']:
                        lines = content.splitlines()
                        highlights = [line for line in lines if re.search(
                            r'cannot protect|cannot restore|Unhandled |result=failed|^FAIL|:err:', line)]
                        print('\n'.join((highlights or lines[-30:])[:40]), flush=True)
                    save()
            finally:
                run([server, '-k'], env, evidence / 'kill.log', 30)
                run([server, '-w'], env, evidence / 'wait.log', 60)
            save()

        hashes = {name: data['ntdll_sha256'] for name, data in results['trees'].items()}
        require(hashes['wine'] == hashes['wine-dxmt'], 'normal Wine/DXMT pair remains identical')
        require(all(hashes['wine'][arch] != hashes['wine-d3dmetal'][arch] for arch in hashes['wine']),
                'both variant ntdll files differ from normal')

        # Prepare the launcher prefix, then sample the factory rather than a timed child spawn.
        home = out / 'graphics-home'
        logs = out / 'graphics-logs'
        env = dict(os.environ, GRYPHFIELD_HOME=str(home), GRYPHFIELD_LOGS=str(logs))
        cli = ['/bin/bash', engine / 'bin/gryphfield-engine']
        setup = run(cli + ['setup'], env, out / 'setup.log', 300)
        setup['ready'] = '"event":"done","ok":true' in (out / 'setup.log').read_text(errors='replace')
        results['graphics_setup'] = setup
        require(setup['ready'], 'graphics prefix setup')
        if not setup['ready']:
            raise RuntimeError('graphics prefix setup did not finish')
        shutil.copy2(engine / 'wine-dxmt/lib/wine/x86_64-windows/winemetal.dll',
                     home / 'prefix/drive_c/windows/system32/winemetal.dll')

        logs.mkdir(exist_ok=True)

        def spawn(label, mode, renderer=None):
            tree = engine / {'wined3d': 'wine', 'dxmt': 'wine-dxmt', 'd3dmetal': 'wine-d3dmetal'}[mode]
            runtime = environment(tree, home / 'prefix')
            if (renderer or mode) == 'wined3d':
                runtime['WINE_D3D_CONFIG'] = 'renderer=vulkan'
            else:
                runtime.pop('WINE_D3D_CONFIG', None)
            run([tree / 'bin/wineserver', '-p'], runtime, out / f'persist-{label}.log', 30)
            warmup = run([tree / 'bin/wine', 'cmd', '/c', 'exit', '0'], runtime,
                         out / f'warmup-{label}.log', 180)
            before = reports()
            log = out / f'spawn-{label}.log'
            sample_path = logs / f'{label}-{mode}-stacks.txt'
            test = run_factory_sample([tree / 'bin/wine', tools / 'spawn-probe.exe'], runtime, log, sample_path)
            content = log.read_text(errors='replace')
            sample = sample_path.read_text(errors='replace') if sample_path.exists() else ''
            test.update(warmup=warmup, fields=dict(re.findall(
                r'^(spawn_[a-z_]+|dxgi_loaded|factory|factory_hr|adapter|adapter_hr|adapter_desc_hr|result)=([^\s]+)',
                content, re.M)),
                gpu_unsupported='GPU not supported:' in content,
                nsalert='NSAlert' in sample,
                d3dmetal_thread_alive='D3DMetalWineThread' in sample,
                unhandled_fault=bool(re.search(r'Unhandled (?:page fault|exception)', content, re.I)),
                pthread_fault=bool(re.search(r'(?:Unhandled page fault|page fault on (?:read|write)|code=c0000005)',
                                            content, re.I)) and
                              ('libsystem_pthread' in content or 'pthread_setname_np' in content))
            test['passed_thread_init'] = (test['d3dmetal_thread_alive'] and not test['pthread_fault'] and
                                         not test['unhandled_fault'] and
                                         (test['gpu_unsupported'] or test['nsalert'] or 'factory_hr' in test['fields']))
            test['passed_spawn'] = test['exit_code'] == 0 and test['fields'].get('result') == 'ok'
            run(cli + ['kill'], env, out / f'kill-{label}.log', 30)
            time.sleep(8)
            crashes = out / ('crashes-' + label)
            crashes.mkdir()
            for path, stamp in reports().items():
                if before.get(path) != stamp:
                    shutil.copy2(path, crashes / path.name)
            print(f'{label}: {json.dumps(test)}', flush=True)
            return test

        try:
            for mode in ['wined3d', 'dxmt', 'd3dmetal']:
                test = results['spawn'][mode] = spawn(mode, mode)
                if mode == 'wined3d':
                    require(test['passed_spawn'], 'normal WineD3D spawn/factory/adapter')
                elif mode == 'd3dmetal':
                    require(test['passed_thread_init'], 'D3DMetal thread initialized, alive and no pthread fault')
                save()

            # Also exercise WineD3D and DXMT with the variant pair in the actual
            # wine-d3dmetal tree. Restore every graphics module afterwards.
            target = engine / 'wine-d3dmetal/lib/wine'
            backup = out / 'graphics-modules-original'
            backup.mkdir()
            replacements = [(arch, file) for arch, files in [
                ('x86_64-windows', ['d3d11.dll', 'dxgi.dll', 'd3d10core.dll', 'winemetal.dll']),
                ('x86_64-unix', ['d3d11.so', 'dxgi.so', 'd3d10core.so', 'winemetal.so'])] for file in files]
            present = []
            for arch, file in replacements:
                dest = target / arch / file
                if dest.exists() or dest.is_symlink():
                    (backup / arch).mkdir(exist_ok=True)
                    shutil.copy2(dest, backup / arch / file, follow_symlinks=False)
                    present.append((arch, file))
            try:
                for mode, tree in [('wined3d', 'wine'), ('dxmt', 'wine-dxmt')]:
                    for arch, file in replacements:
                        dest = target / arch / file
                        dest.unlink(missing_ok=True)
                        original = engine / tree / 'lib/wine' / arch / file
                        if original.exists():
                            shutil.copy2(original, dest)
                    test = results['variant_controls'][mode] = spawn('variant-' + mode, 'd3dmetal', renderer=mode)
                    if mode == 'wined3d':
                        require(test['passed_spawn'], 'variant WineD3D spawn/factory/adapter')
                    save()
            finally:
                for arch, file in replacements:
                    dest = target / arch / file
                    dest.unlink(missing_ok=True)
                    if (arch, file) in present:
                        shutil.copy2(backup / arch / file, dest, follow_symlinks=False)
        finally:
            run(cli + ['kill'], env, out / 'kill-final.log', 30)
        results['passed'] = not failures
    finally:
        results['failures'] = failures
        save()
        summary = '\n'.join(
            f'- {name}: ' + ', '.join(f'{test}={data.get("passed", data.get("exit_code") == 0)}'
                                      for test, data in tree['tests'].items())
            for name, tree in results['trees'].items())
        summary += '\n' + '\n'.join(f'- {mode}: {json.dumps(data)}' for mode, data in results['spawn'].items())
        (out / 'summary.txt').write_text(summary + '\n')
        with open(os.environ.get('GITHUB_STEP_SUMMARY', os.devnull), 'a') as stream:
            stream.write('Experimental native-TSD ntdll (CI/review only).\n\n' + summary + '\n')
    if not results['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
