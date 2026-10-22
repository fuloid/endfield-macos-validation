# Gryphfield engine: components and licenses

Private build for personal use; not for redistribution.

| Component | Source | License |
|---|---|---|
| Wine 11.19 + patches in `wine/patches` | https://github.com/wine-mirror/wine (`455e3509`) with this repository's patches | LGPL-2.1-or-later |
| FreeType, GnuTLS and dependencies in `wine/lib/external` | Homebrew bottles (x86_64), copied unmodified apart from install names | their own licenses (FreeType: FTL/GPL-2.0; GnuTLS, nettle, gmp, libtasn1, libidn2, libunistring, p11-kit, gettext: LGPL/GPL family; libpng: libpng license) |
| DXMT (`wine-dxmt`: d3d11, dxgi, d3d10core, winemetal) | https://github.com/3Shain/dxmt v0.80 (`589adb78`), built from source with the patches in `tools/dxmt/` | LGPL-2.1-or-later |
| MoltenVK 1.4.2 + FineWine patches (`moltenvk`) | https://github.com/KhronosGroup/MoltenVK, patches from https://github.com/stoicswe/Endfield_FineWine | Apache-2.0 |
| `tools/direct-launch.exe` | https://github.com/mary-ext/crossover-wine-endfield `tools/direct-launch.c` | MIT |
| `tools/gryphfield-watch.exe`, `bin/gryphfield-engine` | this repository | LGPL-2.1-or-later |
| GRYPHLINK launcher (`gryphlink`) | Gryphline / Hypergryph | proprietary; included only for the owner's private use |
