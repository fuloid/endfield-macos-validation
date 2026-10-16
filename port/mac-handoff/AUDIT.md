# Independent audit, 2026-10-16

Verdict: the Rosetta patch logic survives local review and expanded tests. The previous handoff overstated readiness. The corrected package is ready for a Mac build experiment; macOS compilation and game compatibility remain unproven.

## Findings corrected

1. **Missing upstream patches.** The old checklist specified upstream 11.19 plus two Rosetta patches and misc/0008. Upstream also needs the selected dispatcher patches misc/0009–0010 and timing patch misc/0011 to reproduce FineWine's chosen mitigations. All three apply cleanly. Misc/0008 targets a different executable and Linux display drivers; it is unnecessary here.
2. **Incomplete standalone build instructions.** FineWine's script builds replacement CrossOver modules by default. Its disabled FreeType/GnuTLS/CoreAudio/Vulkan options do not describe a standalone game runtime. Added a pinned, full-tree Mac recipe that requires the relevant runtime libraries and retains CoreAudio. Darwin execution is still pending.
3. **Missing wrapper setting.** Standalone Wine does not inherit CrossOver's `ROSETTA_ADVERTISE_AVX=1` wrapper setting. The handoff now sets it and the runtime probe reports CPUID flags.
4. **Unsafe/inaccurate application-test helper.** `applytest.sh` reset tracked files and deleted untracked files in its input tree; after a failed patch it also discarded earlier patches, invalidating sequential results. Replaced it with isolated snapshots that stop at the first failure. Source worktrees are preserved.
5. **Weak helper-test provenance/coverage.** Earlier tests compiled saved extracted helpers. The new verifier extracts the helpers from freshly applied source, confirms the old copies match, checks truncated reads/all ModRM values/prefixes/length limits and rejected instructions, and compiles with warnings as errors. The original trace stub itself caused a warning; corrected the harness.
6. **MoltenVK confidence was overstated.** All seven MoltenVK patches plus the SPIRV-Cross patch apply sequentially to pinned revisions. This does not establish successful Metal compilation, synchronization correctness, or frame pacing. Patch 0007 can declare presentation complete without a completion callback after one second. Treat it as an experimental workaround. It was not changed during this audit.
7. **MSync conclusion was too strong.** Linux success is not evidence that a particular Mac failure would require MSync. Diagnose failures first. No MSync port is included.

## Reproduced checks

- Wine 11.19 `455e3509b98a6919fd4ad1def4803e08c41c03b2`: both Rosetta patches and the three selected misc patches apply together with default context.
- Wine 11.0 `db11d0fe6a169c457e23d007e20404643d067aa8`: both Rosetta patches apply (second needs `-C1`); all 17 kernel backports and four misc patches apply sequentially within each family.
- Valve Wine `3c1a3fef30942f5655bc7cfd0f4b76a411ea215d`: both Rosetta patches apply with `-C1`; the same 17+4 series apply.
- dw-proton Wine `b585905ec92032840e924e4e2a88382523e92ed3`: both Rosetta patches apply with `-C1`. The old misc/kernel series are not directly replayable because the fork already contains related implementations. Export checks and dispatcher/timing source inspection support omitting those backports; this is not a claim of exact whole-series equivalence.
- MoltenVK `db66022459ffb663aa2b50f6b018bc2e124f5edf` and SPIRV-Cross `6c09849fe88c48eaed08413aa022aaa136a3a057`: 7+1 patches apply, with the SPIRV-Cross revision matching MoltenVK's pin.
- FineWine source/patch provenance: `3d3fbec2b6bb0afb8e3de614ef70acd5eaa7113d`.
- NOP helper: **4,378 checks, zero failures**. XGETBV helper: **11 checks, zero failures**. Compiled directly from applied patch helpers, not an independent reimplementation.
- Added `rosetta_probe.c`: six actual-instruction/Windows-exception checks, all passing on native Windows. These are control results only; the executable still needs patched Wine/Rosetta.
- Bash syntax checked for the handoff and replacement application-test scripts. No original source tree was reset or patched during the audit.

## Scope and limits

The XGETBV masks match the available CrossOver 26.3 source, including `0xe7` on Sequoia and later. The NOP parser checks read lengths, rejects non-/0 opcodes/LOCK and instructions longer than 15 bytes, and preserves RIP on rejection. The Apple invalid-opcode hooks return through `leave_handler`, which restores Wine's macOS thread state; unhandled UD2 remains illegal, and privileged instructions reuse Wine's existing classifier.

The relative-wait patch uses QPC ticks as 100ns units; the pinned Wine implementation provides those units on macOS via its Mach-time conversion. This is source evidence, not a Mac timing measurement. Kernel exports exist upstream but implementations are not identical to dw-proton, and export presence is not proof of ACE behavior. `PsGetProcessExitStatus` remains a known stub.

DXMT's Wine 8+ prerequisite does not guarantee that any prebuilt unixlib works with every Wine build. The handoff requires actual backend-load checks and supports rebuilding against the selected Wine build. No FPS or full-build duration has been measured on an M4 Air here.

Run `python port/verify_port.py` from the original Windows workspace to reproduce the evidence in `port/audit/verification.json`. It may fetch missing Git objects from the pinned source repositories. The JSON includes expected failures for attempts to reapply old backports to newer trees; the required selected handoff recipe must pass. Detailed Mac steps: `MAC_HANDOFF.md`.

## Re-audit 2026-10-16: FineWine moved on (ACE-CORE)

FineWine `cc4cefa` (2026-10-15) adds `patches/stage2-dwproton/ace-core/0001`–`0019` for the game's new `ACE-CORE*.sys` driver. The handoff above was pinned to `3d3fbec` and had none of them. With the current game it would hit `KeBugCheckEx 0x65636157` at `DriverEntry` and not launch. FineWine reports the game as launching and playable with `0001`–`0016`. A remaining `0x65636154` ("Tace") watchdog bugcheck is described there as cosmetic.

Upstream-11.19 port (`acecore/patches`, gated by `acecore/verify_acecore.py`; applied on top of the six base patches it reproduces tree `f9155804`):

| FineWine patch | Port |
|---|---|
| 0001 new imports | Ported. Dropped duplicate x86_64 `KeGetCurrentIrql`. `KeTryToAcquireGuardedMutex` uses CAS (the original's decrement/re-increment can race with a release). |
| 0002 security cookie | Omitted: upstream's `update_security_cookie` is a newer form of the same fix, called from the same place. |
| 0003–0006 | Applied unchanged (`DriverInit` kernel return address, `MmCopyMemory`, service name, `PsGetProcessExitStatus`). |
| 0007 lsass | Re-implemented. Upstream ships a real `lsass.exe` as the demand-start `SamSs` service (exits if launched outside the SCM). wineboot now starts that service. |
| 0008, 0009 | Omitted: identical `get_kernel_obj_list` for tokens/directories already upstream. |
| 0010 MDL | Upstream has the MDL implementations; only the section-view retention (`mapped_views`, `ZwUnmapViewOfSection`) is ported. |
| 0011, 0013 | Omitted: every added line is already present upstream. |
| 0012, 0014, 0015 | Applied (0012 with offsets; checked for duplicates and `#ifdef` placement). |
| 0016 `IoThreadToProcess` | Omitted: upstream implements it (`thread->kthread.process`). |
| 0017–0019 | Not ported: experimental cross-process callbacks FineWine tested as not changing the outcome. |

Corrections to the earlier sections: `PsGetProcessExitStatus` is no longer a stub (`0106`). The `ROSETTA_ADVERTISE_AVX` wrapper claim is not confirmed by the CrossOver 26.3 source, which only mentions the variable in a comment. Setting it remains reasonable. Still unverified: compilation of the ace-core port on Darwin, behaviour without MSync (FineWine always runs with MSync on), and ACE runtime behaviour on upstream Wine.

## Re-audit 2026-10-16: decoder parity, CrossOver Rosetta workarounds, first macOS run

- NOP decoder widened to match hardware and FineWine: `0F 1E` and `0F 1F` with any ModRM.reg, REX anywhere among prefixes. LOCK, over-long (>15 bytes) and truncated forms stay rejected as on hardware (FineWine skips these). 8,735 helper checks pass.
- New `0003` ports CrossOver hacks found by the independent GPT review (`review-gpt/REPORT.md` section 4): 22131, 24256, 24265 (MXCSR reload via a thunk reading the unused `syscall_frame->align[0]`, since upstream does not restore GS before the dispatcher), 24945, 25719, 18947. It is rebased for 11.19 only; on other trees it is informational.
- First Apple Silicon CI run (38845562248, macOS 15.7 arm64): full build, install and wineboot succeeded. `rosetta_skip_long_nop skipped 3-byte NOP` fired, so Rosetta does fault on `0F 1F C1` and the patch handles it. The probe then crashed rewriting its executed RWX page (unhandled write fault), which is exactly CW Hack 24945 and is fixed by `0003`.
- `verify_port.py` now sets `GIT_CEILING_DIRECTORIES`: once the workspace became a git repository, `git apply` inside snapshots silently skipped every hunk. The committed evidence predates this and was valid.
