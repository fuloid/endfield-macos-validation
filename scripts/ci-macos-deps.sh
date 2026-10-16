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
    # The current installer only supports Apple Silicon. Use official historical
    # Homebrew/core revisions that still provide Intel bottles for this experiment.
    for directory in Homebrew Cellar Caskroom Frameworks bin etc etc/bash_completion.d \
        include lib lib/pkgconfig opt sbin share share/aclocal share/doc share/info \
        share/locale share/man share/man/man{1,2,3,4,5,6,7,8} share/zsh \
        share/zsh/site-functions var var/log var/homebrew var/homebrew/linked; do
        sudo /usr/bin/install -d -o "$(id -un)" -g admin -m 0755 "/usr/local/$directory"
    done
    git init -q /usr/local/Homebrew
    git -C /usr/local/Homebrew remote add origin https://github.com/Homebrew/brew.git
    git -C /usr/local/Homebrew fetch --depth 1 origin c9ab4622bce3bc96253c66d2e775cedf92ad40a8
    git -C /usr/local/Homebrew checkout -q --detach FETCH_HEAD
    ln -s /usr/local/Homebrew/bin/brew /usr/local/bin/brew
fi
core=/usr/local/Homebrew/Library/Taps/homebrew/homebrew-core
if [ ! -d "$core/.git" ]; then
    git init -q "$core"
    git -C "$core" remote add origin https://github.com/Homebrew/homebrew-core.git
    git -C "$core" fetch --depth 1 origin c93932295cbf28ce84a0c068c25b6116db7eb152
    git -C "$core" checkout -q --detach FETCH_HEAD
fi
intel_brew() { HOMEBREW_NO_INSTALL_FROM_API=1 arch -x86_64 /usr/local/bin/brew "$@"; }
intel_brew --version
git -C "$core" rev-parse HEAD
intel_brew install --force-bottle freetype gnutls molten-vk
for formula in freetype gnutls molten-vk; do
    intel_brew list --versions "$formula"
done
lipo -verify_arch x86_64 /usr/local/opt/freetype/lib/libfreetype.dylib
lipo -verify_arch x86_64 /usr/local/opt/gnutls/lib/libgnutls.dylib
lipo -verify_arch x86_64 /usr/local/opt/molten-vk/lib/libMoltenVK.dylib
df -h "$RUNNER_TEMP"
