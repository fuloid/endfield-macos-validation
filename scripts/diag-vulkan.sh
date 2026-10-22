#!/usr/bin/env bash
# Half-drawn GRYPHLINK window in wined3d (Vulkan renderer) mode: quadtest (tools/quadtest) one case
# per process with shaders from Microsoft's compiler, including ANGLE's textured copy to the
# backbuffer (case 8), then screenshots of presented frames (strip, textured copy, clear only) to
# see whether the triangle is lost on the way to the screen, and MoltenVK's converted MSL.
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
note 'Microsoft bytecode, one case per process (the VM reaches only 9_3 through MoltenVK):'
for c in 1 2 3 4 6 7 8; do
    run_qt "vk-93-case$c" "$ENGINE/wine" renderer=vulkan 0 -- 1 93 "$c"
done
note ''
note 'What reaches the screen (screenshots in the artifact, taken once quadtest prints "holding"):'
shot() {   # label tree d3d-config hold-mode
    local label=$1 i
    QUADTEST_HOLD_MODE=$4 run_qt "$label" "$2" "$3" 40000 -- 1 93 8 &
    pid=$!
    for i in $(seq 1 120); do grep -q '^holding' "$OUT/qt-$label.txt" 2>/dev/null && break; sleep 1; done
    sleep 3
    screencapture -x "$OUT/screen-$label.png" 2>/dev/null
    note "  - screenshot screen-$label.png, $i s after start"
    wait $pid
}
shot vk-hold-strip "$ENGINE/wine" renderer=vulkan strip
shot vk-hold-textured "$ENGINE/wine" renderer=vulkan textured
shot vk-hold-clear "$ENGINE/wine" renderer=vulkan clear
shot dxmt-hold-strip "$ENGINE/wine-dxmt" '' strip
note ''
note 'MoltenVK debug (converted MSL), 9_3 textured copy:'
MVK_CONFIG_LOG_LEVEL=4 MVK_CONFIG_DEBUG=1 run_qt vk-93-case8-mvkdebug "$ENGINE/wine" renderer=vulkan 0 -- 1 93 8
exit 0
