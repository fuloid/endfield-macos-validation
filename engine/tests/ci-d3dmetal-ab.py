#!/usr/bin/env python3
"""Compare standalone D3DMetal thread initialization; graphics outcomes are informational."""
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
        process = subprocess.Popen(command, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return {'exit_code': process.wait(timeout=timeout), 'timed_out': False}
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            return {'exit_code': process.returncode, 'timed_out': True}


def reports():
    root = Path.home() / 'Library/Logs/DiagnosticReports'
    return {p: p.stat().st_mtime_ns for p in root.rglob('*') if p.is_file()}


def main():
    baseline, crossover, probe, out = [Path(p).resolve() for p in sys.argv[1:]]
    out.mkdir(parents=True, exist_ok=True)
    results = {}
    for label, engine in [('wine-11.19', baseline), ('crossover-26.3', crossover)]:
        evidence = out / label
        evidence.mkdir(exist_ok=True)
        shutil.copy2(engine / 'VERSION.json', evidence / 'VERSION.json')
        home = out.parent / (label + '-home')
        env = dict(os.environ, GRYPHFIELD_HOME=str(home), GRYPHFIELD_LOGS=str(evidence / 'logs'))
        cli = ['/bin/bash', str(engine / 'bin/gryphfield-engine')]
        setup = []
        for attempt in range(1, 4):
            log = evidence / f'setup-{attempt}.log'
            result = run(cli + ['setup'], env, log, 240)
            result['ready'] = '"event":"done","ok":true' in log.read_text(errors='replace')
            setup.append(result)
            if result['ready']:
                break
        results[label] = {'setup': setup, 'probes': {}}
        for mode in ['wined3d', 'dxmt', 'd3dmetal']:
            before = reports()
            log = evidence / f'spawn-{mode}.log'
            result = run(cli + ['_graphics-selftest', mode, str(probe), 'spawn'], env, log, 110)
            text = log.read_text(errors='replace')
            fields = dict(re.findall(r'^(spawn_[a-z_]+|dxgi_loaded|factory|factory_hr|adapter|adapter_hr|adapter_desc_hr|result)=([^\s]+)', text, re.M))
            result['fields'] = fields
            if result['timed_out'] or result['exit_code'] in (137, -9):
                result['outcome'] = 'timeout_or_killed'
            elif fields.get('result') == 'ok' and result['exit_code'] == 0:
                result['outcome'] = 'ok'
            elif fields.get('factory') == 'failed':
                result['outcome'] = 'factory_error'
            elif fields.get('adapter') == 'failed':
                result['outcome'] = 'adapter_error'
            else:
                result['outcome'] = 'incomplete_or_crashed'
            run(cli + ['kill'], env, evidence / f'kill-{mode}.log', 30)
            time.sleep(8)
            crash_dir = evidence / ('crashes-' + mode)
            crash_dir.mkdir(exist_ok=True)
            for path, stamp in reports().items():
                if before.get(path) != stamp:
                    shutil.copy2(path, crash_dir / path.name)
            results[label]['probes'][mode] = result
            (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(f'{label} {mode}: {json.dumps(result)}', flush=True)
    summary = '\n'.join(
        f'- {label} {mode}: {result["outcome"]}; exit={result["exit_code"]}; '
        + ' '.join(f'{key}={value}' for key, value in result['fields'].items())
        for label, tests in results.items() for mode, result in tests['probes'].items())
    (out / 'summary.txt').write_text(summary + '\n')
    with open(os.environ.get('GITHUB_STEP_SUMMARY', os.devnull), 'a') as stream:
        stream.write('D3DMetal A/B diagnostic (informational; no gameplay claim).\n\n' + summary + '\n')


if __name__ == '__main__':
    main()
