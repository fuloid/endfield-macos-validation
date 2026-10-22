# Vulkan lighting-loss investigation (2026-10-23)

Delivery **(b): diagnostics**, not an established rendering fix. The Apple M4
screenshot shows near-black scenery with visible characters, floor lighting and UI;
it cannot identify the failed pass or resource. No game or Metal execution was
possible on Windows. No CI was started.

Sources checked: MoltenVK v1.4.2, commit
`db66022459ffb663aa2b50f6b018bc2e124f5edf`, and its pinned SPIRV-Cross
`6c09849fe88c48eaed08413aa022aaa136a3a057`, cloned outside the repository.
All seven MoltenVK patches and the SPIRV-Cross patch passed sequential
`git apply --check` and application against those sources.

## Ranked candidates and separating evidence

All connections to this game's lighting are **hypotheses**, not confirmed causes.

1. **A failed shader/pipeline compilation retained for the session.** This has the
   most direct code explanation for persistent missing rendering: patch
   [0004:797](patches/moltenvk/0004-defer-cached-shader-libraries-and-replay-recorded-pipelines.patch#L797)
   clears `_needsCompile` even when compilation leaves `_mtlLibrary` nil;
   [0004:729](patches/moltenvk/0004-defer-cached-shader-libraries-and-replay-recorded-pipelines.patch#L729)
   then returns a null function. Upstream also makes pipeline invalidity sticky
   ([MVKPipeline.mm:1004](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm#L1004))
   and skips draws for invalid pipelines
   ([MVKCmdDraw.mm:218](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/Commands/MVKCmdDraw.mm#L218)).
   Compiler failures/timeouts are logged at
   [MVKSync.mm:459](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/GPUObjects/MVKSync.mm#L459).
   Patch 0004 adds four background workers by default (patch:211); compilation
   contention is a hypothesis worth comparing with workers disabled. A compiler
   error immediately before the visual change supports this candidate. The game
   might handle pipeline-creation failures differently; its behavior is unknown.
2. **A synchronization/resource-declaration problem in the hazard-tracking path.**
   [0002:105](patches/moltenvk/0002-use-metal-hazard-tracking-instead-of-barrier-fences.patch#L105)
   makes `usesBarrierFences()` always false; patch:10 disables the non-render
   barrier encoder boundary, and patch:150 enables argument-buffer `useResource`
   declarations instead. This changes dependencies, but no missing declaration
   or faulty lighting resource was established. Upstream uses tracked heaps
   ([MVKDeviceMemory.mm:226](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/GPUObjects/MVKDeviceMemory.mm#L226))
   and normally serial compute dispatches
   ([MVKCommandBuffer.mm:1079](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/Commands/MVKCommandBuffer.mm#L1079));
   removing fences alone is not proof of a bug. Compare argument buffers off,
   then stock MoltenVK. Neither comparison isolates patch 0002 by itself.
3. **A Metal GPU fault/device loss.** Upstream
   [MVKQueue.mm:220](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/GPUObjects/MVKQueue.mm#L220)
   reports timeouts, page faults, OOM and other command-buffer failures. With the
   default `MVK_CONFIG_RESUME_LOST_DEVICE=0`
   ([MVKEnvironment.h:294](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/MoltenVK/MoltenVK/Utility/MVKEnvironment.h#L294)),
   it marks the device lost; further submissions are rejected (MVKQueue.mm:79).
   This fits continuing partial rendering less directly. If resume is enabled,
   local faults can log `Resumed VkDevice` and allow later submissions. Debug mode
   requests Metal encoder execution status (MVKQueue.mm:172), logged at :263.
   The game's response or device recreation cannot be established from this repo.
4. **Lower-evidence patch/translation candidates.** Patch 0006:24 resets resource
   generations and handles wraparound; no skipped-use defect was found. Patch
   0007:62 synthesizes presentation completion after a grace period; this affects
   presentation waiting, not shader/light resource contents. Patch 0005:29 adds
   position invariance; SPIRV-Cross patch:13 adds device-scope fences only for
   applicable control barriers and MSL 3.2+. No incorrect transformation was
   established in either. Stock comparison changes all these paths together;
   improvement is evidence for further isolation, not proof of one culprit.

## Tester procedure (physical Mac, no debugger)

Use the matching updated engine and UI. Under **Settings > Graphics diagnostics**:

- **Vulkan debug logging** maps to `mvk_debug` (boolean, default `false`).
  Help: "Useful for debugging Vulkan rendering issues, such as flickering,
  missing lighting, or broken scenes. Only applies in Vulkan mode. Restart the
  game to apply. Creates larger Wine logs and may reduce performance."
- **Background shader compilation** maps to `mvk_background_compilation`
  (boolean, default `true`). Help: "Useful for debugging Vulkan rendering issues,
  such as flickering, missing lighting, or broken scenes. Turn off to compare
  without background shader compilation. Only applies in Vulkan mode. Restart
  the game to apply. May increase loading times and stutter when turned off."

The engine reads `~/Library/Application Support/Gryphfield/config.json` (or
`$GRYPHFIELD_HOME/config.json`). Both new controls only affect Vulkan launches.
Debug mode enables `MVK_CONFIG_DEBUG=1` and at least info-level MoltenVK logging;
it also logs translated shaders, so save logs promptly. Background compilation off
sets `MVK_CONFIG_PIPELINE_CACHE_BACKGROUND_WORKERS=0`, bypassing patch 0004's
cache-load workers/replay. Deferred first-use compilation and library sharing
remain enabled; this is not a complete patch-0004 kill switch.

1. Record macOS/game/engine versions, M4 model, graphics settings, scene and time
   of the symptom. Enable Vulkan debug logging and the existing Metal validation
   switch; reproduce with the usual route/settings. Save the screenshot and Wine
   log at the first visual change, then note whether new frames still render.
2. Fully stop the game and launcher between comparisons. Keep the same scene,
   settings and diagnostic logging. Turn background shader compilation off and
   repeat; restore it before the next comparison.
3. Compare the existing `mvk_argument_buffers=false` switch, then restore it and
   compare `moltenvk_stock=true`. Stock must actually be bundled; inspect the
   `# MoltenVK env:` search path. Back up/rename the game's `vulkan_pso_cache.bin`
   before changing compiler/argument-buffer variants, and record cold/warm cache
   status. These comparisons may introduce other rendering or performance issues.
4. Send the saved `~/Library/Logs/Gryphfield/wine-*.log` (or the file reached through
   `latest-wine.log` before the next launch). Include the `# graphics:` and
   `# MoltenVK env:` headers, MoltenVK/GPU/MSL version lines, and the first
   `compile failed`, `Timeout after`, `Unable to convert SPIR-V`,
   `Lost VkDevice`, `Resumed VkDevice`, `MTLCommandBuffer`, `Encoders for`,
   `faulted` or Metal-validation error, with surrounding lines. Report explicitly
   if none occurs. The environment header records launch inputs, not app
   layer-setting overrides or proof of which dylib was loaded.

Triggers, frequency, affected scenes, original graphics settings and whether the
default DX11/DXMT mode also reproduces all still require user testing. No code
here can answer those questions or prove that the game recovers from device loss.

Risks: diagnostic logging/validation can slow rendering and change timing; debug
logs can be large, and disabling workers can increase first-use stutter. Defaults
retain normal rendering behavior. Windows checks cannot validate the Apple
compiler, M4 GPU behavior, or a real-game reproduction.
