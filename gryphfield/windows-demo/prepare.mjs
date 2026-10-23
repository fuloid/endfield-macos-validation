// Explicit Windows-only build overlay. The production core, view and manifest stay untouched.
import { cpSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

if (process.platform !== "win32") throw new Error("The demo overlay is only for Windows.");
const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const output = resolve(process.argv[2] ?? "D:/Jimu/endfield-dev/gryphfield-windows-demo");
const stage = join(root, ".windows-demo", "app");
mkdirSync(stage, { recursive: true });
mkdirSync(output, { recursive: true });
for (const name of ["src", "assets", "package.json", "tsconfig.json"]) cpSync(join(root, name), join(stage, name), { recursive: true });
const manifest = JSON.parse(readFileSync(join(root, "app.json"), "utf8"));
manifest.id = "dev.native_sdk.gryphfield.windows_demo";
manifest.name = "gryphfield-demo";
manifest.display_name = "Gryphfield Windows Demo";
manifest.description = "Gryphfield UI with a simulated engine; no Wine or game is launched.";
manifest.platforms = ["windows"];
manifest.shell.windows[0].title = "Gryphfield - Windows Demo";
// In SDK 0.11.0 the manifest's "metal" request maps to Direct2D on Windows.
// Its Windows boot image loader still reads from cwd; bind to packaged files
// so Explorer, the shortcut, and launches from other working folders agree.
for (const image of manifest.assets.images) image.path = join(output, "resources", basename(image.path)).replaceAll("\\", "/");
writeFileSync(join(stage, "app.json"), `${JSON.stringify(manifest, null, 2)}\n`);
let core = readFileSync(join(root, "src/core.ts"), "utf8");
function replaceOnce(before, after) {
  if (core.split(before).length !== 2) throw new Error(`Demo overlay anchor changed: ${before}`);
  core = core.replace(before, after);
}
const bytes = (path) => `asciiBytes(${JSON.stringify(path.replaceAll("\\", "/"))})`;
replaceOnce('modal: "none", mock: false,', 'modal: "none", mock: true,');
replaceOnce('runner: asciiBytes("/bin/bash"), script: asciiBytes("/Applications/Gryphfield.app/Contents/Resources/engine/bin/gryphfield-engine"),',
  `runner: ${bytes(process.execPath)}, script: ${bytes(join(output, "mock-engine.mjs"))},`);
replaceOnce('Cmd.spawn([asciiBytes("/usr/bin/sw_vers"), asciiBytes("-productVersion")]',
  'Cmd.spawn([model.runner, model.script, asciiBytes("macos-version")]');
writeFileSync(join(stage, "src/core.ts"), core);
cpSync(join(root, "mock-engine.mjs"), join(output, "mock-engine.mjs"));
cpSync(join(root, "windows-demo", "START-HERE.txt"), join(output, "START-HERE.txt"));
console.log(`Windows demo overlay: ${stage}\nRuntime and data: ${output}\nNode: ${process.execPath}`);
