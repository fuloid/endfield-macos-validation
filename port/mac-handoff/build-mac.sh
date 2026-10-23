#!/usr/bin/env bash
# Experimental standalone Wine handoff. Syntax/patches checked on Windows;
# configure, build, and runtime must be validated on macOS.
set -euo pipefail
PORT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="${WORK_DIR:-$PORT_DIR/mac-work}"
mkdir -p "$WORK_DIR"
WORK_DIR="$(cd "$WORK_DIR" && pwd)"
SOURCE="$WORK_DIR/wine-src"
BUILD="$WORK_DIR/wine-build"
PREFIX="$WORK_DIR/wine-install"
REV=455e3509b98a6919fd4ad1def4803e08c41c03b2
BREW="${INTEL_BREW:-/usr/local/bin/brew}"
TOOL_BREW="${TOOL_BREW:-$BREW}"

die() { echo "$*" >&2; exit 1; }
require_mac() {
    [ "$(uname -s)" = Darwin ] || die 'This step needs macOS.'
    arch -x86_64 /usr/bin/true || die 'Install Rosetta 2 first.'
}
source_check() {
    [ "$(git -C "$SOURCE" rev-parse HEAD)" = "$REV" ] || die 'Unexpected Wine revision.'
    [ -f "$SOURCE/.handoff-patches.sha256" ] || die 'Run prepare first.'
    (cd "$PORT_DIR" && shasum -a 256 -c "$SOURCE/.handoff-patches.sha256")
}
prepare() {
    [ ! -e "$SOURCE" ] || die "$SOURCE already exists. Use a new WORK_DIR for a fresh preparation."
    git init -q "$SOURCE"
    git -C "$SOURCE" remote add origin https://github.com/wine-mirror/wine.git
    git -C "$SOURCE" fetch --depth 1 origin "$REV"
    git -C "$SOURCE" -c core.autocrlf=false checkout -q --detach FETCH_HEAD
    local p
    for p in "$PORT_DIR"/patches/*.patch; do
        git -C "$SOURCE" apply --check "$p"
        git -C "$SOURCE" apply "$p"
    done
    (cd "$PORT_DIR" && shasum -a 256 patches/*.patch) > "$SOURCE/.handoff-patches.sha256"
    git -C "$SOURCE" diff --stat
}
configure() {
    require_mac
    source_check
    [ -x "$BREW" ] || die 'Intel Homebrew is required; see MAC_HANDOFF.md.'
    [ "$("$BREW" --prefix)" = /usr/local ] || die 'Use Intel Homebrew in /usr/local.'
    local bison_prefix freetype_prefix gnutls_prefix mvk_prefix mac_sdk
    bison_prefix="$("$TOOL_BREW" --prefix bison)"
    freetype_prefix="$("$BREW" --prefix freetype)"
    gnutls_prefix="$("$BREW" --prefix gnutls)"
    mvk_prefix="$("$BREW" --prefix molten-vk)"
    mac_sdk="$(xcrun --sdk macosx --show-sdk-path)"
    local dylib
    for dylib in "$freetype_prefix/lib/libfreetype.dylib" "$gnutls_prefix/lib/libgnutls.dylib" "$mvk_prefix/lib/libMoltenVK.dylib"; do
        [ -f "$dylib" ] || die "Missing runtime library: $dylib"
        lipo "$dylib" -verify_arch x86_64
    done
    export PATH="$bison_prefix/bin:$("$TOOL_BREW" --prefix)/bin:/usr/local/bin:$PATH"
    export PKG_CONFIG_LIBDIR="$WORK_DIR/system-pkgconfig:$freetype_prefix/lib/pkgconfig:$gnutls_prefix/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:$mac_sdk/usr/lib/pkgconfig"
    export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
    export CPPFLAGS="-I/usr/local/include ${CPPFLAGS:-}"
    export LDFLAGS="-L/usr/local/lib -L$mvk_prefix/lib ${LDFLAGS:-}"
    export LIBRARY_PATH="/usr/local/lib:$mvk_prefix/lib"
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"
    # Some SDKs omit pkg-config metadata for the system compression libraries,
    # although Homebrew's FreeType/GnuTLS .pc files require it. Probe actual
    # x86_64 system versions, then describe those libraries without extra paths.
    mkdir -p "$WORK_DIR/system-pkgconfig"
    cat > "$WORK_DIR/system-libraries.c" <<'C'
#include <stdio.h>
#include <zlib.h>
#include <bzlib.h>
int main(void)
{
    printf("zlib=%s\nbzip2=%s\n", zlibVersion(), BZ2_bzlibVersion());
    return 0;
}
C
    /usr/bin/clang -arch x86_64 "$WORK_DIR/system-libraries.c" -lz -lbz2 -o "$WORK_DIR/system-libraries"
    arch -x86_64 "$WORK_DIR/system-libraries" | tee "$WORK_DIR/system-libraries.log"
    local library version link_flag
    for library in zlib bzip2; do
        version="$(sed -n "s/^$library=//p" "$WORK_DIR/system-libraries.log" | cut -d, -f1)"
        [ -n "$version" ] || die "Could not query system $library version."
        if [ "$library" = zlib ]; then link_flag=-lz; else link_flag=-lbz2; fi
        printf 'Name: %s\nDescription: Apple system %s\nVersion: %s\nLibs: %s\n' \
            "$library" "$library" "$version" "$link_flag" > "$WORK_DIR/system-pkgconfig/$library.pc"
    done
    pkg-config --print-errors --cflags --libs freetype2 gnutls | tee "$WORK_DIR/dependency-flags.log"
    command -v x86_64-w64-mingw32-gcc >/dev/null || die 'Install mingw-w64 (see MAC_HANDOFF.md).'
    mkdir -p "$BUILD"
    [ ! -f "$BUILD/Makefile" ] || die 'Build already configured; use build or a new WORK_DIR.'
    # Load the runtime libraries by their install-name leaf (e.g. libfreetype.6.dylib), not an
    # absolute Homebrew path: `bundle` copies them into $PREFIX/lib/external and the launcher
    # sets DYLD_FALLBACK_LIBRARY_PATH to that folder, so users need no Homebrew.
    local ft_leaf tls_leaf vk_leaf
    ft_leaf="$(basename "$(otool -D "$freetype_prefix/lib/libfreetype.dylib" | tail -1)")"
    tls_leaf="$(basename "$(otool -D "$gnutls_prefix/lib/libgnutls.dylib" | tail -1)")"
    vk_leaf="$(basename "$(otool -D "$mvk_prefix/lib/libMoltenVK.dylib" | tail -1)")"
    # The macOS 27 SDK has pipe2 (available from 27.0), which configure finds; ntdll then links it
    # weakly and calls it unchecked, crashing at startup on macOS 26 and earlier
    # (mary-ext/crossover-wine-endfield aa312528). Use pipe() and fcntl() instead.
    (cd "$BUILD" && CC="${CC:-/usr/bin/clang -arch x86_64}" CXX="${CXX:-/usr/bin/clang++ -arch x86_64}" \
        OBJC="${OBJC:-/usr/bin/clang -arch x86_64}" \
        ac_cv_func_pipe2=no \
        ac_cv_lib_soname_freetype="$ft_leaf" \
        ac_cv_lib_soname_gnutls="$tls_leaf" \
        ac_cv_lib_soname_vulkan="$vk_leaf" "$SOURCE/configure" \
        --build=x86_64-apple-darwin --host=x86_64-apple-darwin \
        --enable-archs=x86_64 --with-mingw --disable-tests --without-x \
        --with-freetype --with-gnutls --with-vulkan --without-gstreamer \
        --prefix="$PREFIX") 2>&1 | tee "$WORK_DIR/configure.log"
    grep -q '^#define SONAME_LIBVULKAN ' "$BUILD/include/config.h" || die 'Vulkan loader was not detected.'
    grep -q '^#define SONAME_LIBGNUTLS ' "$BUILD/include/config.h" || die 'GnuTLS was not detected.'
    grep -q '^#define SONAME_LIBFREETYPE ' "$BUILD/include/config.h" || die 'FreeType was not detected.'
}
build() {
    require_mac
    source_check
    [ -f "$BUILD/Makefile" ] || die 'Run configure first.'
    export PATH="$("$TOOL_BREW" --prefix bison)/bin:$("$TOOL_BREW" --prefix)/bin:/usr/local/bin:$PATH"
    local jobs="${JOBS:-6}"
    make -C "$BUILD" -j"$jobs" 2>&1 | tee "$WORK_DIR/build.log"
    make -C "$BUILD" install 2>&1 | tee "$WORK_DIR/install.log"
    build_d3dmetal_ntdll
    file "$PREFIX"/bin/wine* | tee "$WORK_DIR/binaries.txt"
    echo "Installed into $PREFIX; continue with MAC_HANDOFF.md."
}
build_d3dmetal_ntdll() (
    require_mac
    source_check
    local variant="$PREFIX/lib/wine-d3dmetal-ntdll" saved="$WORK_DIR/ntdll-normal"
    local applied=0 p patches=()
    [ ! -e "$saved" ] || die "$saved already exists; inspect the previous variant build."
    cp -pR "$BUILD/dlls/ntdll" "$saved"
    restore_ntdll() {
        local status=$? i
        trap - EXIT
        for (( i=applied-1; i>=0; i-- )); do
            git -C "$SOURCE" apply --reverse "${patches[i]}" || status=1
        done
        cp -pR "$saved/." "$BUILD/dlls/ntdll/" || status=1
        [ "$status" != 0 ] || rm -rf "$saved"
        exit "$status"
    }
    trap restore_ntdll EXIT
    for p in "$PORT_DIR"/patches-d3dmetal/*.patch; do patches+=("$p"); done
    for p in "${patches[@]}"; do
        git -C "$SOURCE" apply --check "$p"
        git -C "$SOURCE" apply "$p"
        applied=$((applied + 1))
    done
    # PE sources do not receive __APPLE__ from the MinGW compiler.
    printf 'x86_64_CFLAGS += -DWINE_D3DMETAL_TSD\n' > "$WORK_DIR/d3dmetal-ntdll.mk"
    make -C "$BUILD" -f Makefile -f "$WORK_DIR/d3dmetal-ntdll.mk" -j"${JOBS:-6}" \
        dlls/ntdll/ntdll.so dlls/ntdll/x86_64-windows/ntdll.dll \
        2>&1 | tee "$WORK_DIR/build-d3dmetal-ntdll.log"
    mkdir -p "$variant/x86_64-unix" "$variant/x86_64-windows" "$variant/patches"
    install -m 755 "$BUILD/dlls/ntdll/ntdll.so" "$variant/x86_64-unix/ntdll.so"
    install -m 644 "$BUILD/dlls/ntdll/x86_64-windows/ntdll.dll" "$variant/x86_64-windows/ntdll.dll"
    cp "${patches[@]}" "$variant/patches/"
    printf 'Experimental D3DMetal native-TSD ntdll pair. CI/review only; do not release.\n' > "$variant/EXPERIMENTAL.txt"
    (cd "$variant" && shasum -a 256 x86_64-unix/ntdll.so x86_64-windows/ntdll.dll) > "$variant/SHA256SUMS"
    cmp -s "$PREFIX/lib/wine/x86_64-unix/ntdll.so" "$variant/x86_64-unix/ntdll.so" \
        && die 'Variant Unix ntdll unexpectedly matches the normal build.'
    cmp -s "$PREFIX/lib/wine/x86_64-windows/ntdll.dll" "$variant/x86_64-windows/ntdll.dll" \
        && die 'Variant PE ntdll unexpectedly matches the normal build.'
    echo "Experimental ntdll pair installed into $variant"
)
bundle() {
    # Copy the runtime libraries Wine loads (and every non-system library they depend on) into
    # $PREFIX/lib/external, point their cross-references at the same folder, and re-sign them.
    require_mac
    [ -x "$PREFIX/bin/wine" ] || die 'Run build first.'
    local ext="$PREFIX/lib/external" queue=() seen=" " lib leaf dep dep_leaf src
    rm -rf "$ext"
    mkdir -p "$ext"
    resolve() {   # a dependency reference -> a real file under /usr/local, or nothing
        case "$1" in
            /usr/local/*) [ -e "$1" ] && echo "$1" ;;
            @rpath/*|@loader_path/*) [ -e "/usr/local/lib/${1##*/}" ] && echo "/usr/local/lib/${1##*/}" ;;
        esac
    }
    for lib in freetype/lib/libfreetype.dylib gnutls/lib/libgnutls.dylib molten-vk/lib/libMoltenVK.dylib; do
        queue+=("$(otool -D "/usr/local/opt/$lib" | tail -1)")
    done
    while [ ${#queue[@]} -gt 0 ]; do
        lib="${queue[0]}"
        queue=("${queue[@]:1}")
        leaf="${lib##*/}"
        case "$seen" in *" $leaf "*) continue ;; esac
        seen="$seen$leaf "
        src="$(resolve "$lib")"
        [ -n "$src" ] || die "Cannot find $lib"
        cp -L "$src" "$ext/$leaf"
        chmod u+w "$ext/$leaf"
        install_name_tool -id "@rpath/$leaf" "$ext/$leaf" 2>/dev/null || die "install_name_tool -id failed: $leaf"
        for dep in $(otool -L "$ext/$leaf" | tail -n +2 | awk '{print $1}'); do
            case "$dep" in
                /usr/local/*|@rpath/*|@loader_path/*)
                    dep_leaf="${dep##*/}"
                    [ "$dep_leaf" = "$leaf" ] && continue
                    install_name_tool -change "$dep" "@loader_path/$dep_leaf" "$ext/$leaf" 2>/dev/null || die "install_name_tool -change failed: $leaf"
                    queue+=("$dep") ;;
            esac
        done
    done
    for lib in "$ext"/*.dylib; do
        codesign --force --sign - "$lib" >/dev/null 2>&1 || die "codesign failed: $lib"
    done
    # nothing may still point into Homebrew, neither the bundled libraries nor Wine itself
    local leftovers
    leftovers="$( { otool -L "$ext"/*.dylib; otool -L "$PREFIX"/bin/* "$PREFIX"/lib/wine/x86_64-unix/*.so 2>/dev/null; } \
        | grep -E '^\s+/usr/local/|^\s+/opt/homebrew/' || true)"
    [ -z "$leftovers" ] || die "Homebrew references remain: $leftovers"
    (cd "$ext" && for lib in *.dylib; do echo "$lib $(shasum -a 256 "$lib" | cut -d' ' -f1)"; done) > "$ext/BUNDLED.txt"
    echo "Bundled $(ls "$ext"/*.dylib | wc -l | tr -d ' ') libraries into $ext ($(du -sh "$ext" | cut -f1))"
    cat "$ext/BUNDLED.txt"
}
case "${1:-help}" in
    prepare) prepare ;;
    configure) configure ;;
    build) build ;;
    bundle) bundle ;;
    *) echo 'usage: bash build-mac.sh prepare|configure|build|bundle'; exit 1 ;;
esac
