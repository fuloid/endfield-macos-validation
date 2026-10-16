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
    (cd "$BUILD" && CC="${CC:-/usr/bin/clang -arch x86_64}" CXX="${CXX:-/usr/bin/clang++ -arch x86_64}" \
        OBJC="${OBJC:-/usr/bin/clang -arch x86_64}" \
        ac_cv_lib_soname_freetype="$freetype_prefix/lib/libfreetype.dylib" \
        ac_cv_lib_soname_gnutls="$gnutls_prefix/lib/libgnutls.dylib" \
        ac_cv_lib_soname_vulkan="$mvk_prefix/lib/libMoltenVK.dylib" "$SOURCE/configure" \
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
    file "$PREFIX"/bin/wine* | tee "$WORK_DIR/binaries.txt"
    echo "Installed into $PREFIX; continue with MAC_HANDOFF.md."
}
case "${1:-help}" in
    prepare) prepare ;;
    configure) configure ;;
    build) build ;;
    *) echo 'usage: bash build-mac.sh prepare|configure|build'; exit 1 ;;
esac
