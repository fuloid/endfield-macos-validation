import assert from "node:assert/strict";
import test from "node:test";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { load, save } from "../src/services/settings.ts";

const bytes = (s: string) => new TextEncoder().encode(s);
const text = (s: Uint8Array) => new TextDecoder().decode(s);

test("D3DMetal and independent HUD/log preferences survive save and reload", () => {
  const dir = mkdtempSync(join(tmpdir(), "gryphfield-settings-"));
  try {
    const prefixPath = bytes(join(dir, "prefix"));
    const path = join(dir, "config.json");
    writeFileSync(path, JSON.stringify({ library_dir: "/Volumes/Game Library", unknown: "keep" }));
    const defaults = load({ prefixPath });
    assert.equal(text(defaults.graphics), "dxmt");
    assert.equal(defaults.metalHud, false);
    assert.equal(text(defaults.metalHudLog), "off");
    for (const metalHud of [false, true]) for (const mode of ["off", "1s", "frame"]) {
      save({ ...defaults, prefixPath, graphics: bytes("d3dmetal"), metalHud, metalHudLog: bytes(mode) });
      const loaded = load({ prefixPath });
      assert.equal(text(loaded.graphics), "d3dmetal");
      assert.equal(loaded.metalHud, metalHud);
      assert.equal(text(loaded.metalHudLog), mode);
      const config = JSON.parse(readFileSync(path, "utf8"));
      assert.equal(config.library_dir, "/Volumes/Game Library");
      assert.equal(config.unknown, "keep");
    }
    save({ ...defaults, prefixPath, metalHudLog: bytes("invalid") });
    assert.equal(text(load({ prefixPath }).metalHudLog), "off");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("Vulkan diagnostic booleans default safely and persist independently", () => {
  const dir = mkdtempSync(join(tmpdir(), "gryphfield-vulkan-settings-"));
  try {
    const prefixPath = bytes(join(dir, "prefix"));
    const path = join(dir, "config.json");
    const defaults = load({ prefixPath });
    assert.equal(defaults.mvkDebug, false);
    assert.equal(defaults.mvkBackgroundCompilation, true);
    writeFileSync(path, JSON.stringify({ library_dir: "/Volumes/Game Library", unknown: "keep" }));
    // Preferences survive a graphics-mode change; only the engine limits their application.
    for (const graphics of ["vulkan", "dxmt"]) {
      for (const mvkDebug of [false, true]) for (const mvkBackgroundCompilation of [false, true]) {
        const saved = save({ ...defaults, prefixPath, graphics: bytes(graphics), mvkDebug, mvkBackgroundCompilation });
        const loaded = load({ prefixPath });
        assert.equal(saved.mvkDebug, mvkDebug);
        assert.equal(saved.mvkBackgroundCompilation, mvkBackgroundCompilation);
        assert.equal(loaded.mvkDebug, mvkDebug);
        assert.equal(loaded.mvkBackgroundCompilation, mvkBackgroundCompilation);
        const config = JSON.parse(readFileSync(path, "utf8"));
        assert.equal(config.mvk_debug, mvkDebug);
        assert.equal(config.mvk_background_compilation, mvkBackgroundCompilation);
        assert.equal(config.graphics, graphics);
        assert.equal(config.library_dir, "/Volumes/Game Library");
        assert.equal(config.unknown, "keep");
      }
    }
    // Strings/numbers do not accidentally enable debugging or disable normal compilation.
    writeFileSync(path, JSON.stringify({ mvk_debug: "true", mvk_background_compilation: 0 }));
    assert.equal(load({ prefixPath }).mvkDebug, false);
    assert.equal(load({ prefixPath }).mvkBackgroundCompilation, true);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
