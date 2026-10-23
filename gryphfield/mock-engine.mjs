#!/usr/bin/env node
// Development stand-in for the bundled engine. It never starts Wine.
import { readFileSync, writeFileSync, existsSync, mkdirSync, unlinkSync, rmSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const demo = process.platform === "win32";
const stateFile = join(root, ".mock-state.json");
const logsDir = join(root, ".mock-logs");
const mockData = join(root, ".mock-data");
const prefix = join(mockData, "prefix");
const launcher = join(prefix, "drive_c", "GRYPHLINK");
const game = join(launcher, "games", "Arknights Endfield");
const initial = { rosetta: false, prefix: false, gryphlink: false, game: false, graphics: "dxmt", running: false, fail: "" };
const state = existsSync(stateFile) ? { ...initial, ...JSON.parse(readFileSync(stateFile, "utf8")) } : { ...initial };
const firstRun = !existsSync(stateFile);
const save = () => writeFileSync(stateFile, JSON.stringify(state, null, 2));
const out = (value) => process.stdout.write(`${JSON.stringify(value)}\n`);
const done = (ok = true, code = "", message = "") => out({ event: "done", ok, ...(ok ? {} : { error: { code, message } }) });
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const cmd = process.argv[2] ?? "status";
const readState = () => JSON.parse(readFileSync(stateFile, "utf8"));
const gamePresent = () => existsSync(join(game, "config.ini")) && existsSync(join(game, "Endfield.exe"));
function setGame(present) {
  state.game = present;
  if (present) {
    mkdirSync(game, { recursive: true });
    writeFileSync(join(game, "config.ini"), "[mock]\n");
    writeFileSync(join(game, "Endfield.exe"), "mock\n");
  } else {
    for (const name of ["config.ini", "Endfield.exe"]) {
      const path = join(game, name);
      if (existsSync(path)) unlinkSync(path);
    }
  }
}
if (demo && firstRun) { setGame(true); save(); }

function status() {
  out({
    event: "status",
    engine: { version: "mock-0.1.0", wine: "11.19", patch_commit: "mock" },
    rosetta: state.rosetta,
    prefix: { path: prefix, ready: state.prefix },
    gryphlink: { path: launcher, installed: state.gryphlink, version: "1.6.0" },
    game: { path: game, installed: gamePresent() },
    running: { wineserver: state.running, gryphlink: state.running && (demo ? state.launch === "run-gryphlink" : cmd === "run-gryphlink"), endfield: state.running && (demo ? state.gameRunning : cmd === "run-direct"), unity: false },
    graphics: existsSync(join(mockData, "config.json")) ? JSON.parse(readFileSync(join(mockData, "config.json"), "utf8")).graphics ?? state.graphics : state.graphics,
  });
}

async function main() {
  if (cmd === "macos-version") {
    process.stdout.write(`${process.env.GRYPHFIELD_DEMO_MACOS || "27.0"}\n`);
    return;
  }
  if (cmd === "mock-reset") {
    // All paths are fixed children of this mock's own directory.
    for (const path of [mockData, logsDir]) rmSync(path, { recursive: true, force: true });
    Object.assign(state, initial, { running: false, gameRunning: false, session: "", fail: "" });
    if (demo) setGame(true);
    save();
    return done();
  }
  if (cmd === "mock-game") {
    setGame(process.argv[3] === "on");
    save();
    status();
    return done();
  }
  if (cmd === "mock-fail") {
    state.fail = process.argv[3] === "off" ? "" : process.argv[3] ?? "";
    save();
    return done();
  }
  if (cmd === "status") {
    status();
    return done();
  }
  if (cmd === "setup") {
    for (const [step, key] of [["rosetta", "rosetta"], ["prefix", "prefix"], ["gryphlink", "gryphlink"]]) {
      out({ event: "step", step, state: state[key] ? "skipped" : "start", message: `${step} setup` });
      await pause(180);
      state[key] = true;
      out({ event: "step", step, state: "done", message: `${step} ready` });
    }
    mkdirSync(launcher, { recursive: true });
    save();
    return done();
  }
  if (cmd === "run-gryphlink" || cmd === "run-direct") {
    if (!state.prefix || !state.gryphlink || (cmd === "run-direct" && !gamePresent())) return done(false, "NOT_READY", "Setup or game files are missing");
    state.running = true;
    state.launch = cmd;
    state.gameRunning = false;
    state.session = `${process.pid}-${Date.now()}`;
    save();
    mkdirSync(logsDir, { recursive: true });
    const log = join(logsDir, "latest-wine.log");
    const sdkLog = join(game, "sdklogs", "OneChannelLC.log");
    if (existsSync(sdkLog)) unlinkSync(sdkLog);
    writeFileSync(log, "Simulated engine launch; no game or Wine is started.\n");
    const active = () => !demo || readState().session === state.session;
    const stopped = () => { out({ event: "game_exited", exit_code: 0, log }); done(); };
    out({ event: "launcher_started" });
    await pause(demo ? 800 : 250);
    if (!active()) return stopped();
    const fail = demo && process.env.GRYPHFIELD_DEMO_FAIL ? process.env.GRYPHFIELD_DEMO_FAIL : state.fail;
    state.fail = fail === "off" ? "" : fail;
    if (state.fail) {
      const example = state.fail === "parent" ? 'OneChannelLC: {"code":2003,"errorCode":10100}' :
        state.fail === "ace" ? "err:ntoskrnl:KeBugCheckEx 65636157 0 0 0 0" :
        state.fail === "tace" ? "err:ntoskrnl:KeBugCheckEx 65636154 0 0 0 0" :
        "wine: Unhandled page fault on read access";
      writeFileSync(log, `${example}\n`);
      if (state.fail === "parent") {
        const sdklogs = join(game, "sdklogs");
        mkdirSync(sdklogs, { recursive: true });
        writeFileSync(join(sdklogs, "OneChannelLC.log"), '{"code":2003,"errorCode":10100,"filePath":"mock-parent"}\n');
      }
      out({ event: "failed", log });
      out({ event: "game_exited", exit_code: state.fail === "ace" ? 222 : null, log });
      state.running = false;
      save();
      return done(false, "LAUNCH_FAILED", "Mock launch failed");
    }
    if (cmd === "run-direct" || (demo && gamePresent())) {
      if (cmd === "run-gryphlink") out({ event: "launcher_minimized" });
      state.gameRunning = true;
      save();
      out({ event: "game_started" });
      await pause(demo ? 800 : 250);
      if (!active()) return stopped();
      out({ event: "unity_started" });
      await pause(demo ? 3000 : 250);
      if (!active()) return stopped();
      out({ event: "unity_stable" });
      if (!demo) out({ event: "game_exited", exit_code: 0, log });
    } else {
      out({ event: "launcher_minimized" });
      if (state.game) {
        out({ event: "game_started" });
        out({ event: "unity_started" });
        out({ event: "unity_stable" });
      }
    }
    writeFileSync(log, "Mock launch completed\n");
    if (!demo) state.running = false;
    save();
    return done();
  }
  if (["stop-game", "stop", "kill", "restart"].includes(cmd)) {
    state.running = cmd === "restart";
    state.gameRunning = false;
    state.session = "";
    save();
    return done();
  }
  if (cmd === "uninstall-game") {
    setGame(false);
    save();
    return done();
  }
  if (cmd === "reset-prefix") {
    state.prefix = false;
    state.gryphlink = false;
    state.running = false;
    state.gameRunning = false;
    state.session = "";
    if (!process.argv.includes("--keep-game")) setGame(false);
    save();
    return done();
  }
  if (demo && cmd === "choose-location") {
    out({ event: "location", path: "", message: "Demo uses its local placeholder folder." });
    return done();
  }
  if (demo && cmd === "profile-cpu") {
    mkdirSync(logsDir, { recursive: true });
    const log = join(logsDir, "demo-cpu-profile.txt");
    writeFileSync(log, "Simulated CPU capture. No profiler or game was started.\n");
    out({ event: "profile", log, message: "Simulated CPU capture complete" });
    return done();
  }
  if (cmd === "logs") {
    out({ event: "logs", engine: join(logsDir, "engine.log"), latest_wine: join(logsDir, "latest-wine.log") });
    return done();
  }
  done(false, "UNKNOWN_COMMAND", cmd);
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error}\n`);
  done(false, "MOCK_ERROR", String(error));
  process.exitCode = 1;
});
