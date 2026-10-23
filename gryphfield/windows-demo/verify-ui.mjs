// Live packaged-app smoke check. Close other demo windows before running.
// Resets ONLY the selected demo folder's mock data. Build first.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

if (process.platform !== "win32") throw new Error("This smoke check is only for Windows.");
const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const output = resolve(process.argv[2] ?? "D:/Jimu/gryphfield-windows-demo");
const cli = join(root, ".windows-demo/app/.native/sdk/bin/native.js");
const env = { ...process.env, GRYPHFIELD_DEMO_FAIL: "" };
const screenshots = join(output, "screenshots");
mkdirSync(screenshots, { recursive: true });
let app;
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
function run(program, args) {
  const result = spawnSync(program, args, { cwd: output, env, encoding: "utf8", windowsHide: true });
  assert.equal(result.status, 0, result.stderr || result.stdout);
  return result.stdout;
}
const native = (...args) => run(process.execPath, [cli, "automate", ...args]);
const mock = (...args) => run(process.execPath, [join(output, "mock-engine.mjs"), ...args]);
const state = () => JSON.parse(readFileSync(join(output, ".mock-state.json"), "utf8"));
const snapshot = () => native("snapshot");
async function waitFor(predicate, description) {
  const until = Date.now() + 15000;
  while (Date.now() < until) {
    try { if (predicate()) return; } catch {}
    await pause(150);
  }
  throw new Error(`Timed out: ${description}`);
}
const expect = text => waitFor(() => snapshot().includes(text), text);
function widget(name, role = "button") {
  const lines = snapshot().split("\n").filter(line => line.includes(`role=${role} name=${JSON.stringify(name)} `));
  assert.ok(lines.length > 0, `${role} ${name}`);
  // The session card and sidebar both expose Force stop game; prefer sidebar.
  const line = lines.at(-1);
  assert.ok(line.includes("enabled=true"), `${name} is enabled`);
  return line.match(/main-canvas#([0-9]+)/)[1];
}
async function click(name, role) {
  native("widget-action", "main-canvas", widget(name, role), "press");
  await pause(250);
}
function shot(name) {
  native("screenshot", "main-canvas");
  copyFileSync(join(output, ".zig-cache/native-sdk-automation/screenshot-main-canvas.png"), join(screenshots, `${name}.png`));
  writeFileSync(join(screenshots, `${name}.txt`), snapshot());
}
async function launch(version) {
  app = spawn(join(output, "bin/gryphfield-demo.exe"), [], { cwd: output, env: { ...env, GRYPHFIELD_DEMO_MACOS: version }, stdio: "ignore" });
  await waitFor(() => snapshot().includes(`publisher_pid=${app.pid} `), "window opened");
  await expect("mock-0.1.0");
  await pause(1200);
}
async function close() {
  if (!app) return;
  // Gracefully close only the window created by this verifier.
  run("powershell.exe", ["-NoProfile", "-Command", `$p = Get-Process -Id ${app.pid}; $null = $p.CloseMainWindow(); if (-not $p.WaitForExit(5000)) { exit 1 }`]);
  app = undefined;
}
async function setup() {
  const scroll = snapshot().split("\n").find(line => line.includes("scroll=[") && line.includes("bounds=(0,"));
  if (scroll) native("widget-wheel", "main-canvas", scroll.match(/main-canvas#([0-9]+)/)[1], "150");
  await click("Set up Gryphfield");
  await expect("Ready to launch");
}
try {
  mock("mock-reset");
  await launch("27.0");
  shot("01-setup");
  await setup();
  shot("02-ready");
  await click("Settings");
  for (const [label, graphics] of [["DXMT", "dxmt"], ["WineD3D", "wined3d"], ["Vulkan", "vulkan"], ["D3DMetal", "d3dmetal"]]) {
    await click(label, "tab");
    // DXMT is already selected on first run; select another mode first.
    if (label === "DXMT") { await click("Vulkan", "tab"); await click("DXMT", "tab"); }
    await click("Save settings");
    await waitFor(() => JSON.parse(readFileSync(join(output, ".mock-data/config.json"), "utf8")).graphics === graphics, `saved ${graphics}`);
    await waitFor(() => /name="Save settings"[^\n]*enabled=false/.test(snapshot()), "settings save acknowledged");
  }
  shot("03-settings-d3dmetal");
  await click("Close");
  for (const [button, stop, image] of [["Run Endfield Directly", "Force stop game", "04-direct-stable"], ["Run GRYPHLINK", "Kill Wine", "05-gryphlink-stable"]]) {
    await click(button);
    await expect('name="Stable"');
    await waitFor(() => snapshot().includes('name="Refresh"') && /name="Refresh"[^\n]*enabled=true/.test(snapshot()), "launch settled");
    assert.equal(state().running, true);
    shot(image);
    await click(stop);
    await waitFor(() => !state().running, "session stopped");
    await pause(1400);
  }
  await click("Restart");
  await waitFor(() => state().running, "Wine restarted");
  await pause(1400);
  await click("Stop");
  await waitFor(() => !state().running, "Wine stopped");
  await pause(1400);
  for (const [failure, observed] of [["parent", "Launch check 2003 / 10100"], ["ace", "ACE bugcheck"], ["tace", "Tace"], ["other", "Unhandled Wine exception"]]) {
    mock("mock-fail", failure);
    await click("Run Endfield Directly");
    await expect("Launch or engine error");
    await expect(observed);
    shot(`06-failure-${failure}`);
    await click("Close");
  }
  mock("mock-fail", "off");
  await click("Uninstall game...");
  await expect("Uninstall Endfield?");
  await click("Continue");
  await expect("Confirm game removal");
  shot("07-uninstall-confirmation");
  await click("Uninstall game");
  await waitFor(() => /name="Run Endfield Directly"[^\n]*enabled=false/.test(snapshot()), "direct launch disabled after uninstall");
  shot("08-game-missing");
  mock("mock-game", "on");
  await click("Refresh");
  await waitFor(() => /name="Run Endfield Directly"[^\n]*enabled=true/.test(snapshot()), "direct launch reenabled");
  await pause(1200);
  await click("Reset prefix...");
  await expect("Reset Wine prefix?");
  await click("Continue");
  await expect("Confirm prefix reset");
  shot("09-reset-confirmation");
  await click("Reset prefix");
  await expect("Set up Gryphfield");
  assert.equal(state().game, true);
  await pause(1200);
  await setup();
  await close();
  await launch("26.3");
  await click("Settings");
  await click("D3DMetal", "tab");
  await expect("D3DMetal isn't available on this Mac");
  await expect("This Mac is running macOS 26.3");
  shot("10-d3dmetal-unavailable");
  await click("OK");
  await click("Close");
  assert.match(snapshot(), /dispatch_errors=0/);
  await close();
  mock("mock-reset");
  console.log("PASS: packaged window, setup, all graphics saves, both stable launches, stop/restart/kill, four failure dialogs, uninstall/reset confirmations, missing game, and macOS 26.3 D3DMetal gate. Demo reset for the user's first run.");
} finally { await close(); }
