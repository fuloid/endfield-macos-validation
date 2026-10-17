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

launch() {   # mode required(1/0)
    local mode=$1 required=$2 events="$OUT/run-$1.jsonl" pid waited=0 started=0
    /bin/bash "$E" run-gryphlink --graphics "$mode" > "$events" 2> "$OUT/run-$mode.err" &
    pid=$!
    while [ $waited -lt 300 ] && kill -0 $pid 2>/dev/null; do
        grep -q '"launcher_started"' "$events" && { started=1; break; }
        sleep 5; waited=$((waited + 5))
    done
    if [ $started = 1 ]; then
        sleep 30   # let the window draw
        screencapture -x "$OUT/screen-$mode.png" 2>/dev/null
        grep -q '"launcher_minimized"\|"failed"' "$events" || true
    fi
    /bin/bash "$E" kill > "$OUT/kill-$mode.jsonl" 2>&1
    for _ in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    kill $pid 2>/dev/null
    cat "$events"
    cp "$OUT/logs/latest-wine.log" "$OUT/wine-$mode.log" 2>/dev/null
    if [ $required = 1 ]; then
        check "run-gryphlink ($mode): GRYPHLINK started" [ $started = 1 ]
    elif [ $started = 1 ]; then
        note "- INFO run-gryphlink ($mode): GRYPHLINK started"
    else
        note "- INFO run-gryphlink ($mode): GRYPHLINK did not start (informational on the CI VM)"
    fi
}
launch wined3d 1
# DXMT with +loaddll, so the log shows whether DXMT's winemetal.dll was loaded
printf '{"debug_logging":true}\n' > "$OUT/home/config.json"
launch dxmt 0
if grep -qi 'winemetal' "$OUT/wine-dxmt.log" 2>/dev/null; then
    note '- INFO DXMT: winemetal.dll was loaded (DXMT d3d11 in use)'
else
    note '- INFO DXMT: no winemetal.dll load in the log'
fi
rm -f "$OUT/home/config.json"

/bin/bash "$E" run-direct --graphics dxmt > "$OUT/run-direct.jsonl" 2>&1
check 'run-direct without a game fails with GAME_MISSING' grep -q '"code":"GAME_MISSING"' "$OUT/run-direct.jsonl"
/bin/bash "$E" logs > "$OUT/logs.jsonl" 2>&1
check 'logs reports paths' done_ok "$OUT/logs.jsonl"

note "Failures: $failures"
exit $((failures > 0))
