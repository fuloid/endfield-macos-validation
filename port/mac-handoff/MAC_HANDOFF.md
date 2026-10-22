# Mac handoff: verified locally, experimental on macOS

**Use this handoff instead of the old `LOCAL_RESULTS.md` checklist.** The patch logic is plausible and the local checks pass. No standalone Darwin Wine build, Rosetta runtime, game login, or gameplay has been tested here. This is ready for a Mac build experiment, not a claim that Endfield works.

## Transfer

Copy and extract `mac-handoff.zip` to the Mac. It contains the selected Wine patches (seven base patches plus the ten-patch ace-core port), build script, helper tests, a Windows runtime probe and source, verification evidence, licenses, and the optional MoltenVK patch set/script. The source clones and Windows audit snapshots are not needed. Run commands from the extracted directory.

## Baseline and corrections

Start with upstream Wine 11.19 at **455e3509b98a6919fd4ad1def4803e08c41c03b2**. Using one upstream baseline reduces the number of Proton-specific changes to diagnose. It does not prove macOS game compatibility.

Apply exactly these patches in filename order (`build-mac.sh prepare` does this):

- `0001-ntdll-rosetta-long-nop-and-privinstr-upstream.patch`
- `0002-ntdll-rosetta-emulate-xgetbv-upstream.patch`
- `0003-ntdll-rosetta-crossover-workarounds-upstream.patch`: CrossOver 26.3 Rosetta workarounds FineWine gets implicitly (CW HACKs 22131, 24256, 24265, 24945, 25719, 18947): fake success for debug-register writes, live MXCSR in exception contexts and after SIGSYS, retry write/exec faults on executable pages by toggling PROT_EXEC, and retranslate code written cross-process. CI run 38845562248 showed upstream Wine without 24945 crashing when a program rewrites an executed RWX page under Rosetta.
- FineWine misc `0009` and `0010`: the game-specific dispatcher workaround, missing from upstream Wine.
- FineWine misc `0011`: relative waits using QPC, also missing from upstream Wine.
- `0012` (mary-ext `crossover-wine-endfield` `macos/0002`): Darwin's `select()` rejects timeouts over 10^8 seconds, which turned very long sleeps into a busy loop; waits are capped at one day per `select()` and recomputed.
- `0013` (original): winemac.so exports `macdrv_functions`, which DXMT's winemetal looks up to put a Metal view into a window; without it DXMT logs "Failed to create metal view" and draws nothing.
- `0014` (mary-ext `crossover-wine-endfield` `macos/0013`): winemac reuses the window under a stationary cursor for 50 ms instead of asking WindowServer on every mouse-moved event, which cost main-thread CPU during mouse-look with high-rate mice.
- `0015` (original): winemac validates the app-icon list it builds the Dock icon from. In Endfield.exe's process that list arrives corrupted (likely the game's protector, which hooks the thread); a garbage icon size made a memset run off its buffer, the desktop window failed to create, and the game exited before showing a window. Bad entries are now skipped with an `err:` line, and the list is copied off the user stack at once.
- `0016` (original): ntdll restores the TEB GSBASE at the end of `sigsys_handler`. On macOS 14+ a raw `syscall` from Windows code raises SIGSYS; `init_handler` switches GSBASE to the pthread TEB and, unlike the other handlers, this one never switched it back, so the dispatcher read the TEB from `%gs:0x30` as 0 and faulted at `0x320`. The direct syscall returned `c0000005` and Endfield's protector exited with `0xDEADC0DE`. `tools/syscall-probe` reproduces it; CrossOver 26.3 (which never switches GSBASE on macOS) passes it.
- `0017` (CrossOver 26.3 CW Hack 25427): with `CX_HIDE_BUILTIN_VERSION_RESOURCES=1`, `GetFileVersionInfoSizeExW`/`GetFileVersionInfoExW` report no version resource for DLLs with the "Wine builtin DLL" signature, so programs reading Wine's version strings from system DLLs do not find them. Off unless set; the engine sets it with `hide_wine_versions` in config.json.
- `0018` (CrossOver 26.3 CW Hacks 22144, 13438; FineWine `ab8b9e9` stale-link fix): child processes are exec'd through a link named after the exe in `$TMPDIR/winetemp-<loader inode>-.../` (next to an `ntdll.so` symlink), and the loader rewrites `CFBundleName` in its embedded Info.plist, so the Dock, Activity Monitor and the application menu show "Endfield"/"GRYPHLINK" instead of "wine". `wine_info.plist` gets a padding comment for 32 characters.
- `0019` (CrossOver 26.3 CW Hack 25964): winemac puts the largest program icon on a macOS-style rounded square (Apple icon grid, with shadow) for the Dock. The mask is drawn instead of loaded from CrossOver.app.
- `0020` (DWProton `fc17f43`): do not clip cross-process children of layered windows to the parent's restricted surface rectangle. Candidate for missing launcher content; real macOS launcher rendering remains unverified.
- `0021` (Gryphfield, ACE crash at world load): `RegSaveKey` output is padded with blank lines to 0x2000 bytes. ACE-Base64.dll saves `Wow6432Node\CLSID` and reads 0x1000 bytes into the file looking for a binary hive (`hbin`); with a 64-bit-only build the text file is 381 bytes and the read faults. Builds with i386 support (CrossOver) have a file over 4 KB. `load_keys()` skips blank lines, so the loader still accepts the file. Checked by `tools/regsave-probe`. Verified on a tester's Mac (2026-10-21): the game world loads.
- `0022` (Gryphfield, log size): `0001`'s NOP-skip trace moves from the `seh` channel to `rosetta`. VMProtect code runs these NOPs constantly; one `+seh` game log had 212,000 such lines. Use `WINEDEBUG=+seh,+rosetta` to see them.
- `0101`–`0115`: upstream-11.19 port of FineWine's `ace-core` series (FineWine `cc4cefa`, 2026-10-15). The game's late-2026 `ACE-CORE*.sys` driver replaced `ACE-BASE.sys`; without this series it bug-checks at `DriverEntry` (`KeBugCheckEx 0x65636157`) and the game does not launch. Ported: `0001` (new imports), `0003` (kernel return address for `DriverInit`, the entry bugcheck fix), `0004`, `0005`, `0006` (real `PsGetProcessExitStatus`), `0007` (an `lsass.exe` process; here wineboot starts upstream's `SamSs` service), `0010` (section views stay mapped while a locked MDL uses them), `0012`, `0014`, `0015`. Already in upstream 11.19 and omitted: `0002`, `0008`, `0009`, `0011`, `0013`, `0016`. Not ported: `0017`–`0019`, which FineWine tested and found did not change the outcome. Details: `acecore-verification.json` and `AUDIT.md`.

The complete selection applies with normal context to a fresh snapshot and reproduces the reviewed tree exactly. Wine 11.19 already exports the kernel functions covered by the em-backports series; do not reapply those old patches. Export presence and source review do not prove ACE compatibility.

Omit misc `0008`: it targets `EM-Win64-Shipping.exe` checking Linux display drivers, not this Mac path. Omit the CrossOver MSync thread-exit patch: upstream has no MSync header/implementation. The upstream Vulkan loader already handles a missing SONAME define.

FineWine's original `build-wine.sh` defaults to three replacement modules for an existing CrossOver installation, disables fonts/TLS/audio/Vulkan, and is unsuitable as a standalone game runtime. The supplied `build-mac.sh` builds and installs the full tree, requires FreeType/GnuTLS/Vulkan, and retains CoreAudio. It has passed Bash syntax checks and its patch selection has been applied locally; its Darwin commands remain unexecuted.

## Dependencies and build

Use macOS 15 or later, Xcode Command Line Tools (or full Xcode), Rosetta 2, and Intel Homebrew at `/usr/local`. Native ARM libraries from `/opt/homebrew` cannot be linked into x86_64 Wine. Homebrew documents the two installations [coexisting](https://docs.brew.sh/FAQ).

Check prerequisites:

```bash
xcode-select -p
arch -x86_64 /usr/bin/true
arch -x86_64 /usr/local/bin/brew --prefix
```

If Rosetta or Intel Homebrew is absent, install it first using Apple's installer and the [Homebrew installation instructions](https://docs.brew.sh/Installation) in an x86_64 shell. Install the Intel dependencies:

```bash
arch -x86_64 /usr/local/bin/brew install bison mingw-w64 pkgconf freetype gnutls molten-vk
```

Then, from the extracted handoff directory:

```bash
shasum -a 256 -c SHA256SUMS
export WORK_DIR="$PWD/mac-work"
bash build-mac.sh prepare
bash build-mac.sh configure
JOBS=6 bash build-mac.sh build
bash build-mac.sh bundle
```

`bundle` copies FreeType, GnuTLS, MoltenVK and every non-system library they need into `$WORK_DIR/wine-install/lib/external`, points them at each other, ad-hoc signs them, and fails if anything still references Homebrew. Wine loads them by file name, so whoever runs Wine must set `DYLD_FALLBACK_LIBRARY_PATH` to that folder; the installed Wine then runs without Intel Homebrew.

The script creates a new pinned source checkout, applies the selected patches, and installs into `$WORK_DIR/wine-install`. It refuses to overwrite an existing source directory. Logs are in `$WORK_DIR`. Start with six build jobs on the Air; lower this if memory pressure becomes a problem. Previous 2â€“4 hour estimates were not measurements of this full standalone build.

Stop on a failed configure/build and retain the logs. Do not disable GnuTLS, FreeType, Vulkan, or CoreAudio to make a game build appear successful. GStreamer is intentionally disabled in this first recipe; video/cutscene support may require additional dependencies and a later reconfigure in a new build directory. Full Xcode with Metal tools is required for the optional MoltenVK/DXMT source builds. `configure` sets `ac_cv_func_pipe2=no` (mary-ext `aa312528`): with the macOS 27 SDK, ntdll would otherwise link `pipe2` weakly and call it unchecked, crashing at startup on macOS 26 and earlier.

## Test Wine and Rosetta before installing graphics

Set these in the same terminal, before Wine starts. Upstream lacks CrossOver's wrapper; set AVX advertisement explicitly. The probe prints CPUID flags so this can be checked rather than assumed. FineWine's [graphics notes](https://github.com/stoicswe/Endfield_FineWine/blob/3d3fbec2b6bb0afb8e3de614ef70acd5eaa7113d/docs/graphics-performance.md) identify this wrapper setting.

```bash
export WINEBIN="$WORK_DIR/wine-install/bin/wine"
if [ ! -x "$WINEBIN" ]; then
    export WINEBIN="$WORK_DIR/wine-install/bin/wine64"
fi
export WINEPREFIX="$WORK_DIR/test-prefix"
export WINEARCH=win64
export ROSETTA_ADVERTISE_AVX=1
export WINEFSYNC=0
export WINEESYNC=0
export WINENTSYNC=0
export DYLD_FALLBACK_LIBRARY_PATH="$WORK_DIR/wine-install/lib/external:/usr/lib"
file "$WINEBIN"
"$WINEBIN" --version
"$WINEBIN" wineboot -u > "$WORK_DIR/wineboot.log" 2>&1
WINEDEBUG=+seh,+rosetta "$WINEBIN" "$PWD/rosetta_probe.exe" > "$WORK_DIR/rosetta-probe.log" 2>&1
cat "$WORK_DIR/rosetta-probe.log"
```

Start Wine directly as above, not through `arch -x86_64`: Wine is x86_64, so Rosetta runs it anyway, and `/usr/bin/arch` is SIP-protected, so macOS strips `DYLD_*` variables and the bundled libraries would not be found.

Expected: a working x86_64 Wine runtime and **eight passing probe checks**. The probe executes actual machine instructions and handles Windows exceptions; it is separate from the mocked helper tests. It covers five NOP forms (including ENDBR64 and `0F 1F /1`), XGETBV, `mov rax,cr3` yielding `c0000096`, and UD2 yielding `c000001d`. NOP faults should be consumed by Wine, not reach the program's exception handler. It does not load ACE or prove kernel-driver support.

On a Rosetta release that faults on these forms, expect `skipped ...-byte NOP` (`rosetta` channel) and `emulated xgetbv` (`seh` channel) in the trace. If Rosetta executes an instruction itself, no helper trace is expected. Missing trace lines alone are not a failure. For macOS 15+, the emulation returns CrossOver's `0xe7`; that is not proof of AVX-512 support. Apple explicitly [excludes AVX-512](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment).

The supplied probe executable was compiled and passed all eight checks on native Windows as a control. To rebuild it on the Mac:

```bash
/usr/local/bin/x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror rosetta_probe.c -o rosetta_probe.exe
```

Optional host unit tests (these do not test Rosetta):

```bash
mkdir -p helper-tests
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter noptest.c -o helper-tests/noptest
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter xgetbvtest.c -o helper-tests/xgetbvtest
./helper-tests/noptest
./helper-tests/xgetbvtest
```

## Graphics, then the game

DX11 with DXMT is the first graphics experiment. DXMT's [build documentation](https://github.com/3Shain/dxmt/blob/main/docs/DEVELOPMENT.md) accepts Wine 8+ as a prerequisite and supports building against a Wine build directory. That establishes an independent integration path, not guaranteed compatibility with every Wine release or prebuilt unixlib.

Try the project's `dxmt-v0.80-builtin` release with its matching Windows and x86_64 Unix components. Confirm the chosen Wine actually loads DXMT's `d3d11`, `dxgi`, and `winemetal` components using `WINEDEBUG=+loaddll` and DXMT logs. CrossOver's `CX_GRAPHICS_BACKEND=dxmt` setting does not select a backend in upstream Wine. Do not mix CrossOver modules into this Wine installation. If the prebuilt fails to load, rebuild DXMT against this exact Wine tree using its documented `wine_build_path` option. LLVM 15 and Metal tools are needed for that build.

First get a simple D3D11 sample rendering. Then install/update the game normally in this separate test prefix and launch with `-force-d3d11`. For the first failed launch, capture `WINEDEBUG=+seh,+loaddll,+ntoskrnl`; use low logging for gameplay measurements. Check login, reach gameplay, play for at least 30 minutes, change areas, and restart. Record OS version, Wine revision, DXMT version, game version, launch arguments, and logs. A login screen alone is insufficient.

Expected ACE-CORE log lines on a working run, matching FineWine on CrossOver: `err:ntoskrnl:KeBugCheckEx 65636154` ("Tace") about a second after the driver starts, and both ACE-CORE services reported `STOPPED` with exit code `1067`. FineWine reports this as harmless: the user-mode ACE process stays connected and the game plays. `KeBugCheckEx 65636157` ("Wace") instead means the entry check failed, which the ace-core port should prevent. Before launching, confirm `lsass.exe` is running in the prefix (e.g. `"$WINEBIN" tasklist` or `winedbg --command "info proc"`); ACE-CORE fails `DriverInit` without it.

If ACE fails, first diagnose the exact exception, missing export, or service failure. **Failure without MSync does not prove MSync is required.** Porting MSync is a separate project, not an automatic next step.

The optional Vulkan path uses the pinned MoltenVK 1.4.2 + SPIRV-Cross patch set. It has passed application checks, not an Apple compiler/GPU test. The copied `scripts/build-moltenvk.sh` is the original FineWine script; inspect its `BUILD_DIR` before running because its fetch phase replaces build directories. Use a fresh directory. Test this path separately and back up/rename `vulkan_pso_cache.bin` before switching the patched shader compiler.

MoltenVK patch `0007` advances completion when a newer presentation is queued and can synthesize completion after a one-second grace period. This is a behavior-changing workaround that needs presentation/frame-pacing testing; a queued frame alone does not establish display completion under the [Vulkan presentation-wait contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresentKHR.html). The shader barrier patch also needs MSL 3.2+ and GPU validation. Preserve all other recipe dependencies if comparing builds with and without `0007`.

## Evidence and acceptance

`verification.json` records source revisions, individual patch results, compiler identity, input hashes, kernel export checks, and helper/control-test output. Both Rosetta helpers were extracted from the freshly patched source for the tests; the earlier saved copies match exactly. All 8,735 NOP checks and 11 XGETBV checks pass with warnings treated as errors.

The Mac handoff is successful only after a full build, Wine initialization, eight passing runtime probe checks, verified graphics backend loading/rendering, and a stable game session. Remaining uncertainty includes current ACE behavior, graphics integration, service/launcher requirements, and later game updates.
