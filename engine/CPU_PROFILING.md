# Manual CPU capture

UI: Settings > CPU profiling > Capture CPU profile. Start/Cancel confirmation includes possible macOS access restriction. Start dismisses the dialog, the engine emits a five-second preparation countdown, then runs /usr/bin/sample for ten seconds at a 10 ms interval. Status is displayed without opening a modal over the game. No always-on recording.

Protocol: `profile-cpu`; progress events use `event: profile`, `state`, `message`, optional `log` (metadata.json path). Terminal `done.ok` is authoritative; nonzero sampler status, missing call graph, target exit, or unavailable target must not be reported as success. Capture runs under a separate UI process key, independent of the launch session.

Folder: ~/Library/Logs/Gryphfield/profiles/cpu-<UTC>-<random>/
- metadata.json: schema_version 1, native PID, UTC start, configured duration/interval, engine versions, last launch mode, macOS and hardware model.
- stacks.txt: Apple's sampler report, retained verbatim.
- sampler.txt: sampler stdout/stderr, including access failures.
- resources.tsv: UTC time, PID, ps CPU percentage, RSS KiB, cumulative CPU time each second. ps CPU percentage is a lifetime average, NOT a measured one-second CPU delta.
- result.json: sampler exit status (null if never started), whether the original target still exists, and status (`complete`, `failed`, or `cancelled`). Written on preparation failure and cancellation too.
- README.txt: interpretation notes.

Sampling includes waiting stacks. Counts are not equivalent to CPU percentages. Rosetta frames may lack symbols. No FPS, per-frame, or GPU timings are collected. Sampler output is file-size limited and runtime bounded to 45 seconds including symbolication. One supervisor owns and reaps its sampler and one-second sleep; there are no independent ticker/watchdog shells. Report folders are mode 700 and files are private (umask 077). SIGINT/SIGTERM produce PROFILE_CANCELLED and never success. SIGKILL cannot run cleanup. Reports are separate from rotating Wine logs and retained for manual sharing.

The per-prefix directory lock is deliberately not automatically stolen: stale-owner detection can race PID reuse or owner-file creation. If a crash/SIGKILL leaves cpu-profile.lock, quit all Gryphfield engine processes before manually removing the lock directory. Native target selection requires the current UID, an Endfield executable identity/command, a process that maps this engine's `ntdll.so` (device and inode compared, among lsof's `txt` entries; patch 0018 may reach it through a link named after the exe, and Wine's renaming wipes the environment that ps would show, so `WINEPREFIX` cannot be read), and one unique native PID. Gryphfield has one prefix per engine, so the loader ties the process to it. It fails closed if macOS cannot expose these fields.

Release acceptance: exercise on macOS with a harmless Wine-hosted executable named Endfield.exe in the test prefix (no game needed); verify native PID resolution, five-second preparation, ten-second sample, valid nonempty stack report, folder contents, denied access/error handling, no-game, duplicate capture and game-exits cases. Check Native SDK UI contracts and build; inspect confirmation/status UI. Actual Endfield profiling remains tester validation, including impact on gameplay.

CI acceptance (after assembly; never ship the fixture):

```bash
mkdir -p "$RUNNER_TEMP/test-tools"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror engine/tests/profile-fixture.c \
  -o "$RUNNER_TEMP/test-tools/Endfield.exe"
bash engine/tests/profile-target.sh "$RUNNER_TEMP/engine/bin/gryphfield-profile"
/usr/bin/python3 -I engine/tests/ci-profile.py "$RUNNER_TEMP/engine" \
  "$RUNNER_TEMP/cpu-profile-tests" "$RUNNER_TEMP/test-tools/Endfield.exe"
```

The output directory must be new. The harness initializes a disposable prefix under a path containing spaces using only bundled Wine, launches only the harmless fixture, and retains JSONL/stderr/report evidence. It requires real sample success (not a skip if access is denied), checks at least 14 seconds elapsed and a call graph, then exercises no target, duplicate targets, concurrent capture, target exits during prep/capture, failed/empty/invalid sampler output, timeout, SIGINT/SIGTERM in prep/capture with descendant reaping, stale locks, and failed report creation. Failure injection uses only sourced helper function overrides in generated harness drivers, never CLI/environment bypasses in the engine. Upload the entire cpu-profile-tests directory as CI evidence, including failures. This does not establish real-game/ACE or UI acceptance.
