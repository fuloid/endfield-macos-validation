# CrossOver standalone experiment: implementation handoff

## Scope and provenance

Only new files under `port/crossover-experimental/` and the new
`.github/workflows/validate-crossover.yml` are owned by this experiment. The branch
remains `ci/macos-validation`; no worktree, production replacement, release, SDK
change, game-file change, main push, or new ACE/syscall patch was made.

CrossOver 26.3.0 FOSS Wine 11.0 archive SHA256:
`ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872`.
FineWine baseline: `cc4cefa60a8236ad4adb3716f4faa2c6c8f48981`.
All 44 patches are original blobs pinned individually in `sources.lock.json`.
The supplied `dl/cx/sources/wine` was a partial inspection extraction; the build
uses the complete verified archive. The same source/patch combination passed
preparation both locally and on CI.

Latest FineWine observed: `ab8b9e909c5f5d87f166de0cf4ec00a1a8aa29da` (seven commits
ahead). The later MSync waiter-node and stale-ntdll-symlink patches and the changed
ACE-CORE imports patch are excluded from this baseline. Review/authorization is
needed before adopting a different ACE baseline; obtain the friend's exact
version and graphics settings before claiming equivalence.

## Implementation and signed commits

- `edc29a8f3c200ef637f5b6555a8ac33a7df092a5`: full install/build scripts, original
  patches, lock, independent CPU/syscall/dylib smoke, engine assembly wrapper and
  isolated manual CI.
- `fd563081fb428ab9e0b8e3548ffeff6d80c68b4f`: initialize runner-specific paths in
  a step (GitHub does not expose `runner` in job-level env).
- `5aaff88427faf57a8a558202db434af4377093c4`: run the original Mach-dependent MSync
  test on macOS instead of Linux.

All three were committed with `-S`, verified locally as good signatures, and
pushed to the authorized branch. GitHub also verified the signing key.

## Validation evidence

- Local: shell syntax, Python compilation, all 44 committed-blob hashes, clean
  application to the complete archive, checksum-tampering rejection, and
  existing-source-directory rejection pass. The final workflow passes actionlint.
- [Initial YAML rejection, run 39432676616](https://github.com/fuloid/endfield-macos-validation/actions/runs/39432676616):
  failed before jobs because of the job-level `runner` context; corrected.
- [First dispatched run 39432724280](https://github.com/fuloid/endfield-macos-validation/actions/runs/39432724280):
  source archive and all patches passed; the original MSync fixture required
  Apple's Mach headers and therefore failed on Linux; moved to macOS unchanged.
- [Build/runtime run 39432948349](https://github.com/fuloid/endfield-macos-validation/actions/runs/39432948349):
  source verification, full x86_64 macOS configuration and original MSync WaitAll
  test passed (11/11 cases). Full Wine build/install, CrossOver's native
  `macdrv_functions` export check, pipe2-import check and dylib bundling passed.
  CPU/direct-syscall/dylib and assembled launcher steps completed with success
  conclusions. The run was cancelled during evidence upload: the original path
  included the temporary prefix (including Wine drive mappings). The corrected
  workflow uploads only logs/screenshots and supports explicit reuse of the
  completed matching Wine cache. Exact probe output and final rerun are pending.

The completed Wine build was cached under
`crossover-install-v1-macOS-ARM64-901a34458ac13190c6f2c798aa9ab5119222fd90ae90c117ff5efb92cb2b82e7`.
Its Wine build inputs are unchanged by the later generated-license-label correction.
The later packaging-only correction updates the generated license table as well
as VERSION.json and excludes this report from Wine cache invalidation. Its
metadata transformation was checked locally against an isolated output fixture.

The unchanged upstream comparison is
[direct syscall run 39430689402](https://github.com/fuloid/endfield-macos-validation/actions/runs/39430689402),
which failed on shipped upstream Wine: normal NtQueryInformationProcess worked,
the direct syscall faulted. This experiment reuses that probe; it does not change
or repeat the upstream syscall implementation work.

## Dependency gaps and integration proposal

The complete FOSS Wine build uses ordinary Clang/MinGW for x86_64, Rosetta, Intel
FreeType/GnuTLS/MoltenVK and the existing generic dylib closure bundler. Native
tools are not a newly hermetic SDK; Intel Homebrew/core revisions are inherited
from `scripts/ci-macos-deps.sh`, and actual versions are recorded in CI.
Observed build environment: macOS 15.7.9, Xcode 16.4 (16F6), Apple Clang 17.0.0;
Intel FreeType 2.14.1_1, GnuTLS 3.8.10 and stock MoltenVK 1.4.0. Engine assembly
prefers the baseline app's patched MoltenVK 1.4.2 through its existing library path.

CrossOver's `macdrv_functions` export is already present in this source. No
upstream-11.19 shim is applied. The missing optional `cxcompatdb.so` only causes
a warning in Wine's loader. The standalone artifact supplies neither Apple's
D3DMetal/GPTK implementation nor CrossOver's UI/compatibility database. Friend
success using those components does not establish standalone DXMT/MoltenVK parity.

The existing engine CLI does not enable `WINEMSYNC`; this baseline defaults to
server synchronization. The source contains MSync and its original rollback test,
but enabling it for the product would need a separate environment/configuration
decision and runtime validation. No existing CLI/configuration file was edited.

The new assembly wrapper takes `wine-install`, pinned DXMT v0.80 and an explicit
baseline app artifact (default run 39426019859) for unchanged launcher, helpers and
patched MoltenVK. It calls the existing assembler into a new directory and fixes
the new engine's Wine/provenance labels. The UI source and install protocol stay
unchanged. After review and real-Mac validation, the parent can select this
Wine install in app assembly and propagate its provenance metadata. No production
integration edit is included here.

## Required real-Mac validation

CI smoke is not gameplay verification. The friend must test an isolated fresh
prefix on real Apple Silicon: launcher installation/login, DXMT rendering,
game/ACE startup, sustained gameplay and graphics modes. Record macOS/Rosetta,
FineWine hashes, sync mode and the graphics backend; compare those with the known
working installation. Do not reuse or modify an existing game prefix for this
comparison. The half-window investigation remains paused.
