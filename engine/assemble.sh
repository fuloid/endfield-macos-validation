#!/usr/bin/env bash
# Assemble the Gryphfield engine directory (Gryphfield.app/Contents/Resources/engine) on macOS.
#
#   WINE_INSTALL=... DXMT_DIR=... MVK_DIR=... GRYPHLINK_DIR=... TOOLS_DIR=... \
#   PATCH_COMMIT=... bash engine/assemble.sh <out-dir>
#
# WINE_INSTALL   installed, bundled Wine (build-mac.sh build + bundle), with lib/external
# DXMT_DIR       extracted dxmt-<version>-builtin release (x86_64-windows/, x86_64-unix/)
# MVK_DIR        patched MoltenVK build output (libMoltenVK.dylib, LICENSE.MoltenVK)
# STOCK_MVK_DYLIB optional: the stock MoltenVK release dylib, for the graphics diagnostics setting
# GRYPHLINK_DIR  the GRYPHLINK launcher folder, without games/
# D3DMETAL_DIR   optional: extracted d3dmetal-<version> bundle (external/, x86_64-windows/) from
#                Apple's Game Porting Toolkit, for the D3DMetal graphics mode
# TOOLS_DIR      gryphfield-watch.exe, direct-launch.exe, discord-bridge.exe and gryphfield-discord (native)
# GRYPHFIELD_D3DMETAL_TSD_NTDLL=1  install the experimental native-TSD ntdll pair
#                (WINE_INSTALL/lib/wine-d3dmetal-ntdll) only into the wine-d3dmetal tree.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:?usage: assemble.sh <out-dir>}"
: "${WINE_INSTALL:?}" "${DXMT_DIR:?}" "${MVK_DIR:?}" "${GRYPHLINK_DIR:?}" "${TOOLS_DIR:?}"
die() { echo "$*" >&2; exit 1; }
[ ! -e "$OUT" ] || die "$OUT already exists"
[ -x "$WINE_INSTALL/bin/wine" ] && [ -d "$WINE_INSTALL/lib/external" ] || die 'WINE_INSTALL needs a bundled Wine (build-mac.sh bundle)'
mkdir -p "$OUT"/{bin,tools,licenses}

# Engine CLI and Windows helpers
cp "$HERE/bin/gryphfield-engine" "$HERE/bin/gryphfield-profile" "$OUT/bin/"
chmod 755 "$OUT/bin/gryphfield-engine"
chmod 644 "$OUT/bin/gryphfield-profile"
cp "$TOOLS_DIR/gryphfield-watch.exe" "$TOOLS_DIR/direct-launch.exe" "$TOOLS_DIR/discord-bridge.exe" "$OUT/tools/"
cp "$TOOLS_DIR/gryphfield-discord" "$OUT/bin/"
chmod 755 "$OUT/bin/gryphfield-discord"
cp "$TOOLS_DIR/gryphfield-hud" "$OUT/bin/"
chmod 755 "$OUT/bin/gryphfield-hud"

# Wine: runtime only (no headers, import libraries or man pages)
mkdir -p "$OUT/wine"
(cd "$WINE_INSTALL" && tar -cf - bin lib share) | (cd "$OUT/wine" && tar -xf -)
rm -rf "$OUT/wine/share/man"
rm -rf "$OUT/wine/lib/wine-d3dmetal-ntdll"
find "$OUT/wine/lib/wine" -name '*.a' -delete
cp "$WINE_INSTALL/BUILD-INFO.txt" "$OUT/wine/" 2>/dev/null || true
cp -R "$WINE_INSTALL/patches" "$OUT/wine/patches" 2>/dev/null || true

# wine-dxmt: the same Wine, with DXMT in place of Wine's d3d11/dxgi/d3d10core. Wine searches its
# own library folder before WINEDLLPATH and finds that folder from the real path of ntdll.so, so
# the loader, ntdll.so, bin/ and DXMT's files are real copies; everything else links to ../wine.
build_dxmt_overlay() {
    local src="$OUT/wine" dst="$OUT/wine-dxmt" arch file name
    mkdir -p "$dst/lib/wine"
    cp -RP "$src/bin" "$dst/bin"
    ln -s ../wine/share "$dst/share"
    ln -s ../../wine/lib/external "$dst/lib/external"
    for arch in "$src"/lib/wine/*/; do
        arch="$(basename "$arch")"
        mkdir -p "$dst/lib/wine/$arch"
        for file in "$src/lib/wine/$arch"/*; do
            name="$(basename "$file")"
            if [ -e "$DXMT_DIR/$arch/$name" ]; then
                cp "$DXMT_DIR/$arch/$name" "$dst/lib/wine/$arch/$name"
            elif [ "$arch" = x86_64-unix ] && { [ "$name" = ntdll.so ] || [ "$name" = wine ] || [ "$name" = wine-preloader ]; }; then
                cp -p "$file" "$dst/lib/wine/$arch/$name"
            else
                ln -s "../../../../wine/lib/wine/$arch/$name" "$dst/lib/wine/$arch/$name"
            fi
        done
        # files DXMT adds (winemetal.dll / winemetal.so)
        if [ -d "$DXMT_DIR/$arch" ]; then
            for file in "$DXMT_DIR/$arch"/*; do
                name="$(basename "$file")"
                case "$name" in nvapi64.dll|nvngx.dll) continue ;; esac   # NVIDIA extensions: unused
                [ -e "$dst/lib/wine/$arch/$name" ] || cp "$file" "$dst/lib/wine/$arch/$name"
            done
        fi
    done
    for name in d3d11.dll dxgi.dll d3d10core.dll winemetal.dll; do
        [ -f "$dst/lib/wine/x86_64-windows/$name" ] && [ ! -L "$dst/lib/wine/x86_64-windows/$name" ] \
            || die "DXMT overlay is missing $name"
    done
    [ -f "$dst/lib/wine/x86_64-unix/winemetal.so" ] || die 'DXMT overlay is missing winemetal.so'
    if otool -L "$dst/lib/wine/x86_64-unix/winemetal.so" | grep -E '^\s+(/usr/local/|/opt/homebrew/)'; then
        die 'winemetal.so references Homebrew'
    fi
}
build_dxmt_overlay

# wine-d3dmetal: the same Wine with Apple's D3DMetal d3d11/dxgi (Game Porting Toolkit), laid out as
# in Apple's redist: lib/external holds D3DMetal.framework and libd3dshared.dylib, and each Unix side
# (x86_64-unix/<dll>.so) is a link to libd3dshared.dylib, so they share one loaded image.
build_d3dmetal_overlay() {
    local src="$OUT/wine" dst="$OUT/wine-d3dmetal" arch file name fw
    mkdir -p "$dst/lib/wine" "$dst/lib/external"
    cp -RP "$src/bin" "$dst/bin"
    ln -s ../wine/share "$dst/share"
    for file in "$src"/lib/external/*; do
        ln -s "../../../wine/lib/external/$(basename "$file")" "$dst/lib/external/$(basename "$file")"
    done
    cp -R "$D3DMETAL_DIR/external/D3DMetal.framework" "$D3DMETAL_DIR/external/libd3dshared.dylib" "$dst/lib/external/"
    fw="$dst/lib/external/D3DMetal.framework"
    ln -s A "$fw/Versions/Current"
    ln -s Versions/Current/D3DMetal "$fw/D3DMetal"
    ln -s Versions/Current/Resources "$fw/Resources"
    for arch in "$src"/lib/wine/*/; do
        arch="$(basename "$arch")"
        mkdir -p "$dst/lib/wine/$arch"
        for file in "$src/lib/wine/$arch"/*; do
            name="$(basename "$file")"
            if [ "$arch" = x86_64-windows ] && [ -e "$D3DMETAL_DIR/x86_64-windows/$name" ]; then
                cp "$D3DMETAL_DIR/x86_64-windows/$name" "$dst/lib/wine/$arch/$name"
            elif [ "$arch" = x86_64-unix ] && { [ "$name" = ntdll.so ] || [ "$name" = wine ] || [ "$name" = wine-preloader ]; }; then
                cp -p "$file" "$dst/lib/wine/$arch/$name"
            else
                ln -s "../../../../wine/lib/wine/$arch/$name" "$dst/lib/wine/$arch/$name"
            fi
        done
    done
    if [ "${GRYPHFIELD_D3DMETAL_TSD_NTDLL:-}" = 1 ]; then
        local variant="$WINE_INSTALL/lib/wine-d3dmetal-ntdll"
        [ -d "$variant" ] || die "GRYPHFIELD_D3DMETAL_TSD_NTDLL=1 but $variant is missing"
        (cd "$variant" && shasum -a 256 -c SHA256SUMS)
        for file in x86_64-unix/ntdll.so x86_64-windows/ntdll.dll; do
            rm -f "$dst/lib/wine/$file"
            cp -p "$variant/$file" "$dst/lib/wine/$file"
            cmp "$variant/$file" "$dst/lib/wine/$file"
        done
        cp "$variant/EXPERIMENTAL.txt" "$dst/EXPERIMENTAL-D3DMETAL-NTDLL.txt"
        cp -R "$variant/patches" "$dst/ntdll-patches"
    fi
    for name in d3d11 dxgi; do
        [ -f "$dst/lib/wine/x86_64-windows/$name.dll" ] && [ ! -L "$dst/lib/wine/x86_64-windows/$name.dll" ] \
            || die "D3DMetal overlay is missing $name.dll"
        rm -f "$dst/lib/wine/x86_64-unix/$name.so"
        ln -s ../../external/libd3dshared.dylib "$dst/lib/wine/x86_64-unix/$name.so"
    done
    lipo "$dst/lib/external/libd3dshared.dylib" -verify_arch x86_64
    lipo "$fw/D3DMetal" -verify_arch x86_64
    while IFS= read -r file; do
        [ -e "$file" ] || die "Broken D3DMetal overlay link: $file"
    done < <(find "$dst" -type l)
    while IFS= read -r file; do
        if file -b "$file" | grep -q 'Mach-O'; then
            if otool -L "$file" | grep -E '^[[:space:]]+(/usr/local/|/opt/homebrew/)'; then
                die "D3DMetal runtime references Homebrew: $file"
            fi
        fi
    done < <(find "$dst/lib/external" -type f)
}
if [ -n "${D3DMETAL_DIR:-}" ]; then build_d3dmetal_overlay; fi

# Patched MoltenVK (found first through DYLD_FALLBACK_LIBRARY_PATH)
mkdir -p "$OUT/moltenvk"
cp "$MVK_DIR/libMoltenVK.dylib" "$OUT/moltenvk/"
lipo "$OUT/moltenvk/libMoltenVK.dylib" -verify_arch x86_64
cp "$MVK_DIR/LICENSE.MoltenVK" "$OUT/licenses/MoltenVK-LICENSE.txt" 2>/dev/null || true
# Stock MoltenVK, used instead when config.json sets "moltenvk_stock": true (diagnostics)
if [ -n "${STOCK_MVK_DYLIB:-}" ]; then
    mkdir -p "$OUT/moltenvk-stock"
    cp "$STOCK_MVK_DYLIB" "$OUT/moltenvk-stock/libMoltenVK.dylib"
    lipo "$OUT/moltenvk-stock/libMoltenVK.dylib" -verify_arch x86_64
fi

# Bundled GRYPHLINK (copied into the prefix on first run)
mkdir -p "$OUT/gryphlink"
cp -R "$GRYPHLINK_DIR" "$OUT/gryphlink/GRYPHLINK"
rm -rf "$OUT/gryphlink/GRYPHLINK/games" "$OUT/gryphlink/GRYPHLINK/launcher_click.exe"
[ -f "$OUT/gryphlink/GRYPHLINK/Launcher.exe" ] || die 'GRYPHLINK_DIR has no Launcher.exe'
gl_version="$(cd "$OUT/gryphlink/GRYPHLINK" && for d in */; do d="${d%/}"; [ -f "$d/Games.exe" ] && echo "$d"; done \
    | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"

# Licenses
cp "$HERE/LICENSES.md" "$OUT/licenses/README.md"
for f in "$DXMT_DIR"/../LICENSE* "$DXMT_DIR"/LICENSE*; do [ -f "$f" ] && cp "$f" "$OUT/licenses/DXMT-$(basename "$f").txt"; done

cat > "$OUT/VERSION.json" <<JSON
{"version":"${ENGINE_VERSION:-0.1.0}","wine":"11.19","wine_rev":"455e3509","patch_commit":"${PATCH_COMMIT:-unknown}","gryphlink":"$gl_version","dxmt":"${DXMT_VERSION:-unknown}","d3dmetal":"${D3DMETAL_VERSION:-none}","moltenvk":"${MVK_VERSION:-1.4.2-patched}","built":"$(date -u +%Y-%m-%dT%H:%M:%SZ)"}
JSON
echo "Assembled engine in $OUT ($(du -sh "$OUT" | cut -f1))"
cat "$OUT/VERSION.json"
