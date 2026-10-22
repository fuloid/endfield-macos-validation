#!/usr/bin/env bash
# CI smoke test of an assembled engine on the GitHub macOS VM (no game files are available there):
# status -> setup -> run-gryphlink until GRYPHLINK starts, in wined3d mode (required) and DXMT
# mode (informational; the VM's paravirtual Metal may not support it), then the missing-game path.
#   bash engine/ci-smoke.sh <engine-dir> <output-dir>
set -uo pipefail
ENGINE="${1:?engine dir}"
OUT="${2:?output dir}"
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
printf '{"moltenvk_stock":true,"mvk_argument_buffers":false,"wined3d_csmt":false,"metal_validation":true,"hide_wine_versions":true,"exception_trace":true,"log_max_mb":10}
'     > "$OUT/home/config.json"
launch wined3d 0 diagnostics
check 'diagnostics settings reach the Wine log header'     grep -q 'csmt=0x0, diagnostics: stock-moltenvk no-argument-buffers no-csmt metal-validation' "$OUT/wine-diagnostics.log"
check 'hide_wine_versions reaches the Wine environment'     grep -q 'CX_HIDE_BUILTIN_VERSION_RESOURCES=1' "$OUT/wine-diagnostics.log"
check 'exception_trace adds the full +seh trace' grep -q 'WINEDEBUG=fixme-all,err+all,warn+seh,+seh, ' "$OUT/wine-diagnostics.log"
check 'log_max_mb below 50 is raised to 50' grep -q '; log limit 50 MB$' "$OUT/wine-diagnostics.log"
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
