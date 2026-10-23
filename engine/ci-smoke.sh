#!/usr/bin/env bash
# CI smoke test of an assembled engine on the GitHub macOS VM (no game files are available there):
# status -> setup -> run-gryphlink until GRYPHLINK starts, in wined3d mode (required) and DXMT
# mode (informational; the VM's paravirtual Metal may not support it), then the missing-game path.
#   bash engine/ci-smoke.sh <engine-dir> <output-dir> [test-tools-dir with discord-pipe-probe.exe]
set -uo pipefail
ENGINE="${1:?engine dir}"
OUT="${2:?output dir}"
TEST_TOOLS="${3:-}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
export GRYPHFIELD_HOME="$OUT/home" GRYPHFIELD_LOGS="$OUT/logs"
E="$ENGINE/bin/gryphfield-engine"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
failures=0

note() { echo "$*"; echo "$*" >> "$SUMMARY"; }
check() {   # description command...
    if "${@:2}"; then note "- PASS $1"; else note "- FAIL $1"; failures=$((failures + 1)); fi
}
done_ok() { grep -q '"event":"done","ok":true' "$1"; }

note '## Gryphfield engine smoke test'
/bin/bash "$E" status > "$OUT/status-1.jsonl" 2> "$OUT/status-1.err"
cat "$OUT/status-1.jsonl"
check 'status reports and finishes' done_ok "$OUT/status-1.jsonl"
check 'status sees Rosetta' grep -q '"rosetta":true' "$OUT/status-1.jsonl"

/bin/bash "$E" setup > "$OUT/setup.jsonl" 2> "$OUT/setup.err"
cat "$OUT/setup.jsonl"
check 'setup finishes' done_ok "$OUT/setup.jsonl"
/bin/bash "$E" status > "$OUT/status-2.jsonl" 2>> "$OUT/status-1.err"
check 'prefix ready after setup' grep -q '"ready":true' "$OUT/status-2.jsonl"
check 'GRYPHLINK installed after setup' grep -q '"installed":true,"version":"1\.' "$OUT/status-2.jsonl"
check 'lsass.exe runs in the prefix' bash -c "grep -rqi lsass '$OUT/logs/setup.log' || \
    (export WINEPREFIX='$OUT/home/prefix' DYLD_FALLBACK_LIBRARY_PATH='$ENGINE/wine/lib/external:/usr/lib'; \
     '$ENGINE/wine/bin/wine' tasklist 2>/dev/null | grep -qi lsass)"

launch() {   # mode required(1/0) [label]
    local mode=$1 required=$2 label="${3:-$1}" pid waited=0 started=0
    local events="$OUT/run-$label.jsonl"
    /bin/bash "$E" run-gryphlink --graphics "$mode" > "$events" 2> "$OUT/run-$label.err" &
    pid=$!
    while [ $waited -lt 300 ] && kill -0 $pid 2>/dev/null; do
        grep -q '"launcher_started"' "$events" && { started=1; break; }
        sleep 5; waited=$((waited + 5))
    done
    if [ $started = 1 ]; then
        sleep 30   # let the window draw
        screencapture -x "$OUT/screen-$label.png" 2>/dev/null
        grep -q '"launcher_minimized"\|"failed"' "$events" || true
    fi
    /bin/bash "$E" kill > "$OUT/kill-$label.jsonl" 2>&1
    for _ in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    kill $pid 2>/dev/null
    cat "$events"
    cp "$OUT/logs/latest-wine.log" "$OUT/wine-$label.log" 2>/dev/null
    if [ $required = 1 ]; then
        check "run-gryphlink ($label): GRYPHLINK started" [ $started = 1 ]
    elif [ $started = 1 ]; then
        note "- INFO run-gryphlink ($label): GRYPHLINK started"
    else
        note "- INFO run-gryphlink ($label): GRYPHLINK did not start (informational on the CI VM)"
    fi
}
launch wined3d 1
# DXMT with +loaddll, so the log shows whether DXMT's winemetal.dll was loaded
printf '{"debug_logging":true}\n' > "$OUT/home/config.json"
launch dxmt 0
# +loaddll prints 'Loaded L"...\winemetal.dll" at ...: builtin' when DXMT's bridge loads
check 'DXMT: winemetal.dll loads in DXMT mode' grep -qE 'Loaded L".*winemetal\.dll"' "$OUT/wine-dxmt.log"
check 'detailed logging leaves out the full exception trace' grep -q 'WINEDEBUG=fixme-all,err+all,warn+seh,+loaddll,+ntoskrnl,+process, ' "$OUT/wine-dxmt.log"
if grep -q 'winemetal.dll.*not found' "$OUT/wine-dxmt.log" 2>/dev/null; then
    note '- FAIL DXMT: "winemetal.dll not found" in the log'
    failures=$((failures + 1))
fi
# DXMT finds winemac.so's macdrv_functions table (patch 0013) to put a Metal view in a window
check 'DXMT: winemac.so exports macdrv_functions' bash -c \
    "nm -gU '$ENGINE/wine-dxmt/lib/wine/x86_64-unix/winemac.so' | grep -q ' _macdrv_functions\$'"
if grep -q 'Failed to create metal view' "$OUT/wine-dxmt.log" 2>/dev/null; then
    note '- FAIL DXMT: "Failed to create metal view" in the log'
    failures=$((failures + 1))
fi
rm -f "$OUT/home/config.json"
# Vulkan mode: the game would draw with Vulkan; GRYPHLINK goes through DXMT, not wined3d
launch vulkan 0
check 'Vulkan mode uses the DXMT Wine tree' grep -q '^# graphics: vulkan (tree .*/wine-dxmt)' "$OUT/wine-vulkan.log"
# D3DMetal mode: Apple's d3d11/dxgi. The VM's paravirtual GPU may not run it, so the launch is
# informational; the overlay layout and the tree choice are required.
if [ -x "$ENGINE/wine-d3dmetal/bin/wine" ]; then
    d3dm="$ENGINE/wine-d3dmetal/lib"
    check 'D3DMetal: d3d11.so and dxgi.so link to libd3dshared' bash -c \
        "[ \"\$(readlink '$d3dm/wine/x86_64-unix/d3d11.so')\" = ../../external/libd3dshared.dylib ] && [ \"\$(readlink '$d3dm/wine/x86_64-unix/dxgi.so')\" = ../../external/libd3dshared.dylib ] && [ -f '$d3dm/wine/x86_64-unix/d3d11.so' ]"
    check 'D3DMetal: the framework binary resolves' test -f "$d3dm/external/D3DMetal.framework/D3DMetal"
    check "D3DMetal: d3d11.dll is Apple's" grep -q 'D3DMetalDLLsBase' "$d3dm/wine/x86_64-windows/d3d11.dll"
    printf '{"debug_logging":true}\n' > "$OUT/home/config.json"
    launch d3dmetal 0
    check 'D3DMetal mode uses the D3DMetal Wine tree' grep -q '^# graphics: d3dmetal (tree .*/wine-d3dmetal)' "$OUT/wine-d3dmetal.log"
    note "- INFO D3DMetal: $(grep -c 'wine-d3dmetal.*d3d11' "$OUT/wine-d3dmetal.log" 2>/dev/null) d3d11 load lines; $(grep -c 'Failed to dlopen D3DMetal' "$OUT/wine-d3dmetal.log" 2>/dev/null) 'Failed to dlopen D3DMetal'"
    rm -f "$OUT/home/config.json"
else
    note '- FAIL D3DMetal: the engine has no wine-d3dmetal tree'
    failures=$((failures + 1))
fi

/bin/bash "$E" run-direct --graphics dxmt > "$OUT/run-direct.jsonl" 2>&1
check 'run-direct without a game fails with GAME_MISSING' grep -q '"code":"GAME_MISSING"' "$OUT/run-direct.jsonl"
/bin/bash "$E" logs > "$OUT/logs.jsonl" 2>&1
check 'logs reports paths' done_ok "$OUT/logs.jsonl"

# A chosen GRYPHLINK location (config library_dir), which Wine sees as L:
LIB="$OUT/library"
mkdir -p "$LIB"
/bin/bash "$E" check-location "$LIB" > "$OUT/location-ok.jsonl" 2>&1
cat "$OUT/location-ok.jsonl"
check 'check-location accepts a writable folder' done_ok "$OUT/location-ok.jsonl"
/bin/bash "$E" check-location "$OUT/home/prefix/drive_c" > "$OUT/location-bad.jsonl" 2>&1
# Diagnostics settings: all five non-default values reach the Wine environment
check 'stock MoltenVK is bundled for the diagnostics setting'     lipo "$ENGINE/moltenvk-stock/libMoltenVK.dylib" -verify_arch x86_64
printf '{"moltenvk_stock":true,"mvk_argument_buffers":false,"wined3d_csmt":false,"metal_validation":true,"hide_wine_versions":true,"exception_trace":true,"log_max_mb":10,"metal_hud_log":"1s"}
'     > "$OUT/home/config.json"
launch wined3d 0 diagnostics
check 'diagnostics settings reach the Wine log header'     grep -q 'csmt=0x0, diagnostics: stock-moltenvk no-argument-buffers no-csmt metal-validation' "$OUT/wine-diagnostics.log"
check 'hide_wine_versions reaches the Wine environment'     grep -q 'CX_HIDE_BUILTIN_VERSION_RESOURCES=1' "$OUT/wine-diagnostics.log"
# Discord Rich Presence against a fake Discord socket: the fallback activity while the game "runs",
# then a game-side client (discord-pipe-probe.exe) relayed through \\.\pipe\discord-ipc-0
if [ -n "$TEST_TOOLS" ] && [ -f "$TEST_TOOLS/discord-pipe-probe.exe" ]; then
    /usr/bin/python3 -I "$REPO/tools/discord-pipe-probe/fake-discord.py" "$OUT/ipc" "$OUT/ipc.log" 2> "$OUT/fake-discord.err" &
    fake_pid=$!
    sleep 1
    GRYPHFIELD_DISCORD_IPC_DIR="$OUT/ipc" /bin/bash "$E" _discord-selftest "$TEST_TOOLS/discord-pipe-probe.exe" \
        > "$OUT/discord-selftest.jsonl" 2> "$OUT/discord-selftest.err"
    kill "$fake_pid" 2>/dev/null
    cat "$OUT/discord-selftest.jsonl" "$OUT/ipc.log" 2>/dev/null
    cp "$OUT/logs/discord-selftest.log" "$OUT/" 2>/dev/null
    check 'discord: selftest finishes' done_ok "$OUT/discord-selftest.jsonl"
    check 'discord: fallback handshake uses the Endfield application' grep -q '"client_id": "1461154307171811401"' "$OUT/ipc.log"
    check 'discord: fallback sets an activity with a start time' grep -q '"cmd": "SET_ACTIVITY".*"timestamps"' "$OUT/ipc.log"
    check 'discord: the game handshake is relayed' grep -q '"client_id": "1234567890"' "$OUT/ipc.log"
    check "discord: the game's activity is relayed" grep -q '"state": "pipe probe"' "$OUT/ipc.log"
    check 'discord: the game side gets both replies' grep -q '"probe_exit":0' "$OUT/discord-selftest.jsonl"
else
    note '- INFO discord: no discord-pipe-probe.exe given; selftest skipped'
fi
check 'exception_trace adds the full +seh trace' grep -q 'WINEDEBUG=fixme-all,err+all,warn+seh,+seh, ' "$OUT/wine-diagnostics.log"
check 'log_max_mb below 50 is raised to 50' grep -q '; log limit 50 MB$' "$OUT/wine-diagnostics.log"
check 'metal_hud_log turns the HUD and its log on' grep -q 'metal-validation metal-hud hud-log-1s$' "$OUT/wine-diagnostics.log"
check 'the Metal HUD log file is created' bash -c "head -1 '$OUT/logs/latest-metal-hud.log' | grep -q '^# Gryphfield Metal HUD log (1s)'"

# Load the actual framework with all symbols resolved, then exercise D3D11
# device/swapchain creation and real presentation under both implementations.
check 'D3DMetal framework loads on this macOS runtime' env \
    DYLD_FALLBACK_LIBRARY_PATH="$ENGINE/wine-d3dmetal/lib/external:/usr/lib" \
    "$TEST_TOOLS/framework-probe" "$ENGINE/wine-d3dmetal/lib/external/D3DMetal.framework/D3DMetal"
for mode in dxmt d3dmetal; do
    printf '{"metal_hud_log":"frame"}\n' > "$OUT/home/config.json"
    /bin/bash "$E" _graphics-selftest "$mode" "$TEST_TOOLS/d3d11-probe.exe" > "$OUT/device-$mode.log" 2>&1
    rc=$?
    cat "$OUT/device-$mode.log"
    cp "$OUT/logs/latest-metal-hud.log" "$OUT/hud-$mode.log" 2>/dev/null
    check "$mode: HUD collector flushed and stopped" grep -q '^# Collected ' "$OUT/hud-$mode.log"
    if [ "$rc" = 0 ]; then
        check "$mode: real D3D11 device and 240 presented frames" grep -q '^presented_frames=240' "$OUT/device-$mode.log"
    elif { [ "$rc" = 2 ] || [ "$rc" = 137 ]; } && { grep -qi 'Paravirtual' "$OUT/device-$mode.log" || [ "$(sysctl -n kern.hv_vmm_present 2>/dev/null)" = 1 ] || system_profiler SPDisplaysDataType | grep -qiE 'Paravirtual|Virtual'; }; then
        # The VM's paravirtual GPU either refuses device creation (exit 2) or hangs it until the
        # fixture is killed (137). Both modes do this here, so it says nothing about a real Mac.
        note "- LIMITATION $mode: CI's paravirtual GPU did not give a D3D11 device (exit $rc); see device-$mode.log (not a gameplay pass)."
    else
        note "- FAIL $mode: D3D11 fixture exited $rc; see device-$mode.log"
        failures=$((failures + 1))
    fi
    if grep -q '"processID"' "$OUT/hud-$mode.log"; then
        note "- PASS $mode: attributed, per-frame Metal HUD records captured"
    else
        note "- LIMITATION $mode: macOS emitted no attributable Metal HUD records; HUD runtime logging is unverified on this runner."
    fi
done
# Child processes once dxgi.dll is loaded: on a real Mac, D3DMetal's dxgi (loaded by GRYPHLINK's
# crash reporter) made Launcher.exe fault inside NtCreateUserProcess. wined3d is the control.
for mode in wined3d dxmt d3dmetal; do
    /bin/bash "$E" _graphics-selftest "$mode" "$TEST_TOOLS/spawn-probe.exe" spawn > "$OUT/spawn-$mode.log" 2>&1
    rc=$?
    grep -E '^(spawn_|dxgi_loaded|factory_hr|adapter|result)' "$OUT/spawn-$mode.log"
    steps="$(grep -E '^(spawn_[a-z_]+|factory_hr|adapter|result)=' "$OUT/spawn-$mode.log" | cut -d' ' -f1 | tr '\n' ' ')"
    if [ "$mode" = wined3d ]; then
        check 'wined3d: child processes start before and after dxgi loads' grep -q '^result=ok' "$OUT/spawn-$mode.log"
    else
        note "- INFO $mode: spawn probe exit $rc: $steps(see spawn-$mode.log)"
    fi
done
check 'HUD collectors do not outlive the launch' bash -c "! pgrep -f '^$ENGINE/bin/gryphfield-hud ' >/dev/null"
rm -f "$OUT/home/config.json"
# Log limit: a 65 MB log with a 50 MB limit keeps its first and newest lines and drops to about half
seq -f 'line %08.0f padding-padding-padding-padding-padding' 1 1200000 > "$OUT/trim-test.log"
/bin/bash "$E" _trim-log "$OUT/trim-test.log" 50 > "$OUT/trim-test.jsonl" 2>&1
check 'log trim finishes' done_ok "$OUT/trim-test.jsonl"
check 'log trim stays under the limit' bash -c "[ \$(stat -f %z '$OUT/trim-test.log') -lt 52428800 ]"
check 'log trim keeps the first line' bash -c "head -1 '$OUT/trim-test.log' | grep -q '^line 00000001 '"
check 'log trim keeps the newest line' bash -c "tail -1 '$OUT/trim-test.log' | grep -q '^line 01200000 '"
check 'log trim marks the cut' grep -q '^# --- Gryphfield removed about' "$OUT/trim-test.log"
rm -f "$OUT/trim-test.log"
rm -f "$OUT/home/config.json"
check 'check-location rejects a folder inside the prefix' grep -q '"code":"LOCATION_INVALID"' "$OUT/location-bad.jsonl"
printf '{"library_dir":"%s"}
' "$LIB" > "$OUT/home/config.json"
/bin/bash "$E" setup > "$OUT/setup-location.jsonl" 2>> "$OUT/setup.err"
cat "$OUT/setup-location.jsonl"
check 'setup copies GRYPHLINK to the chosen location' test -f "$LIB/GRYPHLINK/Launcher.exe"
check 'the chosen location is mapped to L:' test "$(readlink "$OUT/home/prefix/dosdevices/l:")" = "$LIB"
/bin/bash "$E" status > "$OUT/status-location.jsonl" 2>> "$OUT/status-1.err"
check 'status reports the chosen location' grep -q '"custom":true,"available":true' "$OUT/status-location.jsonl"
check 'status sees GRYPHLINK in the chosen location' grep -q "\"gryphlink\":{\"path\":\"$LIB/GRYPHLINK\",\"installed\":true" "$OUT/status-location.jsonl"
launch wined3d 1 location
printf '{"library_dir":"%s"}
' "$OUT/unplugged-drive" > "$OUT/home/config.json"
/bin/bash "$E" run-gryphlink --graphics wined3d > "$OUT/run-missing-location.jsonl" 2>&1
check 'a missing location fails with LOCATION_MISSING' grep -q '"code":"LOCATION_MISSING"' "$OUT/run-missing-location.jsonl"
rm -f "$OUT/home/config.json"
/bin/bash "$E" status > "$OUT/status-default.jsonl" 2>> "$OUT/status-1.err"
check 'back on the default location, GRYPHLINK is in the prefix' grep -q '"custom":false' "$OUT/status-default.jsonl"
check 'the L: mapping is removed on the default location' bash -c "/bin/bash '$E' setup >/dev/null 2>&1; [ ! -e '$OUT/home/prefix/dosdevices/l:' ]"

note "Failures: $failures"
exit $((failures > 0))
