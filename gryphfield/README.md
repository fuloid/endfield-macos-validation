# Gryphfield UI

Native SDK front end for the private Endfield macOS bundle. The engine thread owns Wine, graphics libraries, GRYPHLINK, the direct-launch helper, packaging, and macOS CI. This directory owns the app UI and its JSON-line protocol adapter. There is no Wine downloader or external launcher archive picker.

## Plan

1. **Bundle and first run:** macOS CI builds `Gryphfield.app`, then copies the engine to `Contents/Resources/engine/`. The user moves the app to Applications and right-clicks Open once. The UI calls `status`; if Rosetta, the prefix, or GRYPHLINK is missing, it shows setup and calls the idempotent `setup` command. The engine owns the admin dialog and all installation work. Setup finishes by refreshing `status`.
2. **Launch:** the ready screen offers GRYPHLINK for login and updates, plus experimental direct launch. The direct button is enabled only when engine `status.game.installed` is true; the engine must implement that as both `config.ini` and `Endfield.exe` under `GRYPHLINK/games/Arknights Endfield`. Launch commands stream events until they end. `unity_stable` is the success marker after 30 seconds.
3. **Controls and settings:** the bottom bar sends `stop-game`, `stop`, `restart`, and `kill` to the engine. Settings select DXMT, WineD3D, or Vulkan; save extra args, detailed logging, and Rosetta AVX preference in `config.json`; reveal read-only paths in Finder. Game removal and prefix reset require two UI confirmations.
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

## Engine contract to settle

- Add `uninstall-game --yes` for the game's folder only; the current draft lists no uninstall command. UI confirms twice before invoking it.
- Add `reset-prefix --yes --keep-game` to preserve the game folder. The UI offers this choice and defaults to preserving files; plain `reset-prefix --yes` removes them.
- Confirm `config.json` keys: `graphics`, `extra_args` (currently a text string), `debug_logging`, `rosetta_avx`. The engine should parse extra args safely, without shell evaluation. The UI writes these keys and preserves other existing keys.
- Specify whether `game_exited` always includes `log`, and whether `done.ok` becomes false for a game crash. The UI treats `failed`, a positive game exit code, or a failed `done` as errors.
- Keep each JSONL event below Native SDK's 4 KiB process-line limit. Put large diagnostics in files and send paths in events.

The engine thread can adjust this draft before macOS integration. No Wine or graphics internals are implemented in this UI.

## Native SDK 0.11.0 build note

On this Windows checkout, `native check` works, but the first `native test` needed three local SDK-cache workarounds: a test source referenced by the published `build.zig` was absent from the npm bundle; the vendored `packages/core` needed `npm ci --include=dev`; and Node 24 refused to strip TypeScript from the CLI's `node_modules` directory, so a temporary `tsx` loader was used. These changes are under ignored `.native/` or local `node_modules/`, not part of Gryphfield's source. With those workarounds, the TypeScript core, typed view, model-contract executable, and test executable pass. The command still exits nonzero on Windows because its separate analysis object fails in Zig with `coff does not support linking multiple objects into one`. macOS CI should use a corrected SDK package or apply a pinned, audited workaround before building.
