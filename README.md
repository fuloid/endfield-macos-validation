# Endfield macOS Wine validation

Private build experiment for a pinned upstream Wine 11.19 with selected FineWine/Rosetta patches. It checks compilation and small instruction/exception tests. No game or anti-cheat is downloaded or executed by CI. Current game compatibility is unproven.

## Run

Open **Actions → Validate Wine on macOS → Run workflow**. The macOS job is manual; pushes only start portable Linux helper checks. Pushes do not start macOS builds automatically.

It runs portable helper tests on Linux, then builds the full x86_64 Wine runtime on an Apple Silicon `macos-15` runner and attempts the six-check probe under Rosetta. Source revision: `455e3509b98a6919fd4ad1def4803e08c41c03b2`. Two compile jobs and reduced debug information limit memory/storage use. Compiler cache entries are bounded at 1 GB. Each macOS job has a four-hour timeout.

Both successful and failed jobs upload diagnostics, retained for three days. A successful build also uploads the Wine installation, source/patch provenance, and build metadata. These binaries still depend on compatible Intel Homebrew libraries; they are not a self-contained application bundle. Private Actions usage and artifacts count against the account's allowances.

## What success means

- The selected patches apply to the pinned source.
- The helpers extracted from that source pass 4,378 NOP and 11 XGETBV checks.
- Apple clang builds the macOS x86_64 code paths, including the Rosetta hooks.
- The installed runtime initializes a fresh prefix and the probe passes all six checks under Rosetta.

DXMT integration, MoltenVK GPU behavior, ACE acceptance, and gameplay still need separate validation. A passing CI run does not establish those results.

The audited transfer package, patch provenance, licenses, and original local evidence are under [`port/mac-handoff`](port/mac-handoff). [`MAC_HANDOFF.md`](port/mac-handoff/MAC_HANDOFF.md) documents manual use. CI uses native Homebrew build tools and Intel Homebrew runtime libraries; its setup records both toolchains.

The initial repository branch is `ci/macos-validation`; this project does not push to `main`.
