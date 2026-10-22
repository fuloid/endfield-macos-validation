#!/bin/bash
# Focused diagnostic only; creates no distributable app or release assets.
set -uo pipefail
ENGINE="${1:?engine}" OUT="${2:?output}" TOOLS="${3:?test tools}"
mkdir -p "$OUT"
export GRYPHFIELD_HOME="$OUT/home" GRYPHFIELD_LOGS="$OUT/logs"
E="$ENGINE/bin/gryphfield-engine"
/bin/bash "$E" setup > "$OUT/setup.jsonl" 2> "$OUT/setup.err" || exit 1
env DYLD_FALLBACK_LIBRARY_PATH="$ENGINE/wine-d3dmetal/lib/external:/usr/lib" \
    "$TOOLS/framework-probe" "$ENGINE/wine-d3dmetal/lib/external/D3DMetal.framework/D3DMetal" || exit 1
failures=0
for mode in dxmt d3dmetal; do
    /bin/bash "$E" kill > "$OUT/kill-before-$mode.jsonl" 2>&1
    printf '{"metal_hud_log":"frame"}\n' > "$OUT/home/config.json"
    /bin/bash "$E" _graphics-selftest "$mode" "$TOOLS/d3d11-probe.exe" > "$OUT/device-$mode.log" 2>&1
    rc=$?
    echo "$mode exit=$rc"
    grep -E 'stage=|frame=|feature_level=|hresult=|presented_frames=|Unhandled|page fault|Sampling process' "$OUT/device-$mode.log" || true
    cp "$OUT/logs/latest-metal-hud.log" "$OUT/hud-$mode.log" 2>/dev/null
    /bin/bash "$E" kill > "$OUT/kill-after-$mode.jsonl" 2>&1
    [ "$rc" = 0 ] || failures=$((failures + 1))
done
exit $((failures > 0))
