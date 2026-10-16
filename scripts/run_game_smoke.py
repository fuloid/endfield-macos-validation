#!/usr/bin/env python3
"""Launch the trimmed Endfield sample under the built Wine and record how far startup gets.

Only logs, counters and screenshots are written to the output directory; game files are never
copied there. Pass/fail is informational: the goal is to see whether ACE / the protector let the
game reach Unity engine initialization."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import threading
import time

LOG_HEAD_BYTES = 16 * 1024 * 1024
LOG_TAIL_LINES = 20000
MARKERS = {
    'bugcheck_wace_65636157': re.compile(r'KeBugCheckEx 65636157'),
    'bugcheck_tace_65636154': re.compile(r'KeBugCheckEx 65636154'),
    'bugcheck_any': re.compile(r'KeBugCheckEx'),
    'nop_skipped': re.compile(r'skipped \d+-byte NOP'),
    'xgetbv_emulated': re.compile(r'emulated xgetbv'),
    'exec_page_fault_retry': re.compile(r'HACK: (write|exec) fault on an executable page'),
    'unhandled_exception': re.compile(r'Unhandled (exception|page fault)'),
    'unimplemented_function': re.compile(r'unimplemented function'),
    'ace_driver_load': re.compile(r'ACE-(BASE|CORE)', re.I),
    'driver_load_failed': re.compile(r'ZwLoadDriver failed'),
    'lsass_start_failed': re.compile(r"Couldn't (start lsass|open SamSs)"),
}
UNITY_MARKERS = ['Initialize engine version', 'Direct3D:', 'GfxDevice', 'Begin MonoManager',
                 'il2cpp', 'UnloadTime', 'Crash!!!']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True, help='WORK_DIR of the Wine build')
    parser.add_argument('--game', type=Path, required=True, help='extracted sample root (contains GRYPHLINK/)')
    parser.add_argument('--timeout', type=int, default=420)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        raise SystemExit('macOS only')

    out = args.work / 'game-smoke'
    (out / 'screens').mkdir(parents=True, exist_ok=True)
    wine = args.work / 'wine-install/bin/wine'
    if not wine.is_file():
        wine = args.work / 'wine-install/bin/wine64'
    prefix = args.work / 'ci-prefix'
    game_dir = args.game / 'GRYPHLINK/games/Arknights Endfield'
    exe = game_dir / 'Endfield.exe'
    if not exe.is_file():
        raise SystemExit('Endfield.exe not found in the sample')

    env = dict(os.environ, WINEPREFIX=str(prefix), WINEARCH='win64', ROSETTA_ADVERTISE_AVX='1',
               WINEFSYNC='0', WINEESYNC='0', WINENTSYNC='0',
               WINEDLLOVERRIDES='mscoree,mshtml,winedbg.exe=',
               WINEDEBUG='+seh,+loaddll,+ntoskrnl,+process')
    quiet_env = dict(env, WINEDEBUG='-all')
    report = {'host': platform.platform(), 'timeout_seconds': args.timeout, 'markers': {},
              'snapshots': {}, 'unity': {}, 'scope': 'Startup only: ACE / protector / Unity init.'}

    def tasklist(name):
        try:
            r = subprocess.run(['arch', '-x86_64', str(wine), 'tasklist'], env=quiet_env,
                               capture_output=True, text=True, timeout=60)
            text = r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            text = 'tasklist timed out'
        (out / f'tasklist-{name}.txt').write_text(text)
        names = sorted({line.split()[0] for line in text.splitlines()
                        if line.lower().split()[:1] and line.split()[0].lower().endswith('.exe')})
        report['snapshots'][name] = names
        return names

    pre = tasklist('before-launch')
    report['lsass_running_before_launch'] = any(n.lower() == 'lsass.exe' for n in pre)

    counts = {k: 0 for k in MARKERS}
    first_hits = {}
    tail = deque(maxlen=LOG_TAIL_LINES)
    head = (out / 'wine-head.log').open('w', errors='replace')
    head_bytes = 0

    cmd = ['arch', '-x86_64', str(wine), str(exe), '-force-d3d11', '-screen-fullscreen', '0',
           '-screen-width', '1280', '-screen-height', '720']
    start = time.time()
    proc = subprocess.Popen(cmd, cwd=str(game_dir), env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')

    def reader():
        nonlocal head_bytes
        for line in proc.stdout:
            for key, pattern in MARKERS.items():
                if pattern.search(line):
                    counts[key] += 1
                    first_hits.setdefault(key, (round(time.time() - start, 1), line.strip()[:300]))
            if head_bytes < LOG_HEAD_BYTES:
                head.write(line)
                head_bytes += len(line)
            else:
                tail.append(line)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()

    shot = 0
    snapshot_taken = False
    while time.time() - start < args.timeout:
        time.sleep(15)
        elapsed = int(time.time() - start)
        subprocess.run(['screencapture', '-x', str(out / 'screens' / f't{elapsed:04d}.png')],
                       capture_output=True)
        shot += 1
        if not snapshot_taken and elapsed >= 90:
            tasklist('t090')
            snapshot_taken = True
        if proc.poll() is not None:
            break

    report['launcher_process_exit_code'] = proc.poll()
    report['seconds_running'] = round(time.time() - start, 1)
    tasklist('end')
    subprocess.run(['arch', '-x86_64', str(wine.parent / 'wineserver'), '-k'], env=quiet_env,
                   capture_output=True)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    thread.join(timeout=30)
    head.close()
    (out / 'wine-tail.log').write_text(''.join(tail))

    report['markers'] = {k: {'count': counts[k], 'first': first_hits.get(k)} for k in MARKERS}
    for player in prefix.glob('drive_c/users/*/AppData/LocalLow/Gryphline/*/Player*.log'):
        shutil.copyfile(player, out / player.name)
        text = player.read_text(errors='replace')
        report['unity'][player.name] = {m: (m in text) for m in UNITY_MARKERS}
    report['screenshots'] = shot
    (out / 'game.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('lsass_running_before_launch', 'seconds_running',
                                              'launcher_process_exit_code', 'unity')}, indent=2))
    for k, v in report['markers'].items():
        print(f'{k}: {v["count"]}' + (f'  first @ {v["first"][0]}s: {v["first"][1]}' if v['first'] else ''))


if __name__ == '__main__':
    main()
