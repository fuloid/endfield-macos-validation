import assert from "node:assert/strict";
import test from "node:test";
import { NativeApp, findWidget, type NativeSnapshot } from "@native-sdk/core/testing";
import { serviceBoolBytes, serviceBytes, serviceConcat, serviceEnumBytes, serviceI64Bytes } from "@native-sdk/core";
import { parse } from "../src/services/engine.ts";

const bytes = (text: string) => new TextEncoder().encode(text);
const text = (value: unknown) => new TextDecoder().decode(Uint8Array.from(value as number[]));
const button = (s: NativeSnapshot, name: string) => findWidget(s, { role: "button", name });
const shown = (s: NativeSnapshot) => s.widgets.map(w => `${w.name} ${w.text}`).join(" ").replace(/\s+/g, " ");

// Answer the status parser request at the service wire boundary, as in profile.test.ts.
function eventWire(raw: object): Uint8Array {
  const e = parse({ line: bytes(JSON.stringify(raw)) });
  const names = ["status", "step", "launcher_started", "launcher_minimized", "game_started", "unity_started", "unity_stable", "game_exited", "profile", "failed", "logs", "location", "done", "unknown"];
  return serviceConcat([
    serviceEnumBytes(names.indexOf(e.event)),
    ...[e.ok, e.rosetta, e.prefixReady, e.gryphlinkInstalled, e.gameInstalled, e.wineserverRunning, e.gryphlinkRunning, e.endfieldRunning, e.unityRunning].map(serviceBoolBytes),
    serviceI64Bytes(e.exitCode),
    ...[e.engineVersion, e.wineVersion, e.gryphlinkVersion, e.graphics, e.prefixPath, e.gryphlinkPath, e.gamePath, e.locationPath].map(serviceBytes),
    serviceBoolBytes(e.locationCustom), serviceBoolBytes(e.locationAvailable),
    ...[e.logPath, e.step, e.state, e.message, e.errorCode].map(serviceBytes),
  ]);
}

// Boot like the app does, then answer the host version probe the way `sw_vers -productVersion` would.
async function boot(app: NativeApp, version?: string) {
  let s = await app.snapshot();
  s = await app.fireTimer(s.effects.timers[0].key);
  const probe = s.effects.spawns.find(p => text(p.argv[0]) === "/usr/bin/sw_vers")!;
  assert.ok(probe, "boot asks the host for its product version");
  assert.equal(text(probe.argv[1]), "-productVersion");
  if (version === undefined) return app.spawnExit(probe.key, 1, "spawn_failed");
  s = await app.streamLine(probe.key, bytes(version));
  return app.spawnExit(probe.key, 0);
}

// SettingsResult wire format, as in profile.test.ts.
const settingsWire = (graphics: string) => serviceConcat([
  serviceBytes(bytes(graphics)), serviceBytes(bytes("")), serviceBoolBytes(false), serviceBoolBytes(false),
  serviceBytes(bytes("1024")), ...[true, false, true, false, true, true, false, false, false, false].map(serviceBoolBytes),
  serviceBytes(bytes("off")),
]);

async function openSettings(app: NativeApp, s: NativeSnapshot) {
  return app.click(button(s, "Settings"));
}

test("below 26.4, pressing D3DMetal opens the dialog and keeps graphics unchanged", async () => {
  await using app = await NativeApp.start({ width: 1000, height: 1100 });
  let s = await boot(app, "15.6");
  s = await openSettings(app, s);
  const control = s.widgets.find(w => w.name === "D3DMetal")!;
  assert.ok(control.enabled !== false, "the control stays clickable");
  const before = s.model.graphics;
  const saved = s.model.settingsSaved;
  s = await app.click(control);
  assert.equal(s.model.graphics, before);
  assert.equal(s.model.settingsSaved, saved);
  assert.equal(s.model.modal, "d3dmetal");
  const body = shown(s);
  assert.match(body, /D3DMetal isn't available on this Mac/, body);
  assert.match(body, /D3DMetal mode needs macOS 26\.4 or later\. This Mac is running macOS 15\.6\./, body);
  assert.match(body, /Game Porting Toolkit 4/, body);
  assert.match(body, /DXMT is the recommended mode and works on this Mac\./, body);
  assert.ok(button(s, "About Game Porting Toolkit"));
  assert.ok(button(s, "OK"));
  await app.verifyReplay();
});

test("dialog close returns to Settings, not the main window", async () => {
  await using app = await NativeApp.start({ width: 1000, height: 1100 });
  let s = await boot(app, "15.6");
  s = await openSettings(app, s);
  s = await app.click(s.widgets.find(w => w.name === "D3DMetal")!);
  s = await app.click(button(s, "OK"));
  assert.equal(s.model.modal, "settings");
  assert.ok(button(s, "Save settings"));
  assert.equal(s.model.graphics, "dxmt");
  await app.verifyReplay();
});

test("a saved d3dmetal setting is shown and used as dxmt below 26.4, in either arrival order", async () => {
  for (const versionFirst of [true, false]) {
    await using app = await NativeApp.start({ width: 1000, height: 1100 });
    let s = await app.snapshot();
    s = await app.fireTimer(s.effects.timers[0].key);
    const main = s.effects.spawns.find(p => text(p.argv[0]) !== "/usr/bin/sw_vers")!;
    const probe = s.effects.spawns.find(p => text(p.argv[0]) === "/usr/bin/sw_vers")!;
    const version = () => app.streamLine(probe.key, bytes("26.3.1"));
    if (versionFirst) s = await version();
    const status = { event: "status", prefix: { path: "/tmp/prefix", ready: true } };
    s = await app.streamLine(main.key, bytes(JSON.stringify(status)));
    const request = s.effects.requests.find(r => r.name === "engine.parse")!;
    s = await app.respond(request.key, eventWire(status));
    const load = s.effects.requests.find(r => r.name === "settings.load")!;
    s = await app.respond(load.key, settingsWire("d3dmetal"));
    if (!versionFirst) s = await version();
    assert.equal(s.model.graphics, "dxmt");
    assert.equal(s.model.settingsSaved, true, "nothing is rewritten unless the user saves");
    s = await openSettings(app, s);
    assert.equal(s.widgets.find(w => w.name === "D3DMetal")!.selected, false);
    assert.equal(s.widgets.find(w => w.name === "DXMT")!.selected, true);
    await app.verifyReplay();
  }
});

for (const version of ["26.4", "26.4.1", "27.0", "27"]) {
  test(`at macOS ${version}, D3DMetal selects normally`, async () => {
    await using app = await NativeApp.start({ width: 1000, height: 1100 });
    let s = await boot(app, version);
    s = await openSettings(app, s);
    s = await app.click(s.widgets.find(w => w.name === "D3DMetal")!);
    assert.equal(s.model.graphics, "d3dmetal");
    assert.equal(s.model.modal, "settings");
    assert.equal(s.model.settingsSaved, false);
    await app.verifyReplay();
  });
}

test("when the version cannot be detected, D3DMetal is not blocked", async () => {
  for (const version of [undefined, "", "garbage"]) {
    await using app = await NativeApp.start({ width: 1000, height: 1100 });
    let s = await boot(app, version);
    s = await openSettings(app, s);
    s = await app.click(s.widgets.find(w => w.name === "D3DMetal")!);
    assert.equal(s.model.graphics, "d3dmetal");
    assert.equal(s.model.modal, "settings");
  }
});

test("the settings description mentions the macOS requirement", async () => {
  await using app = await NativeApp.start({ width: 1000, height: 1100 });
  const s = await openSettings(app, await app.snapshot());
  assert.match(shown(s), /DXMT and D3DMetal \(Apple's, experimental, macOS 26\.4 or later\) run the game's DirectX 11 renderer\./);
});
