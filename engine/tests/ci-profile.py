#!/usr/bin/env python3
"""macOS acceptance: assembled engine + disposable Wine prefix, never a real game.
python3 engine/tests/ci-profile.py ENGINE OUT TEST_TOOLS/Endfield.exe
OUT must not exist. Evidence is retained there, including private partial reports.
"""
import json
import os
from pathlib import Path
import signal
import subprocess as sp
import sys
import time

engine, out, fixture = (Path(v).resolve() for v in sys.argv[1:])
assert sys.platform == "darwin", "requires macOS and real /usr/bin/sample"
assert fixture.name == "Endfield.exe" and fixture.is_file()
out.mkdir(parents=True, exist_ok=False)
home = out / "home with spaces"
logs = out / "logs"
home.mkdir()
env = dict(os.environ, GRYPHFIELD_HOME=str(home), GRYPHFIELD_LOGS=str(logs),
           WINEPREFIX=str(home / "prefix"), WINEDEBUG="-all", WINEARCH="win64",
           WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d",
           DYLD_FALLBACK_LIBRARY_PATH=f"{engine}/wine/lib/external:/usr/lib")
cli = ["/bin/bash", str(engine / "bin/gryphfield-engine"), "profile-cpu"]
helper = engine / "bin/gryphfield-profile"
assert helper.is_file(), "assembler must ship the helper"
# Fault injection exists only in this harness. Reuse assembled protocol definitions and helper.
def driver(name, override):
    source = (engine / "bin/gryphfield-engine").read_text().split("\nresolve_library\n")[0]
    path = out / (name + "-driver.sh")
    path.write_text(source + '\nENGINE="$1"\nsource "$ENGINE/bin/gryphfield-profile"\n' + override + '\ncmd_profile_cpu\n')
    return ["/bin/bash", str(path), str(engine)]

handles = []
children = []
def start(name, command=cli):
    f = open(out / (name + ".jsonl"), "w")
    e = open(out / (name + ".err"), "w")
    handles.extend([f, e])
    p = sp.Popen(command, env=env, stdout=f, stderr=e, start_new_session=True)
    children.append(p)
    return p

def events(name):
    return [json.loads(line) for line in (out / (name + ".jsonl")).read_text().split("\n")[:-1]]

def until(predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.1)
    raise AssertionError("condition timed out")

def state(name, value):
    until(lambda: any(e.get("state") == value for e in events(name)))

def terminal(name, p, code=None):
    rc = p.wait(timeout=55)
    es = events(name)
    done = [e for e in es if e.get("event") == "done"]
    assert len(done) == 1, (name, es)
    if code:
        assert rc != 0 and done[0]["ok"] is False, (name, rc, es)
        assert done[0]["error"]["code"] == code, (name, es)
        assert not any(e.get("state") in ("complete", "saved") for e in es), (name, es)
    else:
        assert rc == 0 and done[0]["ok"] is True, (name, rc, es)
    print("PASS", name, flush=True)

def target():
    p = sp.run(["/bin/bash", "-c", 'PREFIX="$1"; ENGINE="$3"; source "$2"; profile_target',
                "target", env["WINEPREFIX"], str(helper), str(engine)], env=env, text=True, capture_output=True)
    return p.stdout if p.returncode == 0 else ""

def launch(name):
    f = open(out / (name + "-fixture.log"), "w")
    handles.append(f)
    p = sp.Popen([str(engine / "wine/bin/wine"), str(fixture)], env=env,
                 stdout=f, stderr=f, start_new_session=True)
    children.append(p)
    until(lambda: "profile fixture ready" in (out / (name + "-fixture.log")).read_text(), 60)
    return p

def stop_fixture():
    pid = target()
    if pid:
        os.kill(int(pid), signal.SIGKILL)
    until(lambda: not target())

def reports():
    return set((logs / "profiles").glob("cpu-*"))

def latest_result(before, status):
    added = reports() - before
    assert len(added) == 1, added
    folder = added.pop()
    result = json.loads((folder / "result.json").read_text())
    assert result["status"] == status, result
    json.loads((folder / "metadata.json").read_text())
    assert folder.stat().st_mode & 0o077 == 0
    for f in folder.iterdir():
        assert f.stat().st_mode & 0o077 == 0, f
    assert not (home / "cpu-profile.lock").exists()
    return folder, result

def descendants(pid):
    rows = sp.check_output(["/bin/ps", "-axo", "pid=,ppid="], text=True)
    pairs = [tuple(map(int, line.split())) for line in rows.splitlines()]
    found = {pid}
    while True:
        more = {p for p, parent in pairs if parent in found}
        if more <= found:
            return found - {pid}
        found |= more

def dead(pid):
    try:
        os.kill(pid, 0)
        return False
    except ProcessLookupError:
        return True

try:
    # No downloads/setup or GRYPHLINK: initialize only a disposable prefix with bundled Wine.
    sp.run([str(engine / "wine/bin/wine"), "wineboot", "-u"], env=env,
           stdout=open(out / "wineboot.log", "w"), stderr=sp.STDOUT, check=True, timeout=600)
    terminal("no-game", start("no-game"), "PROFILE_TARGET")
    launch("success")
    try:
        until(lambda: bool(target()))
    except AssertionError:
        # Evidence for the report: what macOS actually shows for the fixture process.
        with open(out / "target-diagnostics.txt", "w") as d:
            sp.run(["/usr/bin/pgrep", "-u", str(os.getuid()), "-if", r"Endfield\.exe"], stdout=d, stderr=sp.STDOUT)
            sp.run(["/bin/ps", "-axww", "-o", "pid,ppid,uid,comm,args"], stdout=d, stderr=sp.STDOUT)
            for pid in sp.run(["/usr/bin/pgrep", "-u", str(os.getuid()), "-if", r"Endfield\.exe"],
                              capture_output=True, text=True).stdout.split():
                d.write("\n--- ps eww %s\n" % pid)
                d.flush()
                sp.run(["/bin/ps", "eww", "-p", pid, "-o", "command="], stdout=d, stderr=sp.STDOUT)
        raise
    native_pid = int(target())
    assert sp.check_output(["/bin/ps", "-p", str(native_pid), "-o", "uid="], text=True).strip() == str(os.getuid())
    before = reports()
    began = time.monotonic()
    terminal("real-sample", start("real-sample"))
    assert time.monotonic() - began >= 14, "must include five-second prep and real ten-second sample"
    folder, result = latest_result(before, "complete")
    assert result["sampler_exit_code"] == 0 and result["target_still_running"] is True
    assert "Call graph:" in (folder / "stacks.txt").read_text()
    assert len((folder / "resources.tsv").read_text().splitlines()) >= 9
    assert json.loads((folder / "metadata.json").read_text())["pid"] == native_pid

    # Ambiguous native targets in the same prefix must never select either one.
    launch("duplicate-target")
    until(lambda: not target())
    terminal("duplicate-target", start("duplicate-target"), "PROFILE_TARGET")
    sp.run([str(engine / "wine/bin/wineserver"), "-k"], env=env, check=True, timeout=15)
    sp.run([str(engine / "wine/bin/wineserver"), "-w"], env=env, check=True, timeout=30)

    for phase in ("preparing", "sampling"):
        launch("exit-" + phase)
        until(lambda: bool(target()))
        before = reports()
        name = "exit-" + phase
        p = start(name)
        state(name, phase)
        stop_fixture()
        terminal(name, p, "PROFILE_EXITED")
        latest_result(before, "failed")

    launch("errors")
    until(lambda: bool(target()))
    for name, override, expected in (
        ("sampler-failure", 'profile_sample() { echo "injected access denied" >&2; return 77; }', 77),
        ("missing-stacks", 'profile_sample() { return 0; }', 0),
        ("invalid-stacks", 'profile_sample() { echo invalid > "$report/stacks.txt"; }', 0),
    ):
        before = reports()
        terminal(name, start(name, driver(name, override)), "PROFILE_FAILED")
        folder, result = latest_result(before, "failed")
        assert result["sampler_exit_code"] == expected

    # Bounded timeout without waiting 45 seconds; only harness overrides the clock pause/sampler.
    before = reports()
    name = "timeout"
    p = start(name, driver(name, 'profile_sample() { exec /bin/sleep 90; }\nprofile_pause() { sleep .02 & pause=$!; wait "$pause"; pause=""; }'))
    terminal(name, p, "PROFILE_TIMEOUT")
    latest_result(before, "failed")

    for phase in ("preparing", "sampling"):
        for sig in (signal.SIGTERM, signal.SIGINT):
            before = reports()
            name = "cancel-" + phase + "-" + sig.name
            p = start(name)
            state(name, phase)
            if phase == "sampling":
                terminal(name + "-busy", start(name + "-busy"), "PROFILE_BUSY")
            time.sleep(.2)
            owned = descendants(p.pid)
            assert owned, "must observe active child before cancellation"
            os.kill(p.pid, sig)
            terminal(name, p, "PROFILE_CANCELLED")
            until(lambda: all(dead(pid) for pid in owned), 5)
            latest_result(before, "cancelled")

    # Conservative stale-lock policy: never silently steal even a dead owner's lock.
    lock = home / "cpu-profile.lock"
    lock.mkdir()
    (lock / "owner").write_text("99999999\n")
    terminal("stale-lock", start("stale-lock"), "PROFILE_BUSY")
    (lock / "owner").unlink()
    lock.rmdir()
    # An unwritable destination (a regular file) fails cleanly and releases its lock.
    bad_logs = out / "not-a-directory"
    bad_logs.write_text("blocked")
    env["GRYPHFIELD_LOGS"] = str(bad_logs)
    terminal("save-failure", start("save-failure"), "PROFILE_SAVE")
    assert not lock.exists()
    print("CPU profiling acceptance passed; evidence:", out)
finally:
    for p in children:
        if p.poll() is None:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            p.wait(timeout=5)
        except sp.TimeoutExpired:
            pass
    sp.run([str(engine / "wine/bin/wineserver"), "-k"], env=env, timeout=15)
    for f in handles:
        f.close()
