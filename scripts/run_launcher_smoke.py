#!/usr/bin/env python3
"""Launch the Endfield sample the real way (Launcher.exe -> Games.exe -> Play -> Endfield.exe) under
the built Wine, clicking the launcher UI from inside the prefix with launcher_click.exe.

Success = UnityCrashHandler64.exe appears and stays alive for at least 30 s (Unity engine started).
This script never fails the job: every outcome, including errors in this script, is written to
game-smoke/summary.md (and the GitHub step summary) with a probable reason, then it exits 0.
Only logs, process snapshots and screenshots are kept; game files are never copied out."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import threading
import time
import traceback

MIN_FREE_MEMORY = 3 * 1024 ** 3
CRASH_HANDLER = 'unitycrashhandler64.exe'
REQUIRED_ALIVE = 30
LOG_HEAD_BYTES = 16 * 1024 * 1024
LOG_TAIL_LINES = 20000
# fractions of the Games.exe window rect (incl. shadow margin), calibrated on Windows
TARGETS = {'events': (0.408, 0.740), 'notices': (0.357, 0.740),
           'dx11_checkbox': (0.758, 0.926), 'play': (0.834, 0.867)}
MARKERS = {
    'bugcheck_wace_65636157': r'KeBugCheckEx 65636157',
    'bugcheck_tace_65636154': r'KeBugCheckEx 65636154',
    'bugcheck_any': r'KeBugCheckEx',
    'nop_skipped': r'skipped \d+-byte NOP',
    'xgetbv_emulated': r'emulated xgetbv',
    'exec_page_fault_retry': r'HACK: (write|exec) fault on an executable page',
    'unhandled_exception': r'Unhandled (exception|page fault)',
    'unimplemented_function': r'unimplemented function',
    'driver_load_failed': r'ZwLoadDriver failed',
    'lsass_start_failed': r"Couldn't (start lsass|open SamSs)",
    'qtwebengine': r'QtWebEngineProcess',
    'no_opengl': r'Failed to (create internal root context|get a GL context)',
    'vulkan_error': r'err:vulkan|vkCreateInstance failed|Failed to create Vulkan',
}
MARKERS = {k: re.compile(v) for k, v in MARKERS.items()}
UNITY_MARKERS = ['Initialize engine version', 'Direct3D:', 'GfxDevice', 'il2cpp', 'Crash!!!']


def catbox(path):
    """Upload a screenshot to catbox.moe (public, anonymous) and return its URL, or None."""
    try:
        r = subprocess.run(['curl', '-fsS', '--max-time', '60', '-F', 'reqtype=fileupload',
                            '-F', f'fileToUpload=@{path}', 'https://catbox.moe/user/api.php'],
                           capture_output=True, text=True, timeout=90)
        url = r.stdout.strip()
        return url if r.returncode == 0 and url.startswith('https://') else None
    except Exception:
        return None


def free_memory():
    """Free + inactive + speculative + purgeable pages, in bytes (memory macOS can hand out)."""
    text = subprocess.run(['vm_stat'], capture_output=True, text=True).stdout
    page = int(re.search(r'page size of (\d+)', text).group(1))
    pages = 0
    for key in ('Pages free', 'Pages inactive', 'Pages speculative', 'Pages purgeable'):
        m = re.search(key + r':\s+(\d+)', text)
        if m:
            pages += int(m.group(1))
    return pages * page, text


class Run:
    def __init__(self, args):
        self.args = args
        self.out = args.work / 'game-smoke'
        (self.out / 'screens').mkdir(parents=True, exist_ok=True)
        self.wine = args.wine / 'bin/wine'
        if not self.wine.is_file():
            self.wine = args.wine / 'bin/wine64'
        self.prefix = args.work / 'game-prefix'
        self.root = args.game / 'GRYPHLINK'
        self.env = dict(os.environ, WINEPREFIX=str(self.prefix), WINEARCH='win64',
                        ROSETTA_ADVERTISE_AVX='1', WINEFSYNC='0', WINEESYNC='0', WINENTSYNC='0',
                        WINEDLLOVERRIDES='mscoree,mshtml,winedbg.exe=',
                        WINEDEBUG='+seh,+loaddll,+ntoskrnl,+process')
        self.quiet = dict(self.env, WINEDEBUG='-all')
        self.report = {'host': platform.platform(), 'steps': [], 'markers': {}, 'unity': {},
                       'clicks': {}, 'verdict': 'unknown', 'probable_reason': None}
        self.start = time.time()
        self.counts = {k: 0 for k in MARKERS}
        self.first = {}
        self.tail = deque(maxlen=LOG_TAIL_LINES)
        self.head = None
        self.head_bytes = 0
        self.launcher = None

    def t(self):
        return round(time.time() - self.start, 1)

    def note(self, text):
        print(f'[{self.t():7.1f}s] {text}', flush=True)
        self.report['steps'].append(f'{self.t()}s {text}')

    def shot(self, name):
        path = self.out / 'screens' / f'{int(self.t()):04d}-{name}.png'
        subprocess.run(['screencapture', '-x', str(path)], capture_output=True)
        self.report.setdefault('screens', {})[name] = str(path)
        return path

    def wine_run(self, args, timeout=120, quiet=True):
        try:
            r = subprocess.run(['arch', '-x86_64', str(self.wine), *map(str, args)],
                               env=self.quiet if quiet else self.env, capture_output=True,
                               text=True, errors='replace', timeout=timeout)
            return r.returncode, r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            return None, f'timed out after {timeout}s'

    def tasks(self, name=None):
        code, text = self.wine_run(['tasklist'], timeout=60)
        if name:
            (self.out / f'tasklist-{name}.txt').write_text(text)
        return {line.split()[0].lower() for line in text.splitlines()
                if line.split() and line.split()[0].lower().endswith('.exe')}

    def reader(self):
        for line in self.launcher.stdout:
            for key, pattern in MARKERS.items():
                if pattern.search(line):
                    self.counts[key] += 1
                    self.first.setdefault(key, (self.t(), line.strip()[:300]))
            if self.head_bytes < LOG_HEAD_BYTES:
                self.head.write(line)
                self.head_bytes += len(line)
            else:
                self.tail.append(line)

    def clicker(self):
        # prefer the clicker built from this repo's source (has 'fit'); fall back to the sample's copy
        if self.args.clicker and self.args.clicker.is_file():
            return self.args.clicker
        return self.root / 'launcher_click.exe'

    def launcher_window(self, clicker):
        code, text = self.wine_run([clicker, 'list'], timeout=60)
        best = None
        for line in text.splitlines():
            if 'exe=Games.exe' not in line:
                continue
            m = re.search(r'rect=(-?\d+),(-?\d+) (\d+)x(\d+)', line)
            if m and int(m.group(3)) >= 400 and int(m.group(4)) >= 250:
                best = line.strip()
        return best, text

    def click(self, clicker, name):
        rx, ry = TARGETS[name]
        code, text = self.wine_run([clicker, 'click', 'Games.exe', rx, ry], timeout=60)
        self.report['clicks'][name] = {'exit_code': code, 'output': text.strip()[-500:]}
        self.note(f'click {name} -> exit {code}: {text.strip()[-200:]}')
        time.sleep(3)
        self.shot(f'after-{name}')
        return code == 0

    def execute(self):
        args = self.args
        mem, vm = free_memory()
        (self.out / 'vm_stat-start.txt').write_text(vm)
        self.report['free_memory_gb_at_start'] = round(mem / 1024 ** 3, 2)
        self.note(f'available memory {mem / 1024 ** 3:.2f} GB (need >= 3 GB for launcher + game)')
        if mem < MIN_FREE_MEMORY:
            self.report['verdict'] = 'skipped'
            self.report['probable_reason'] = (f'only {mem / 1024 ** 3:.2f} GB memory available on the '
                                              'runner; launcher + game need at least 3 GB')
            return
        if not (self.root / 'Launcher.exe').is_file():
            self.report['verdict'] = 'not run'
            self.report['probable_reason'] = 'sample archive has no GRYPHLINK/Launcher.exe'
            return

        code, text = self.wine_run(['wineboot', '-u'], timeout=600)
        (self.out / 'wineboot.log').write_text(text)
        self.note(f'wineboot -u exit {code}')
        before = self.tasks('before-launch')
        self.report['lsass_running_before_launch'] = 'lsass.exe' in before
        self.note(f'lsass.exe running before launch: {"lsass.exe" in before}')

        clicker = self.clicker()
        self.report['clicker'] = str(clicker)
        self.head = (self.out / 'wine-head.log').open('w', errors='replace')

        # Environment-only graphics fallbacks (no launcher files touched). CI VMs have no usable
        # OpenGL for Wine, which breaks wined3d and therefore Qt's ANGLE renderer.
        attempts = [('default', {}),
                    ('wined3d-vulkan', {'WINE_D3D_CONFIG': 'renderer=vulkan'}),
                    ('wined3d-vulkan+chromium-software',
                     {'WINE_D3D_CONFIG': 'renderer=vulkan',
                      'QTWEBENGINE_CHROMIUM_FLAGS': '--disable-gpu --disable-gpu-compositing'})]
        window = None
        self.report['attempts'] = {}
        for name, extra in attempts:
            self.env.update(extra)
            self.quiet.update(extra)
            self.head.write(f'\n===== launcher attempt: {name} {extra} =====\n')
            self.launcher = subprocess.Popen(['arch', '-x86_64', str(self.wine), str(self.root / 'Launcher.exe')],
                                             cwd=str(self.root), env=self.env, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True, errors='replace')
            threading.Thread(target=self.reader, daemon=True).start()
            self.note(f'started GRYPHLINK/Launcher.exe (attempt {name})')
            crashes_before = self.counts['unhandled_exception']
            deadline = self.t() + args.window_timeout
            while self.t() < deadline:
                time.sleep(10)
                self.shot(f'waiting-window-{name}')
                window, listing = self.launcher_window(clicker)
                if window:
                    (self.out / f'windows-at-launcher-{name}.txt').write_text(listing)
                    break
            crashed = self.counts['unhandled_exception'] - crashes_before
            self.report['attempts'][name] = {'window': window, 'unhandled_exceptions': crashed}
            if window:
                self.report['graphics_mode'] = name
                break
            names = self.tasks(f'no-window-{name}')
            self.note(f'attempt {name}: no launcher window, {crashed} unhandled exception(s), '
                      f'running: {", ".join(sorted(names))}')
            self.wine_run([str(self.wine.parent / 'wineserver'), '-k'], timeout=60)
            try:
                self.launcher.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.launcher.kill()
            time.sleep(3)
        if not window:
            gl = 'wgl/OpenGL context creation failed' if self.counts['no_opengl'] else ''
            self.report['verdict'] = 'launcher failed'
            self.report['probable_reason'] = (
                f'no Games.exe launcher window in any graphics mode ({", ".join(n for n, _ in attempts)}). '
                + (f'{gl} in this VM, so Direct3D (and Qt ANGLE) had no backend; ' if gl else '')
                + 'the launcher crashed (see unhandled exceptions per attempt in game.json and '
                'wine-head.log) or is stuck in an update/integrity step. CI-VM graphics limits are the '
                'likely cause; a real Apple Silicon Mac has working OpenGL/Metal.')
            return
        self.report['launcher_window'] = window
        self.note(f'launcher window: {window}')
        time.sleep(args.ui_settle)
        # the launcher does not shrink below 1312x754; move it (like dragging) so Play is on screen
        code, text = self.wine_run([clicker, 'fit', 'Games.exe'], timeout=60)
        self.report['fit'] = {'exit_code': code, 'output': text.strip()[-300:]}
        self.note(f'fit Games.exe -> exit {code}: {text.strip()[-160:]}')
        time.sleep(3)
        self.shot('launcher-ready')

        self.click(clicker, 'events')      # harmless check that clicks register in Wine
        self.click(clicker, 'notices')
        self.click(clicker, 'dx11_checkbox')
        self.click(clicker, 'play')
        self.shot('after-play')

        seen_game = seen_handler = None
        handler_since = None
        game_gone_after = None
        deadline = self.t() + args.game_timeout
        while self.t() < deadline:
            time.sleep(5)
            names = self.tasks()
            now = self.t()
            if 'endfield.exe' in names and seen_game is None:
                seen_game = now
                self.note('Endfield.exe started')
                self.shot('endfield-started')
            if seen_game is not None and 'endfield.exe' not in names and game_gone_after is None:
                game_gone_after = now
                self.note('Endfield.exe exited')
            if CRASH_HANDLER in names:
                if handler_since is None:
                    handler_since = now
                    seen_handler = seen_handler or now
                    self.note('UnityCrashHandler64.exe started (Unity engine up)')
                    self.shot('unity-started')
                elif now - handler_since >= REQUIRED_ALIVE:
                    self.note(f'UnityCrashHandler64.exe alive for {now - handler_since:.0f}s')
                    self.shot('unity-30s')
                    self.tasks('unity-30s')
                    self.report['verdict'] = 'pass'
                    break
            elif handler_since is not None:
                self.note(f'UnityCrashHandler64.exe exited after {now - handler_since:.0f}s')
                handler_since = None
            if int(now) % 30 < 5:
                self.shot('monitor')
        self.tasks('end')
        self.report.update(endfield_started_at=seen_game, crash_handler_started_at=seen_handler,
                           endfield_exited_at=game_gone_after)
        if self.report['verdict'] == 'pass':
            return
        self.report['verdict'] = 'game did not reach Unity' if seen_game else 'game did not start'
        reasons = []
        if not seen_game:
            reasons.append('Endfield.exe never appeared after clicking Play: the click may not have '
                           'registered (see screens/after-*.png and clicks in game.json), the launcher '
                           'may have asked to log in / update / repair the trimmed install, or it '
                           'refused to start the game')
        if self.counts['bugcheck_wace_65636157']:
            reasons.append('ACE-CORE entry check failed (KeBugCheckEx 65636157)')
        if self.counts['unhandled_exception']:
            reasons.append(f'unhandled exception: {self.first["unhandled_exception"][1]}')
        if self.counts['unimplemented_function']:
            reasons.append(f'missing Wine function: {self.first["unimplemented_function"][1]}')
        if seen_handler:
            reasons.append('UnityCrashHandler64.exe started but did not stay alive 30 s')
        elif seen_game:
            reasons.append('Endfield.exe started but Unity never started its crash handler '
                           '(protector / ACE / early init; see Player.log and wine logs)')
        self.report['probable_reason'] = '; '.join(reasons) or 'unknown; see logs and screenshots'

    def finish(self):
        if self.launcher:
            self.wine_run([str(self.wine.parent / 'wineserver'), '-k'], timeout=60)
            try:
                self.launcher.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.launcher.kill()
        if self.head:
            time.sleep(2)
            self.head.close()
        (self.out / 'wine-tail.log').write_text(''.join(self.tail))
        self.report['markers'] = {k: {'count': self.counts[k], 'first': self.first.get(k)}
                                  for k in MARKERS}
        for player in self.prefix.glob('drive_c/users/*/AppData/LocalLow/Gryphline/*/Player*.log'):
            text = player.read_text(errors='replace')
            (self.out / player.name).write_text(text)
            self.report['unity'][player.name] = {m: (m in text) for m in UNITY_MARKERS}
        self.report['seconds'] = self.t()
        (self.out / 'game.json').write_text(json.dumps(self.report, indent=2) + '\n')

        icon = {'pass': '✅'}.get(self.report['verdict'], '⚠️')
        lines = [f'## Endfield launcher test: {icon} {self.report["verdict"]}', '']
        if self.report['probable_reason']:
            lines += [f'**Probable reason:** {self.report["probable_reason"]}', '']
        lines += [f'- free memory at start: {self.report.get("free_memory_gb_at_start")} GB',
                  f'- lsass.exe running before launch: {self.report.get("lsass_running_before_launch")}',
                  f'- launcher window: `{self.report.get("launcher_window")}`',
                  f'- Endfield.exe started at: {self.report.get("endfield_started_at")} s',
                  f'- UnityCrashHandler64.exe started at: {self.report.get("crash_handler_started_at")} s',
                  '', '| marker | count | first |', '|---|---|---|']
        for k, v in self.report['markers'].items():
            first = (v['first'][1][:120].replace('|', '/') if v['first'] else '')
            lines.append(f'| {k} | {v["count"]} | {first} |')
        if os.environ.get('CATBOX_SCREENSHOTS') == '1':
            screens = self.report.get('screens', {})
            later = next((k for k in ('unity-30s', 'unity-started', 'endfield-started', 'after-play')
                          if k in screens), None)
            picks = [('Launcher ready (after fit)', 'launcher-ready')]
            if later:
                picks.append(({'unity-30s': 'Unity running 30 s (UnityCrashHandler64 alive)'}
                              .get(later, f'Latest state: {later}'), later))
            elif screens:
                picks.append(('Latest screenshot', list(screens)[-1]))
            images = {}
            for title, key in picks:
                if key in screens and Path(screens[key]).is_file():
                    url = catbox(screens[key])
                    images[key] = url
                    lines += ['', f'### {title}', f'![{key}]({url})' if url else '_(catbox upload failed)_']
            self.report['catbox'] = images
        lines += ['', 'This job never fails on launcher/game problems; see the endfield-game-smoke artifact.']
        summary = '\n'.join(lines) + '\n'
        (self.out / 'summary.md').write_text(summary)
        (self.out / 'game.json').write_text(json.dumps(self.report, indent=2) + '\n')
        print(summary)
        if os.environ.get('GITHUB_STEP_SUMMARY'):
            with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as f:
                f.write(summary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--wine', type=Path, required=True, help='installed Wine prefix (wine-install)')
    parser.add_argument('--game', type=Path, required=True, help='extracted sample root (contains GRYPHLINK/)')
    parser.add_argument('--clicker', type=Path, help='fallback launcher_click.exe if the sample has none')
    parser.add_argument('--window-timeout', type=int, default=240)
    parser.add_argument('--ui-settle', type=int, default=25)
    parser.add_argument('--game-timeout', type=int, default=480)
    args = parser.parse_args()
    run = Run(args)
    try:
        run.execute()
    except Exception:
        run.report['verdict'] = 'test error'
        run.report['probable_reason'] = 'the test script itself failed: ' + traceback.format_exc()[-600:]
    finally:
        try:
            run.finish()
        except Exception:
            traceback.print_exc()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
