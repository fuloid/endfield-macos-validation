#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Full FOSS CrossOver Wine install, never the module-swap FineWine payload.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
: "${WORK_DIR:?Set WORK_DIR to a fresh experiment directory}"
mkdir -p "$WORK_DIR"
WORK_DIR="$(cd "$WORK_DIR" && pwd)"
export WORK_DIR
SOURCE="$WORK_DIR/wine-src"
BUILD="$WORK_DIR/wine-build"
PREFIX="$WORK_DIR/wine-install"
TOOL_BREW="${TOOL_BREW:-/opt/homebrew/bin/brew}"
INTEL_BREW="${INTEL_BREW:-/usr/local/bin/brew}"
die() { echo "$*" >&2; exit 1; }
require_mac() {
    [ "$(uname -s)" = Darwin ] || die 'This step requires macOS.'
    arch -x86_64 /usr/bin/true || die 'Rosetta 2 is required.'
    cmp "$HERE/sources.lock.json" "$WORK_DIR/prepared.json" || die 'Prepared source lock differs.'
    python3 "$HERE/prepare.py" --work "$WORK_DIR" --verify-only
}
configure() {
    require_mac
    [ ! -e "$BUILD/Makefile" ] || die 'Already configured; use a fresh WORK_DIR.'
    export PATH="$("$TOOL_BREW" --prefix bison)/bin:$("$TOOL_BREW" --prefix)/bin:/usr/local/bin:$PATH"
    local ft tls mvk sdk leaf_ft leaf_tls leaf_vk
    ft="$("$INTEL_BREW" --prefix freetype)"
    tls="$("$INTEL_BREW" --prefix gnutls)"
    mvk="$("$INTEL_BREW" --prefix molten-vk)"
    sdk="$(xcrun --sdk macosx --show-sdk-path)"
    lipo "$ft/lib/libfreetype.dylib" -verify_arch x86_64
    lipo "$tls/lib/libgnutls.dylib" -verify_arch x86_64
    lipo "$mvk/lib/libMoltenVK.dylib" -verify_arch x86_64
    leaf_ft="$(basename "$(otool -D "$ft/lib/libfreetype.dylib" | tail -1)")"
    leaf_tls="$(basename "$(otool -D "$tls/lib/libgnutls.dylib" | tail -1)")"
    leaf_vk="$(basename "$(otool -D "$mvk/lib/libMoltenVK.dylib" | tail -1)")"
    # Describe Apple's compression libraries when the SDK omits .pc metadata.
    mkdir -p "$WORK_DIR/system-pkgconfig" "$BUILD"
    cat > "$WORK_DIR/system-libraries.c" <<'C'
#include <stdio.h>
#include <zlib.h>
#include <bzlib.h>
int main(void) { printf("zlib=%s\nbzip2=%s\n", zlibVersion(), BZ2_bzlibVersion()); return 0; }
C
    /usr/bin/clang -arch x86_64 "$WORK_DIR/system-libraries.c" -lz -lbz2 -o "$WORK_DIR/system-libraries"
    arch -x86_64 "$WORK_DIR/system-libraries" > "$WORK_DIR/system-libraries.log"
    local lib version flag
    for lib in zlib bzip2; do
        version="$(sed -n "s/^$lib=//p" "$WORK_DIR/system-libraries.log" | cut -d, -f1)"
        [ -n "$version" ] || die "Missing system $lib version"
        if [ "$lib" = zlib ]; then flag=-lz; else flag=-lbz2; fi
        printf 'Name: %s\nDescription: Apple system %s\nVersion: %s\nLibs: %s\n' \
            "$lib" "$lib" "$version" "$flag" > "$WORK_DIR/system-pkgconfig/$lib.pc"
    done
    export PKG_CONFIG_LIBDIR="$WORK_DIR/system-pkgconfig:$ft/lib/pkgconfig:$tls/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:$sdk/usr/lib/pkgconfig"
    export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
    export CPPFLAGS="-I/usr/local/include ${CPPFLAGS:-}"
    export LDFLAGS="-L/usr/local/lib -L$mvk/lib ${LDFLAGS:-}"
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"
    pkg-config --print-errors --cflags --libs freetype2 gnutls
    # Cache variables select relocatable leaf names. CrossOver probes MoltenVK
    # with a capital M, unlike upstream's Vulkan-only path.
    (cd "$BUILD" && CC="${CC:-/usr/bin/clang -arch x86_64}" \
        CXX="${CXX:-/usr/bin/clang++ -arch x86_64}" OBJC="${OBJC:-/usr/bin/clang -arch x86_64}" \
        ac_cv_func_pipe2=no ac_cv_lib_soname_freetype="$leaf_ft" \
        ac_cv_lib_soname_gnutls="$leaf_tls" ac_cv_lib_soname_vulkan="$leaf_vk" \
        ac_cv_lib_soname_MoltenVK="$leaf_vk" "$SOURCE/configure" \
        --build=x86_64-apple-darwin --host=x86_64-apple-darwin --enable-archs=x86_64 \
        --with-mingw --disable-tests --without-x --with-freetype --with-gnutls \
        --with-vulkan --without-gstreamer --prefix="$PREFIX") 2>&1 | tee "$WORK_DIR/configure.log"
    for lib in VULKAN GNUTLS FREETYPE; do
        grep -q "^#define SONAME_LIB$lib " "$BUILD/include/config.h" || die "Missing $lib support"
    done
}
build() {
    require_mac
    export PATH="$("$TOOL_BREW" --prefix bison)/bin:$("$TOOL_BREW" --prefix)/bin:/usr/local/bin:$PATH"
    make -C "$BUILD" -j"${JOBS:-2}" 2>&1 | tee "$WORK_DIR/build.log"
    make -C "$BUILD" install 2>&1 | tee "$WORK_DIR/install.log"
    cp "$BUILD/include/config.h" "$PREFIX/BUILD-config.h"
    cp "$SOURCE/COPYING.LIB" "$PREFIX/"
    cp "$HERE/sources.lock.json" "$PREFIX/"
    cp -R "$HERE/patches" "$HERE/licenses" "$PREFIX/"
    {
        echo 'Experimental standalone CrossOver 26.3.0 / Wine 11.0 + FineWine'
        git -C "$REPO" rev-parse HEAD
        sw_vers
        xcodebuild -version
        /usr/bin/clang --version
    } > "$PREFIX/BUILD-INFO.txt"
    # CrossOver already provides its own macdrv_functions; do not port 11.19's shim.
    nm -gU "$PREFIX/lib/wine/x86_64-unix/winemac.so" | grep ' _macdrv_functions$'
    if nm -u "$PREFIX/lib/wine/x86_64-unix/ntdll.so" | grep -E '(^| )_pipe2$'; then
        die 'ntdll imports pipe2, unavailable on older supported macOS'
    fi
}
case "${1:-}" in
    prepare) python3 "$HERE/prepare.py" --work "$WORK_DIR" ;;
    configure) configure ;;
    build) build ;;
    bundle)
        require_mac
        # Reuse only the existing generic dylib closure bundler. Its bundle
        # command does not prepare, patch or configure upstream Wine.
        bash "$REPO/port/mac-handoff/build-mac.sh" bundle
        ;;
    *) die 'usage: build.sh prepare|configure|build|bundle' ;;
esac
