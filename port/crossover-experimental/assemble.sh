#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Retain the existing Gryphfield CLI and launcher/install contract in a NEW engine.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BASE="${1:?usage: assemble.sh baseline-engine dxmt-builtin-directory output-engine}"
export DXMT_DIR="${2:?DXMT builtin directory}"
OUT="${3:?new output engine}"
: "${WINE_INSTALL:?New standalone CrossOver Wine install}"
[ ! -e "$OUT" ] || { echo 'Output must not already exist' >&2; exit 1; }
cmp "$HERE/sources.lock.json" "$WINE_INSTALL/sources.lock.json"
export MVK_DIR="$BASE/moltenvk" GRYPHLINK_DIR="$BASE/gryphlink/GRYPHLINK" TOOLS_DIR="$BASE/tools"
export STOCK_MVK_DYLIB="$BASE/moltenvk-stock/libMoltenVK.dylib"
export ENGINE_VERSION=0.1.0-crossover-experimental DXMT_VERSION=v0.80
export PATCH_COMMIT="$(git -C "$REPO" rev-parse HEAD)"
bash "$REPO/engine/assemble.sh" "$OUT"
# The existing assembler's Wine label is specific to 11.19. Replace its metadata
# here, leaving production assembly unchanged. Retain full source provenance.
python3 - "$HERE" "$OUT" <<'PY'
import json, pathlib, shutil, sys
here, out = map(pathlib.Path, sys.argv[1:])
lock = json.loads((here / 'sources.lock.json').read_text())
path = out / 'VERSION.json'
version = json.loads(path.read_text())
version.update(wine='11.0', wine_rev='crossover-26.3.0',
               experimental=True, finewine_rev=lock['finewine']['revision'],
               crossover_source_sha256=lock['crossover']['sha256'])
path.write_text(json.dumps(version, indent=2) + '\n')
license_path = out / 'licenses/README.md'
rows = license_path.read_text().splitlines()
wine_rows = [i for i, row in enumerate(rows) if row.startswith('| Wine ')]
if len(wine_rows) != 1:
    raise ValueError('Expected exactly one Wine license/provenance row')
rows[wine_rows[0]] = (
    '| Wine 11.0 / CrossOver 26.3.0 + patches in `wine/patches` | '
    + lock['crossover']['url'] + ' with FineWine `'
    + lock['finewine']['revision'] + '` (archive/patch hashes: `wine/sources.lock.json`) '
    + '| LGPL-2.1-or-later |')
license_path.write_text('\n'.join(rows) + '\n')
shutil.copy2(here / 'sources.lock.json', out / 'wine')
shutil.copy2(here / 'README.md', out / 'licenses/CrossOver-experiment.md')
shutil.copy2(here / 'licenses/FineWine-MIT.txt', out / 'licenses')
shutil.copy2(pathlib.Path(__import__('os').environ['WINE_INSTALL']) / 'COPYING.LIB', out / 'licenses/Wine-COPYING.LIB')
PY
cat "$OUT/VERSION.json"
