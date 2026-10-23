# Gryphfield UI

Native SDK front end for the private Endfield macOS bundle. The engine thread owns Wine, graphics libraries, GRYPHLINK, the direct-launch helper, packaging, and macOS CI. This directory owns the app UI and its JSON-line protocol adapter. There is no Wine downloader or external launcher archive picker.

## Plan

1. **Bundle and first run:** macOS CI builds `Gryphfield.app`, then copies the engine to `Contents/Resources/engine/`. The user moves the app to Applications and right-clicks Open once. The UI calls `status`; if Rosetta, the prefix, or GRYPHLINK is missing, it shows setup and calls the idempotent `setup` command. The engine owns the admin dialog and all installation work. Setup finishes by refreshing `status`.
2. **Launch:** the ready screen offers GRYPHLINK for login and updates, plus experimental direct launch. The direct button is enabled only when engine `status.game.installed` is true; the engine must implement that as both `config.ini` and `Endfield.exe` under `GRYPHLINK/games/Arknights Endfield`. Launch commands stream events until they end. `unity_stable` is the success marker after 30 seconds.
3. **Controls and settings:** the ready screen is a bottle view: Endfield and GRYPHLINK tiles (icons from the original executables, declared in `app.json` `assets.images`), a launch-status card driven by engine events, and a side panel with bottle info and actions. The side panel sends `stop-game`, `stop`, `restart`, and `kill` to the engine. Settings select DXMT, D3DMetal, WineD3D, or Vulkan; save extra args, detailed logging, and Rosetta AVX preference in `config.json`; reveal read-only paths in Finder. Game removal and prefix reset require two UI confirmations.
4. **Failures:** show raw engine errors and the exact launch log. Inspect that Wine log, recent `sdklogs/OneChannelLC.log`, and recent Unity `Player.log` for known patterns. Explanations are hypotheses. Exit 222 alone is weak evidence; `Tace` / 1067 alone is informational if the game continues.
5. **Integration:** replace the mock with the bundled engine in CI, validate on macOS, then package and sign the app. Do not publish a release until the user approves it.

## Development

The project is pinned to `@native-sdk/core` 0.11.0. On Windows, install Zig 0.16.0 and Node 24, then run:

```sh
npx -y -p @native-sdk/cli@0.11.0 native check
npx -y -p @native-sdk/cli@0.11.0 native test --yes
npx -y -p @native-sdk/cli@0.11.0 native dev --core
```

`mock-engine.mjs` implements the draft JSONL protocol. It never starts Wine. In `native dev --core`, send `{"kind":"enable_mock"}` before `{"kind":"boot","at":0}` to use it. `node mock-engine.mjs mock-game on` creates placeholder `config.ini` and `Endfield.exe` in `.mock-data`, enabling the direct button; `mock-game off` removes them. `node mock-engine.mjs mock-fail parent|ace|tace|other` selects failure examples. State and logs stay in ignored `.mock-*` files.

The actual app invokes `/Applications/Gryphfield.app/Contents/Resources/engine/bin/gryphfield-engine` through `/bin/bash`. Native SDK's TypeScript core currently has no supported API for locating its own `.app` bundle at runtime. This matches the current install instruction but needs a host resource-path hook if users should run the app from elsewhere. The SDK packages ordinary assets under `Contents/Resources/assets/`; the engine needs an explicit CI copy to `Contents/Resources/engine/`.

## Windows UI demo

Run `powershell -ExecutionPolicy Bypass -File windows-demo/build.ps1` on Windows with Node 24 and Zig 0.16.0. The default output is `D:\Jimu\endfield-dev\gryphfield-windows-demo`; double-click `bin\gryphfield-demo.exe` and follow `START-HERE.txt` for game-presence, failure, reset and fake macOS version scenarios. Override `-ZigDirectory` or `-OutputDirectory` when needed. Runtime paths are bound to the output directory and installed Node executable at build time; rebuild to relocate them.

This explicit build creates an ignored `.windows-demo/app` overlay of the same view and services. Only the overlay's manifest and initial engine/probe commands change. The production `app.json`, `src/core.ts`, `src/app.native` and macOS CI remain untouched. The Windows mock uses local placeholders, streams a simulated stable session in about five seconds, and keeps it marked running for stop/restart/kill. No game, Wine, graphics layer or installer runs. Folder selection stays at the demo folder; CPU capture writes an explanatory text file.

The build enables SDK automation for UI verification and supplies a Node load hook only during the build to work around SDK 0.11.0's imports of TypeScript inside `node_modules`. It does not modify the SDK cache. The overlay also binds startup image paths to the packaged resources because SDK 0.11.0 reads them relative to the working directory on Windows. Run `node --test windows-demo/mock.test.mjs` for the mock protocol integration check. With other demo windows closed, `node windows-demo/verify-ui.mjs` opens the packaged app, clicks through the demo, and writes screenshots plus semantic snapshots; it resets the demo's mock data before and after verification.

## Engine contract to settle

- Add `uninstall-game --yes` for the game's folder only; the current draft lists no uninstall command. UI confirms twice before invoking it.
- Add `reset-prefix --yes --keep-game` to preserve the game folder. The UI offers this choice and defaults to preserving files; plain `reset-prefix --yes` removes them.
- Confirm `config.json` keys: `graphics`, `extra_args` (currently a text string), `debug_logging`, `exception_trace` (full `+seh` trace), `log_max_mb` (50-5120, default 1024), `rosetta_avx`, and the graphics diagnostics `moltenvk_stock`, `mvk_argument_buffers`, `wined3d_csmt`, `metal_validation`, and `hide_wine_versions` (Wine's builtin DLLs report no version resource), `discord_presence` (Discord Rich Presence through the engine's Discord bridge), `metal_hud` (Apple's Metal performance HUD) and `metal_hud_log` (`off`, `1s` or `frame`). `graphics` also accepts `d3dmetal` (Apple's D3DMetal, bundled from the Game Porting Toolkit). The engine should parse extra args safely, without shell evaluation. The UI writes these keys and preserves other existing keys.
- Specify whether `game_exited` always includes `log`, and whether `done.ok` becomes false for a game crash. The UI treats `failed`, a positive game exit code, or a failed `done` as errors.
- Keep each JSONL event below Native SDK's 4 KiB process-line limit. Put large diagnostics in files and send paths in events.

The engine thread can adjust this draft before macOS integration. No Wine or graphics internals are implemented in this UI.

## Vulkan rendering diagnostics

Settings > Graphics diagnostics also exposes **Vulkan debug logging** (`mvk_debug`, boolean, default false) and **Background shader compilation** (`mvk_background_compilation`, boolean, default true). Useful for debugging Vulkan rendering issues, such as flickering, missing lighting, or broken scenes. Both only apply in Vulkan mode and need a game restart after saving. Debug logging writes detailed MoltenVK/encoder errors and translated shaders to the Wine log, so logs can grow and performance may drop. Turning background compilation off sets `MVK_CONFIG_PIPELINE_CACHE_BACKGROUND_WORKERS=0`; this disables cache-load workers/pipeline replay but keeps first-use compilation and library sharing. Loading times and stutter may increase. Ship these controls with the engine that reads those exact config keys.

## Native SDK 0.11.0 build note

On this Windows checkout, `native check` works, but the first `native test` needed three local SDK-cache workarounds: a test source referenced by the published `build.zig` was absent from the npm bundle; the vendored `packages/core` needed `npm ci --include=dev`; and Node 24 refused to strip TypeScript from the CLI's `node_modules` directory, so a temporary `tsx` loader was used. These changes are under ignored `.native/` or local `node_modules/`, not part of Gryphfield's source. With those workarounds, the TypeScript core, typed view, model-contract executable, and test executable pass. The command still exits nonzero on Windows because its separate analysis object fails in Zig with `coff does not support linking multiple objects into one`. macOS CI should use a corrected SDK package or apply a pinned, audited workaround before building.
