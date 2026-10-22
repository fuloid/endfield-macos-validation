#!/usr/bin/env bash
# Half-drawn GRYPHLINK window in wined3d (Vulkan renderer) mode: quadtest (tools/quadtest) one case
# per process, at feature level 9_3 (GRYPHLINK's path on MoltenVK) and the highest level, with the
# shaders from Microsoft's compiler; once with Wine's run-time HLSL compiler (earlier runs crashed
# in vkCreateGraphicsPipelines that way); DXMT as the reference; a screenshot of what a presented
# frame looks like on screen; and MoltenVK's converted MSL for the 9_3 strip.
#   bash scripts/diag-vulkan.sh <engine-dir> <quadtest.exe> <output-dir>
set -uo pipefail
ENGINE="${1:?engine dir}"
QUADTEST="${2:?quadtest.exe}"
OUT="${3:?output dir}"
mkdir -p "$OUT"
export GRYPHFIELD_HOME="$OUT/home" GRYPHFIELD_LOGS="$OUT/logs"
E="$ENGINE/bin/gryphfield-engine"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
note() { echo "$*"; echo "$*" >> "$SUMMARY"; }

/bin/bash "$E" setup > "$OUT/setup.jsonl" 2> "$OUT/setup.err"
grep -q '"event":"done","ok":true' "$OUT/setup.jsonl" || { note "setup failed"; tail -20 "$OUT/setup.jsonl"; exit 1; }
PREFIX="$OUT/home/prefix"
cp "$QUADTEST" "$PREFIX/drive_c/quadtest.exe"
cp -f "$ENGINE/wine-dxmt/lib/wine/x86_64-windows/winemetal.dll" "$PREFIX/drive_c/windows/system32/" 2>/dev/null

run_qt() {   # label tree d3d-config hold-ms -- quadtest args...; extra env comes from the caller
    local label=$1 tree=$2 config=$3 hold=$4 rc
    shift 5
    (
        export WINEPREFIX="$PREFIX" WINEARCH=win64 WINEDEBUG="${QT_WINEDEBUG:-err+all,fixme-all,warn+seh}"
        export DYLD_FALLBACK_LIBRARY_PATH="$ENGINE/moltenvk:$ENGINE/wine/lib/external:/usr/lib"
        export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
        if [ -n "$config" ]; then export WINE_D3D_CONFIG="$config"; else unset WINE_D3D_CONFIG; fi
        if [ "$hold" != 0 ]; then export QUADTEST_HOLD_MS="$hold"; fi
        perl -e 'alarm shift; exec @ARGV' 150 "$tree/bin/wine" 'C:\quadtest.exe' "$@" \
            > "$OUT/qt-$label.txt" 2> "$OUT/qt-$label.log"
        rc=$?
        "$tree/bin/wineserver" -k 2>/dev/null
        exit $rc
    )
    rc=$?
    note "- \`$label\`: exit $rc; $(grep -E '^(PASS|FAIL)' "$OUT/qt-$label.txt" | sed -E 's/ +/ /g' | tr '\n' ';' | cut -c1-400)$(grep -q 'Exception' "$OUT/qt-$label.log" && echo " **exception:** $(grep -m1 'Exception' "$OUT/qt-$label.log")")"
}

note '## quadtest under wined3d (Vulkan) / MoltenVK'
note ''
note 'Microsoft bytecode, one case per process:'
for level in 93 any; do
    for c in 1 2 3 4 6 7; do
        run_qt "vk-$level-case$c" "$ENGINE/wine" renderer=vulkan 0 -- 1 "$level" "$c"
    done
done
note ''
note 'Wine run-time HLSL compiler (vkd3d), case 1:'
QT_WINEDEBUG='err+all,fixme-all,+seh' run_qt vk-93-case1-hlsl "$ENGINE/wine" renderer=vulkan 0 -- 1 93 1 hlsl
QT_WINEDEBUG='err+all,fixme-all,+seh' run_qt vk-any-case1-hlsl "$ENGINE/wine" renderer=vulkan 0 -- 1 any 1 hlsl
note ''
note 'DXMT reference (all cases):'
run_qt dxmt-any "$ENGINE/wine-dxmt" '' 0 -- 1 any 0
note ''
note 'What reaches the screen (screenshots in the artifact):'
for level in 93 any; do
    run_qt "vk-$level-hold" "$ENGINE/wine" renderer=vulkan 9000 -- 1 "$level" 1 &
    pid=$!
    sleep 6
    screencapture -x "$OUT/screen-vk-$level.png" 2>/dev/null
    wait $pid
done
run_qt dxmt-hold "$ENGINE/wine-dxmt" '' 9000 -- 1 any 1 &
pid=$!
sleep 6
screencapture -x "$OUT/screen-dxmt.png" 2>/dev/null
wait $pid
note ''
note 'MoltenVK debug (converted MSL), 9_3 strip:'
MVK_CONFIG_LOG_LEVEL=4 MVK_CONFIG_DEBUG=1 run_qt vk-93-case1-mvkdebug "$ENGINE/wine" renderer=vulkan 0 -- 1 93 1
exit 0
