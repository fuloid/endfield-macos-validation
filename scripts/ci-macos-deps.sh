#!/usr/bin/env bash
set -euo pipefail
test "$(uname -s)" = Darwin
test "$(sysctl -n hw.optional.arm64)" = 1
if ! arch -x86_64 /usr/bin/true; then
    sudo /usr/sbin/softwareupdate --install-rosetta --agree-to-license
fi
arch -x86_64 /usr/bin/true
test -x /opt/homebrew/bin/brew
/opt/homebrew/bin/brew install bison mingw-w64 pkgconf ccache
if [ ! -x /usr/local/bin/brew ]; then
    curl -fL --retry 3 https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh \
        -o "$RUNNER_TEMP/homebrew-install.sh"
    NONINTERACTIVE=1 arch -x86_64 /bin/bash "$RUNNER_TEMP/homebrew-install.sh"
fi
arch -x86_64 /usr/local/bin/brew install freetype gnutls molten-vk
for formula in freetype gnutls molten-vk; do
    arch -x86_64 /usr/local/bin/brew list --versions "$formula"
done
lipo -verify_arch x86_64 /usr/local/opt/freetype/lib/libfreetype.dylib
lipo -verify_arch x86_64 /usr/local/opt/gnutls/lib/libgnutls.dylib
lipo -verify_arch x86_64 /usr/local/opt/molten-vk/lib/libMoltenVK.dylib
df -h "$RUNNER_TEMP"
