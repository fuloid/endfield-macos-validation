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

**Known ACE limitation in the pinned baseline:** the vendored FineWine patch notes
say the ACE-CORE `KeBugCheckEx 0x65636154` ("Tace") watchdog still fires after
patches 0017–0019, including the cross-process handle and image notification
experiments. The newer ACE-CORE patch revision is not included. This experiment
has not tested the watchdog, so syscall success and launcher smoke do not
establish that the friend's current game passes ACE.

## Implementation and signed commits

- `edc29a8f3c200ef637f5b6555a8ac33a7df092a5`: full install/build scripts, original
  patches, lock, independent CPU/syscall/dylib smoke, engine assembly wrapper and
  isolated manual CI.
- `fd563081fb428ab9e0b8e3548ffeff6d80c68b4f`: initialize runner-specific paths in
  a step (GitHub does not expose `runner` in job-level env).
- `5aaff88427faf57a8a558202db434af4377093c4`: run the original Mach-dependent MSync
  test on macOS instead of Linux.
- `c5e6d6ce2e02a6ad969d548f09aee4c45913d90f`: constrain artifact evidence and
  correct generated provenance metadata.
- `377e7f7`: load the three configured Wine SONAMEs in the dylib smoke.
- `4523079`: retry transient fresh-prefix initialization in the isolated CI
  harness and retain each attempt's logs.

All experiment commits were committed with `-S`, verified locally as good
signatures, and pushed to the authorized branch. GitHub also verified the signing
key.

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
  The run was cancelled during evidence upload because the original path included
  the temporary prefix (including Wine drive mappings). The corrected workflow
  uploads only logs/screenshots and supports explicit reuse of the completed
  matching Wine cache.
- [Corrected probe run 39445688470](https://github.com/fuloid/endfield-macos-validation/actions/runs/39445688470):
  source/cache provenance, nine reported startup/CPU/syscall/dylib outcomes
  (including compile checks; five invoke Wine), and
  engine assembly passed. The engine contract step exposed the fresh-prefix
  `wineboot` issue below, so its final gate correctly failed.
- [Final macOS validation run 39447511931](https://github.com/fuloid/endfield-macos-validation/actions/runs/39447511931):
  source/cache provenance, the same nine reported standalone Wine outcomes, and the full
  engine/launcher smoke passed; final workflow conclusion is success. The fresh
  prefix setup failed once with WineUSB INF resource-copy error 1812, then
  succeeded on its second attempt against the same partially initialized
  temporary prefix. The cause is unresolved; a first failure returns
  `PREFIX_FAILED`, so the user would need to retry setup. The contract suite then
  reported zero failures. Wined3D started GRYPHLINK; DXMT startup was
  informational and it loaded `winemetal.dll`;
  CrossOver's `macdrv_functions` export check passed. This is CI VM smoke only,
  not game or sustained rendering validation.

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

The final run reused the Wine install built under commit `5aaff88` in run
39432948349. Its recorded `BUILD-INFO.txt` and that run's logs identify the
source revision; the final run did not rebuild Wine. The app artifact has not
been rebuilt as a release asset with the CrossOver engine.

The app-icon validation patch shipped with the current Wine 11.19 engine is not
included in this Wine 11.0 baseline. Check whether CrossOver 26.3 has an
equivalent before comparing against the friend's reported startup failure.

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
