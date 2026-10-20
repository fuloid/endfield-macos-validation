# Experimental standalone CrossOver / FineWine engine

This is an isolated alternative for Gryphfield, not a production engine replacement.
It builds and installs the entire **x86_64 Wine 11.0 tree from CrossOver 26.3.0 FOSS
sources**, with the exact 44 FineWine patches at
`cc4cefa60a8236ad4adb3716f4faa2c6c8f48981`. It does not build a replacement-module
payload that requires CrossOver.app. Apple Silicon runs it under Rosetta 2.

## Provenance and scope

`sources.lock.json` pins the official source archive by SHA256 and every vendored
patch in apply order. Patches are original Git blobs, including authorship and
existing whitespace. No 11.19 patches, new ACE changes, SDK changes, or direct
syscall fixes are included. Preparation fails on any hash mismatch or existing
source directory. It creates no commits in the extracted source tree.

Sources:

- CrossOver FOSS download: <https://www.codeweavers.com/crossover/source>.
  Archive SHA256: `ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872`.
  Wine and patches: LGPL-2.1-or-later. The full archive retains component notices;
  installed runtime includes Wine's COPYING.LIB and the original patches.
- FineWine: <https://github.com/stoicswe/Endfield_FineWine/tree/cc4cefa60a8236ad4adb3716f4faa2c6c8f48981>.
  Its original patch documentation is in `finewine/patches/README.md`.
  Original scripts/docs: MIT, notice in `licenses/FineWine-MIT.txt`.
  The vendored WaitAll test/fixture retain LGPL-2.1-or-later identifiers.
- New experiment scripts: MIT. Existing Gryphfield scripts keep their licenses.
- Latest FineWine observed on 2026-10-20:
  `ab8b9e909c5f5d87f166de0cf4ec00a1a8aa29da`, seven commits beyond this baseline.
  Its patch changes add `0006-msync-server-one-waiter-node-per-thread.patch` and
  `0007-ntdll-replace-stale-ntdll-symlink-in-winetemp-dir.patch`, and change
  `ace-core/0001`. These are deliberately not silently adopted. The baseline
  corresponds to the existing local source; matching a friend's later installation
  requires their exact FineWine version/patch hashes and graphics configuration.

## Build

On Apple Silicon macOS with Xcode/Command Line Tools, Rosetta, native Homebrew and
Python 3.12+ (CI installs the same Intel libraries as existing validation):

```sh
export WORK_DIR="$TMPDIR/gryphfield-crossover-experimental"
bash scripts/ci-macos-deps.sh
bash port/crossover-experimental/build.sh prepare
bash port/crossover-experimental/build.sh configure
bash port/crossover-experimental/build.sh build
bash port/crossover-experimental/build.sh bundle
python3 port/crossover-experimental/smoke.py --work "$WORK_DIR"
```

Use a fresh WORK_DIR. `prepare.py --work PATH --archive dl/cx/cx.tar.gz` can reuse
the complete local archive. `dl/cx/sources/wine` in this checkout is only a partial
inspection extraction and is not a build source. `JOBS` defaults to 2 for CI RAM.
The build uses standard Clang and MinGW; no proprietary cx-llvm/win32on64 toolchain
is needed for this 64-bit-only target. This does not support 32-bit executables.

The configure step enables FreeType, GnuTLS, CoreAudio and Vulkan/MoltenVK. It uses
leaf dylib names, avoids the newer SDK's pipe2 import using the existing configure
cache switch, and reuses the existing generic runtime closure bundler. It checks
CrossOver's native `macdrv_functions` export instead of applying upstream 11.19's
DXMT shim. No source edits happen beyond the pinned FineWine series.

## Gryphfield compatibility

`wine-install/{bin,lib,share}` has the same layout consumed by `engine/assemble.sh`.
To assemble a **new** experimental engine:

```sh
WINE_INSTALL="$WORK_DIR/wine-install" \
  bash port/crossover-experimental/assemble.sh \
  /path/to/extracted/baseline/engine /path/to/dxmt/v0.80 "$WORK_DIR/engine"
```

Use the pinned DXMT v0.80 builtin archive, not the baseline engine's whole
wine-dxmt directory (that would mix 11.19 modules into 11.0). The wrapper retains
the existing engine CLI, launcher assets, helpers, patched MoltenVK and install
behavior; it changes the new engine's provenance labels to CrossOver/Wine 11.0.
It does not alter any installed application, shared prefix, game, UI source, or
production packaging. CI borrows those assets from the explicitly selected
existing app artifact and tests with fresh temporary prefixes.

Proposed parent integration, only after review and real-Mac validation: select
this wine-install in the app assembly workflow and propagate its Wine/provenance
metadata. No integration edit or release publication is performed here.

## Dependencies and limits

The FOSS Wine tree builds without CrossOver.app, its proprietary UI or license
manager. The source optionally dlopens `cxcompatdb.so` when present; the standalone
build does not supply it. **D3DMetal, GPTK and CrossOver's proprietary compatibility
database are not supplied.** Friend results with paid CrossOver/D3DMetal do not
prove parity with this standalone DXMT/MoltenVK build. The official D3DMetal glue
in the FOSS source does not include Apple's D3DMetal implementation.

Native tools and the historical Intel Homebrew/core revisions are reused from
`scripts/ci-macos-deps.sh`; dependency versions and environment are captured in CI.
These dependencies are not a newly hermetic SDK. Runtime binaries are ad-hoc
signed, not notarized. Library closure bundling preserves original component
licensing obligations; this experiment is not a public distribution clearance.
See the existing engine LICENSES.md for the bundled graphics/runtime components.

## Validation

`.github/workflows/validate-crossover.yml` is manual-only and isolated from release
and production caches. It verifies the archive and patches on Linux, runs the
original deterministic MSync WaitAll race test on macOS (it needs Mach headers), builds the full install,
and runs CPU exception, normal/direct NtQueryInformationProcess (main + worker),
startup and every-bundled-dylib checks with Intel Homebrew hidden. Checks run
independently and write machine-readable results even if another probe fails.
The existing Gryphfield smoke then exercises setup, launcher and location/error
contracts against a new assembled engine. Graphics on GitHub's VM have limits.
Artifacts and diagnostic logs are retained even when runtime probes fail.

Prior upstream engine direct-syscall reference:
<https://github.com/fuloid/endfield-macos-validation/actions/runs/39430689402>
(failed: normal NtQueryInformationProcess succeeds, direct syscall faults).

**No gameplay claim follows from these tests.** A friend's real Apple Silicon Mac
must still validate a fresh standalone prefix, GRYPHLINK installation/login,
launcher rendering with DXMT, game startup/ACE initialization, sustained gameplay,
graphics modes and behavior with their exact macOS/Rosetta/FineWine versions.
Existing upstream syscall work and the paused half-window investigation are
outside this experiment.
