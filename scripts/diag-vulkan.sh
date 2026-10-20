#!/usr/bin/env bash
# Diagnose the half-drawn GRYPHLINK window in wined3d (Vulkan renderer) mode on the CI VM.
# Runs GRYPHLINK several times with one change each, screenshots it and keeps its Wine log:
#   A baseline        the shipped engine
#   B stock-mvk       KhronosGroup MoltenVK v1.4.2 release instead of our patched build
#   C no-argbuf       MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0
#   D no-csmt         WINE_D3D_CONFIG renderer=vulkan,csmt=0x0
#   E validation      Metal API validation (logged, not fatal) and MoltenVK debug logging
# First, quadtest.exe (tools/quadtest) draws a full-window quad in several ways and reads the
# backbuffer back, under wined3d (patched and stock MoltenVK) and DXMT.
#   bash scripts/diag-vulkan.sh <engine-dir> <stock-libMoltenVK.dylib> <output-dir> [repeats] [quadtest.exe]
set -uo pipefail
ENGINE="${1:?engine dir}"
STOCK_MVK="${2:?stock libMoltenVK.dylib}"
OUT="${3:?output dir}"
REPEATS="${4:-2}"
QUADTEST="${5:-}"
mkdir -p "$OUT"
export GRYPHFIELD_HOME="$OUT/home" GRYPHFIELD_LOGS="$OUT/logs"
E="$ENGINE/bin/gryphfield-engine"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
note() { echo "$*"; echo "$*" >> "$SUMMARY"; }

cp "$ENGINE/moltenvk/libMoltenVK.dylib" "$OUT/patched-libMoltenVK.dylib"
cp "$E" "$OUT/engine.orig"

/bin/bash "$E" setup > "$OUT/setup.jsonl" 2> "$OUT/setup.err"
grep -q '"event":"done","ok":true' "$OUT/setup.jsonl" || { note "setup failed"; cat "$OUT/setup.jsonl"; exit 1; }

quadtest() {   # label tree d3d-config
    local label=$1 tree=$2 rc
    (
        export WINEPREFIX="$OUT/home/prefix" WINEARCH=win64 WINEDEBUG='err+all,fixme-all'
        export DYLD_FALLBACK_LIBRARY_PATH="$ENGINE/moltenvk:$ENGINE/wine/lib/external:/usr/lib"
        export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
        if [ -n "$3" ]; then export WINE_D3D_CONFIG="$3"; else unset WINE_D3D_CONFIG; fi
        cp "$QUADTEST" "$WINEPREFIX/drive_c/quadtest.exe"
        cp -f "$ENGINE/wine-dxmt/lib/wine/x86_64-windows/winemetal.dll" "$WINEPREFIX/drive_c/windows/system32/" 2>/dev/null
        perl -e 'alarm shift; exec @ARGV' 180 "$tree/bin/wine" 'C:\quadtest.exe' 3             > "$OUT/quadtest-$label.txt" 2> "$OUT/quadtest-$label.log"
        rc=$?
        "$tree/bin/wineserver" -k 2>/dev/null
        exit $rc
    )
    rc=$?
    note "- quadtest $label: exit $rc, $(grep -c '^FAIL' "$OUT/quadtest-$label.txt") FAIL"
    grep -E '^(PASS|FAIL)' "$OUT/quadtest-$label.txt" | head -10 | sed 's/^/    /' | tee -a "$SUMMARY"
}

launch() {   # label
    local label=$1 pid waited=0 started=0 events="$OUT/run-$1.jsonl"
    /bin/bash "$E" run-gryphlink --graphics wined3d > "$events" 2> "$OUT/run-$label.err" &
    pid=$!
    while [ $waited -lt 300 ] && kill -0 $pid 2>/dev/null; do
        grep -q '"launcher_started"' "$events" && { started=1; break; }
        sleep 5; waited=$((waited + 5))
    done
    if [ $started = 1 ]; then
        sleep 30   # let the window draw
        screencapture -x "$OUT/screen-$label.png" 2>/dev/null
    fi
    /bin/bash "$E" kill > "$OUT/kill-$label.jsonl" 2>&1
    for _ in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    kill $pid 2>/dev/null
    cp "$OUT/logs/latest-wine.log" "$OUT/wine-$label.log" 2>/dev/null
    note "- $label: GRYPHLINK started=$started"
}

variant() {   # name: set up, run REPEATS times, restore
    local name=$1 i
    for i in $(seq 1 "$REPEATS"); do launch "$name-$i"; done
    cp "$OUT/patched-libMoltenVK.dylib" "$ENGINE/moltenvk/libMoltenVK.dylib"
    cp "$OUT/engine.orig" "$E"
    unset MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS MTL_DEBUG_LAYER MTL_DEBUG_LAYER_ERROR_MODE \
          MTL_DEBUG_LAYER_WARNING_MODE MVK_CONFIG_LOG_LEVEL MVK_CONFIG_DEBUG
}

note '## wined3d/Vulkan half-window diagnosis'
if [ -n "$QUADTEST" ]; then
    quadtest wined3d-vk "$ENGINE/wine" renderer=vulkan
    quadtest dxmt "$ENGINE/wine-dxmt" ''
    cp "$STOCK_MVK" "$ENGINE/moltenvk/libMoltenVK.dylib"
    quadtest wined3d-vk-stock-mvk "$ENGINE/wine" renderer=vulkan
    cp "$OUT/patched-libMoltenVK.dylib" "$ENGINE/moltenvk/libMoltenVK.dylib"
fi
[ "$REPEATS" -gt 0 ] || exit 0
variant A-baseline

cp "$STOCK_MVK" "$ENGINE/moltenvk/libMoltenVK.dylib"
variant B-stock-mvk

export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0
variant C-no-argbuf

sed -i '' "s/WINE_D3D_CONFIG='renderer=vulkan'/WINE_D3D_CONFIG='renderer=vulkan,csmt=0x0'/g" "$E"
grep -q 'csmt=0x0' "$E" || note "- D: could not patch WINE_D3D_CONFIG"
variant D-no-csmt

export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog \
       MVK_CONFIG_LOG_LEVEL=4 MVK_CONFIG_DEBUG=1
variant E-validation
