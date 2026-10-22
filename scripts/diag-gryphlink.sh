#!/usr/bin/env bash
# Diagnostic only: the extracted app is disposable; no release files are changed.
set -euo pipefail
ENGINE="${1:?engine}"; OUT="${3:?output}"
mkdir -p "$OUT"
export GRYPHFIELD_HOME="$OUT/home" GRYPHFIELD_LOGS="$OUT/logs"
E="$ENGINE/bin/gryphfield-engine"
bash "$E" setup > "$OUT/setup.jsonl" 2> "$OUT/setup.err"
grep -q '"event":"done","ok":true' "$OUT/setup.jsonl"
# Let this disposable copy accept a diagnostic trace channel override.
python3 - "$E" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
s = p.read_text()
assert s.count('    export WINEDEBUG\n') == 1
p.write_text(s.replace('    export WINEDEBUG\n',
    '    WINEDEBUG="${DIAG_WINEDEBUG:-$WINEDEBUG}"\n    export WINEDEBUG\n'))
PY
cleanup() { bash "$E" kill >/dev/null 2>&1 || true; }
trap cleanup EXIT
for label in baseline trace; do
    if [ "$label" = trace ]; then
        export DIAG_WINEDEBUG='-all,err+all,trace+d3d,trace+d3d11,trace+dxgi'
    else
        export DIAG_WINEDEBUG='fixme-all,err+all,warn+seh'
    fi
    bash "$E" run-gryphlink --graphics wined3d > "$OUT/$label.jsonl" 2> "$OUT/$label.err" &
    pid=$!
    # Bound disk use and capture the first rendered frames, not minutes of idle tracing.
    for second in $(seq 1 60); do
        sleep 1
        if [ "$second" = 15 ] || [ "$second" = 30 ] || [ "$second" = 60 ]; then
            screencapture -x "$OUT/$label-$second.png" || true
        fi
        log="$GRYPHFIELD_LOGS/latest-wine.log"
        if [ -f "$log" ] && [ "$(stat -L -f %z "$log")" -gt 268435456 ]; then
            echo "$label: trace capped at second $second" | tee -a "$OUT/summary.txt"
            screencapture -x "$OUT/$label-capped.png" || true
            break
        fi
    done
    cleanup
    wait "$pid" || true
    test -f "$GRYPHFIELD_LOGS/latest-wine.log"
    gzip -c "$GRYPHFIELD_LOGS/latest-wine.log" > "$OUT/$label.log.gz"
    echo "$label: captured" | tee -a "$OUT/summary.txt"
done
