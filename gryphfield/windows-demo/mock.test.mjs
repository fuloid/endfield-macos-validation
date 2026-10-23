import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const source = join(dirname(fileURLToPath(import.meta.url)), "..", "mock-engine.mjs");
test("Windows mock: setup, persistent settings, launches, failures, controls and reset", { skip: process.platform !== "win32" }, async () => {
  const root = mkdtempSync(join(tmpdir(), "gryphfield-demo-test-"));
  const script = join(root, "mock-engine.mjs");
  copyFileSync(source, script);
  const env = { ...process.env, GRYPHFIELD_DEMO_FAIL: "", GRYPHFIELD_DEMO_MACOS: "26.3" };
  const run = (...args) => {
    const result = spawnSync(process.execPath, [script, ...args], { env, encoding: "utf8" });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim().split("\n").map(line => JSON.parse(line));
  };
  const status = () => run("status")[0];
  try {
    assert.equal(spawnSync(process.execPath, [script, "macos-version"], { env, encoding: "utf8" }).stdout.trim(), "26.3");
    assert.equal(status().prefix.ready, false);
    assert.equal(status().game.installed, true);
    run("setup");
    assert.equal(status().prefix.ready, true);
    writeFileSync(join(root, ".mock-data", "config.json"), '{"graphics":"d3dmetal","extra_args":"demo"}');
    assert.equal(status().graphics, "d3dmetal");
    for (const command of ["run-direct", "run-gryphlink"]) {
      const events = run(command);
      assert.ok(events.some(e => e.event === "unity_stable"));
      assert.equal(status().running.endfield, true);
      run("stop-game");
      assert.equal(status().running.wineserver, false);
    }
    for (const failure of ["parent", "ace", "tace", "other"]) {
      run("mock-fail", failure);
      const events = run("run-direct");
      assert.ok(events.some(e => e.event === "failed"));
      assert.equal(events.at(-1).ok, false);
      const log = readFileSync(join(root, ".mock-logs", "latest-wine.log"), "utf8");
      assert.match(log, failure === "parent" ? /10100/ : failure === "ace" ? /65636157/ : failure === "tace" ? /65636154/ : /page fault/);
    }
    run("mock-fail", "off");
    // Stop an active stream, rather than only a completed mock session.
    const events = [];
    await new Promise((resolve, reject) => {
      const child = spawn(process.execPath, [script, "run-direct"], { env });
      let pending = "";
      child.stdout.on("data", chunk => {
        pending += chunk;
        const lines = pending.split("\n");
        pending = lines.pop();
        for (const line of lines) {
          const event = JSON.parse(line);
          events.push(event);
          if (event.event === "game_started") run("kill");
        }
      });
      child.once("error", reject);
      child.once("exit", code => code === 0 ? resolve() : reject(new Error(`exit ${code}`)));
    });
    assert.ok(events.some(e => e.event === "game_exited"));
    assert.ok(!events.some(e => e.event === "unity_stable"));
    assert.equal(events.at(-1).ok, true);
    run("restart");
    assert.equal(status().running.wineserver, true);
    run("stop");
    run("mock-game", "off");
    assert.equal(status().game.installed, false);
    assert.equal(run("run-direct").at(-1).ok, false);
    run("mock-game", "on");
    run("reset-prefix", "--yes", "--keep-game");
    assert.equal(status().game.installed, true);
    assert.equal(status().prefix.ready, false);
    run("setup");
    run("uninstall-game", "--yes");
    assert.equal(status().game.installed, false);
    run("mock-reset");
    assert.equal(status().prefix.ready, false);
    assert.equal(status().game.installed, true);
  } finally { rmSync(root, { recursive: true, force: true }); }
});
