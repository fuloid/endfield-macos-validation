# Gryphfield: Arknights: Endfield on Apple Silicon

Experimental macOS app that runs GRYPHLINK and Arknights: Endfield on a bundled, patched upstream
**Wine 11.19** (x86_64, under Rosetta 2). This repository holds the Wine patches, the app engine and
the CI that builds and smoke-tests everything on GitHub's Apple Silicon runners.

Status: GRYPHLINK, login, resource downloads and the game world work on a tester's Mac (since patch 0021,
verified 2026-10-21). Passing CI does not prove gameplay, login or anti-cheat (ACE) acceptance; CI never
downloads or runs the game.

## Download

Builds are published as pre-releases (`gryphfield-v0.1.0-preN`) under
[Releases](../../releases), with a SHA-256 file and notes listing what changed and what is unverified.
Requires Apple Silicon, macOS 15 or later and Rosetta 2. The app is signed ad hoc, not notarized.

## Layout

| Path | Contents |
|---|---|
| `port/mac-handoff/patches/` | Wine patches applied to Wine 11.19 (`455e3509`), in order. `moltenvk/` holds the MoltenVK 1.4.2 patch set. |
| `port/mac-handoff/` | `build-mac.sh`, provenance, licenses, `SHA256SUMS` and per-patch notes in `MAC_HANDOFF.md`. |
| `engine/` | `gryphfield-engine` (the shell backend the UI calls), `assemble.sh` (builds the app's engine directory) and the Windows helpers `direct-launch` and `gryphfield-watch`. |
| `scripts/` | CI helpers: dependency setup, the macOS smoke test, the launcher smoke test and portable helper tests. |
| `tools/` | Small Windows probes the smoke test compiles and runs under Wine (syscall, version resource, RegSaveKey, DWM blur-behind), plus DXMT patches. |
| `port/crossover-experimental/` | Separate experiment: CrossOver 26.3.0's Wine 11.0 with FineWine patches. Not used by the app. |

The SwiftUI app lives on the `gryphfield-ui` branch.

### Wine patches

- `0001`–`0003`: Rosetta fixes (long NOPs, privileged instructions, XGETBV) and CrossOver workarounds.
- `0009`–`0011`: from FineWine (the Endfield `GetProcAddress` dispatcher workaround, QPC-based relative waits).
- `0012`: cap `select()` timeouts so long sleeps do not busy-loop on Darwin.
- `0013`–`0015`, `0018`, `0019`: macOS integration (DXMT exports, cursor, icons, process names, Dock icon).
- `0016`: restore the TEB GS base after SIGSYS (fixes the `0xDEADC0DE` exit on direct syscalls).
- `0017`: optional hiding of Wine's builtin DLL version resources (off by default).
- `0020`: keep cross-process children of layered windows visible (GRYPHLINK; candidate).
- `0021`: pad `RegSaveKey` output past a hive header so ACE's read does not fault (verified on a tester's Mac).
- `0022`: log Rosetta NOP skips on their own `rosetta` debug channel instead of `seh`.
- `0023`: windows that ask DWM for blur-behind (GRYPHLINK) are composited with their content's alpha (transparent, rounded corners).
- `0024`: game sound follows the macOS output device (headphones, AirPods) instead of staying on the device it started with.
- `0101`–`0115`: upstream-11.19 port of FineWine's ACE-CORE driver series.

`MAC_HANDOFF.md` has the details, sources and what each patch was verified against.

## CI

All macOS workflows are started manually from **Actions**.

| Workflow | Purpose |
|---|---|
| Validate Wine on macOS | Builds Wine with the patches (cached by patch hash), then runs the Rosetta probe and the smoke test. A push only runs the portable helper checks. Run this first after changing patches. |
| Build Gryphfield.app | Reuses that cached Wine build, adds DXMT, patched MoltenVK, GRYPHLINK and the UI (`ui_ref`, default `gryphfield-ui`), runs the engine smoke test and uploads `Gryphfield-macos.zip`. |
| Build DXMT (experiment) | Builds DXMT from source with `tools/dxmt` patches (transparency for blur-behind windows). The app uses the frozen result from R2 (`DXMT_URL` in Build Gryphfield.app). |
| Validate experimental CrossOver engine | Builds and packages the CrossOver experiment. |

Runs upload logs and smoke results as artifacts.

## Licenses

Wine and its patches are LGPL-2.1-or-later. Third-party notices are in `port/mac-handoff/licenses/`
and `engine/LICENSES.md`. Arknights: Endfield, GRYPHLINK and ACE belong to their owners. The game
is not included; GRYPHLINK installs it on the user's machine.
