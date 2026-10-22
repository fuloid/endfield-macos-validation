import { Cmd, Sub, asciiBytes } from "@native-sdk/core";
import { applyTextInputEvent, type TextInputEvent, type TextEditState } from "@native-sdk/core/text";
import { diagnosticsDiagnose, engineParse, settingsLoad, settingsSave, settingsSaveLocation } from "@native-sdk/services";
import type { DiagnosticResult, EngineEvent, SettingsResult } from "./shared.ts";

export type Phase = "boot" | "checking" | "setup" | "ready" | "running" | "failure";
export type Action = "none" | "status" | "setup" | "gryphlink" | "direct";
export type Modal = "profile" | "none" | "settings" | "failure" | "uninstall1" | "uninstall2" | "reset1" | "reset2";
export type Graphics = "dxmt" | "d3dmetal" | "wined3d" | "vulkan";
// "second" is stored as "1s" in config.json (an enum member cannot start with a digit)
export type HudLog = "off" | "second" | "frame";
export type Launch = "none" | "gryphlink" | "direct";
export type SetupStep = "none" | "rosetta" | "prefix" | "gryphlink";
export type StepState = "pending" | "active" | "done" | "failed";

export interface Model {
  readonly profileBusy: boolean;
  readonly profileDone: boolean;
  readonly profileOk: boolean;
  readonly profileExited: boolean;
  readonly profileExitCode: number;
  readonly profileError: Uint8Array;
  readonly profileNote: Uint8Array;
  readonly profilePath: Uint8Array;
  readonly phase: Phase;
  readonly action: Action;
  readonly modal: Modal;
  readonly mock: boolean;
  readonly runner: Uint8Array;
  readonly script: Uint8Array;
  readonly doneOk: boolean;
  readonly doneReceived: boolean;
  readonly processExited: boolean;
  readonly rosetta: boolean;
  readonly prefixReady: boolean;
  readonly launcherReady: boolean;
  readonly gameReady: boolean;
  readonly wineRunning: boolean;
  readonly launcherRunning: boolean;
  readonly gameRunning: boolean;
  readonly unityStable: boolean;
  readonly graphics: Graphics;
  readonly engineVersion: Uint8Array;
  readonly prefixPath: Uint8Array;
  readonly launcherPath: Uint8Array;
  readonly gamePath: Uint8Array;
  readonly winePath: Uint8Array;
  readonly logPath: Uint8Array;
  readonly activity: Uint8Array;
  readonly errorCode: Uint8Array;
  readonly errorMessage: Uint8Array;
  readonly exitCode: number;
  readonly gameExitCode: number;
  readonly launchFailed: boolean;
  readonly diagnosticObserved: Uint8Array;
  readonly diagnosticSource: Uint8Array;
  readonly diagnosticExplanation: Uint8Array;
  readonly diagnosticNextStep: Uint8Array;
  readonly argsEdit: TextEditState;
  readonly debugLogging: boolean;
  readonly exceptionTrace: boolean;
  readonly logCapEdit: TextEditState;
  readonly rosettaAvx: boolean;
  readonly stockMoltenvk: boolean;
  readonly mvkArgumentBuffers: boolean;
  readonly mvkDebug: boolean;
  readonly mvkBackgroundCompilation: boolean;
  readonly wined3dCsmt: boolean;
  readonly metalValidation: boolean;
  readonly metalHud: boolean;
  readonly hudLog: HudLog;
  readonly hideWineVersions: boolean;
  readonly discordPresence: boolean;
  readonly settingsSaved: boolean;
  readonly settingsBusy: boolean;
  readonly controlBusy: boolean;
  readonly keepGameOnReset: boolean;
  readonly wineVersion: Uint8Array;
  readonly gryphlinkVersion: Uint8Array;
  readonly gryphlinkIcon: number;
  readonly endfieldIcon: number;
  readonly lastLaunch: Launch;
  readonly sawLauncher: boolean;
  readonly sawGame: boolean;
  readonly sawUnity: boolean;
  readonly setupStep: SetupStep;
  readonly stopRequested: boolean;
  readonly sessionStopped: boolean;
  // Where GRYPHLINK and the game live (config library_dir); empty means inside the prefix.
  readonly locationPath: Uint8Array;
  readonly locationCustom: boolean;
  readonly locationAvailable: boolean;
  readonly locationBusy: boolean;
  readonly locationPending: Uint8Array;
  readonly locationNote: Uint8Array;
}

export type Msg =
  | { readonly kind: "profile_ask" }
  | { readonly kind: "profile_cancel" }
  | { readonly kind: "profile_start" }
  | { readonly kind: "profile_reveal" }
  | { readonly kind: "profile_line"; readonly bytes: Uint8Array }
  | { readonly kind: "profile_parsed"; readonly result: EngineEvent }
  | { readonly kind: "profile_failed"; readonly reason: Uint8Array }
  | { readonly kind: "profile_spawn_failed"; readonly reason: Uint8Array }
  | { readonly kind: "profile_exited"; readonly code: number }
  | { readonly kind: "profile_settle"; readonly at: number }
  | { readonly kind: "boot"; readonly at: number }
  | { readonly kind: "enable_mock" }
  | { readonly kind: "refresh" }
  | { readonly kind: "setup" }
  | { readonly kind: "run_launcher" }
  | { readonly kind: "run_direct" }
  | { readonly kind: "line"; readonly bytes: Uint8Array }
  | { readonly kind: "exited"; readonly code: number }
  | { readonly kind: "settle"; readonly at: number }
  | { readonly kind: "spawn_failed"; readonly reason: Uint8Array }
  | { readonly kind: "parsed"; readonly result: EngineEvent }
  | { readonly kind: "parse_failed"; readonly reason: Uint8Array }
  | { readonly kind: "control_line"; readonly bytes: Uint8Array }
  | { readonly kind: "control_exited"; readonly code: number }
  | { readonly kind: "control_failed"; readonly reason: Uint8Array }
  | { readonly kind: "control_parsed"; readonly result: EngineEvent }
  | { readonly kind: "stop_game" }
  | { readonly kind: "stop_wine" }
  | { readonly kind: "kill_wine" }
  | { readonly kind: "restart" }
  | { readonly kind: "settings_open" }
  | { readonly kind: "modal_close" }
  | { readonly kind: "dxmt" }
  | { readonly kind: "wined3d" }
  | { readonly kind: "vulkan" }
  | { readonly kind: "d3dmetal" }
  | { readonly kind: "toggle_metal_hud" }
  | { readonly kind: "hud_log_off" }
  | { readonly kind: "hud_log_second" }
  | { readonly kind: "hud_log_frame" }
  | { readonly kind: "uninstall_ask" }
  | { readonly kind: "uninstall_next" }
  | { readonly kind: "uninstall_confirm" }
  | { readonly kind: "reset_ask" }
  | { readonly kind: "reset_next" }
  | { readonly kind: "toggle_keep_game" }
  | { readonly kind: "reset_confirm" }
  | { readonly kind: "diagnosed"; readonly result: DiagnosticResult }
  | { readonly kind: "diagnose_failed"; readonly reason: Uint8Array }
  | { readonly kind: "settings_loaded"; readonly result: SettingsResult }
  | { readonly kind: "settings_saved"; readonly result: SettingsResult }
  | { readonly kind: "settings_failed"; readonly reason: Uint8Array }
  | { readonly kind: "args_edit"; readonly edit: TextInputEvent }
  | { readonly kind: "toggle_debug" }
  | { readonly kind: "toggle_exception_trace" }
  | { readonly kind: "log_cap_edit"; readonly edit: TextInputEvent }
  | { readonly kind: "toggle_avx" }
  | { readonly kind: "toggle_stock_mvk" }
  | { readonly kind: "toggle_mvk_argbuf" }
  | { readonly kind: "toggle_mvk_debug" }
  | { readonly kind: "toggle_mvk_background_compilation" }
  | { readonly kind: "toggle_csmt" }
  | { readonly kind: "toggle_metal_validation" }
  | { readonly kind: "toggle_hide_wine_versions" }
  | { readonly kind: "toggle_discord_presence" }
  | { readonly kind: "save_settings" }
  | { readonly kind: "show_prefix" }
  | { readonly kind: "show_launcher" }
  | { readonly kind: "show_game" }
  | { readonly kind: "show_wine" }
  | { readonly kind: "show_log" }
  | { readonly kind: "choose_location" }
  | { readonly kind: "default_location" }
  | { readonly kind: "location_line"; readonly bytes: Uint8Array }
  | { readonly kind: "location_parsed"; readonly result: EngineEvent }
  | { readonly kind: "location_exited"; readonly code: number }
  | { readonly kind: "location_failed"; readonly reason: Uint8Array }
  | { readonly kind: "location_settle"; readonly at: number }
  | { readonly kind: "location_saved"; readonly result: SettingsResult };

export const viewUnbound = ["profile_spawn_failed", "profile_line", "profile_parsed", "profile_failed", "profile_exited", "profile_settle", "boot", "enable_mock", "line", "exited", "settle", "spawn_failed", "parsed", "parse_failed", "control_line", "control_exited", "control_failed", "control_parsed", "diagnosed", "diagnose_failed", "settings_loaded", "settings_saved", "settings_failed", "location_line", "location_parsed", "location_exited", "location_failed", "location_settle", "location_saved"] as const;

export function initialModel(): Model {
  return {
    profileBusy: false, profileDone: false, profileOk: false, profileExited: false, profileExitCode: -1, profileError: asciiBytes(""), profileNote: asciiBytes(""), profilePath: asciiBytes(""),
    phase: "boot", action: "none", modal: "none", mock: false, doneOk: false, doneReceived: false, processExited: false,
    runner: asciiBytes("/bin/bash"), script: asciiBytes("/Applications/Gryphfield.app/Contents/Resources/engine/bin/gryphfield-engine"),
    rosetta: false, prefixReady: false, launcherReady: false, gameReady: false,
    wineRunning: false, launcherRunning: false, gameRunning: false, unityStable: false,
    graphics: "dxmt", engineVersion: asciiBytes(""), prefixPath: asciiBytes(""),
    launcherPath: asciiBytes(""), gamePath: asciiBytes(""), winePath: asciiBytes("/Applications/Gryphfield.app/Contents/Resources/engine/wine"), logPath: asciiBytes(""),
    activity: asciiBytes("Checking bundled engine..."), errorCode: asciiBytes(""),
    errorMessage: asciiBytes(""), exitCode: -1, gameExitCode: -1, launchFailed: false,
    diagnosticObserved: asciiBytes(""), diagnosticSource: asciiBytes(""), diagnosticExplanation: asciiBytes(""), diagnosticNextStep: asciiBytes(""),
    argsEdit: { text: asciiBytes(""), selection: { anchor: 0, focus: 0 }, composition: null },
    debugLogging: false, exceptionTrace: false, logCapEdit: caretAtEnd(asciiBytes("1024")), rosettaAvx: true, stockMoltenvk: false, mvkArgumentBuffers: true, mvkDebug: false, mvkBackgroundCompilation: true, wined3dCsmt: true, metalValidation: false, metalHud: false, hudLog: "off", hideWineVersions: false, discordPresence: false, settingsSaved: true, settingsBusy: false, controlBusy: false, keepGameOnReset: true,
    wineVersion: asciiBytes(""), gryphlinkVersion: asciiBytes(""),
    // Image ids of the static icons declared in app.json `assets.images`.
    gryphlinkIcon: 1, endfieldIcon: 2,
    lastLaunch: "none", sawLauncher: false, sawGame: false, sawUnity: false, setupStep: "none",
    stopRequested: false, sessionStopped: false,
    locationPath: asciiBytes(""), locationCustom: false, locationAvailable: true, locationBusy: false,
    locationPending: asciiBytes(""), locationNote: asciiBytes(""),
  };
}

export function hasProfileNote(model: Model): boolean { return model.profileNote.length > 0; }
export function hasProfilePath(model: Model): boolean { return model.profilePath.length > 0; }
export function ready(model: Model): boolean {
  return model.rosetta && model.prefixReady && model.launcherReady;
}
export function canRunLauncher(model: Model): boolean {
  return ready(model) && model.action === "none" && !model.controlBusy;
}
export function canRunDirect(model: Model): boolean {
  return canRunLauncher(model) && model.gameReady;
}
export function canChangeLocation(model: Model): boolean {
  return model.action === "none" && !model.controlBusy && !model.locationBusy && !model.wineRunning && model.prefixPath.length > 0;
}
export function locationMissing(model: Model): boolean {
  return model.locationCustom && !model.locationAvailable;
}
export function hasLocationNote(model: Model): boolean {
  return model.locationNote.length > 0;
}
export function locationLabel(model: Model): Uint8Array {
  return model.locationCustom ? model.locationPath : asciiBytes("Inside Gryphfield's Wine prefix (default)");
}
export function modeLabel(model: Model): Uint8Array {
  if (model.graphics === "wined3d") return asciiBytes("WineD3D");
  if (model.graphics === "vulkan") return asciiBytes("Vulkan");
  if (model.graphics === "d3dmetal") return asciiBytes("D3DMetal");
  return asciiBytes("DXMT");
}
export function hasLog(model: Model): boolean {
  return model.logPath.length > 0;
}
export function hasErrorCode(model: Model): boolean {
  return model.errorCode.length > 0;
}
function sameAscii(bytes: Uint8Array, value: Uint8Array): boolean {
  if (bytes.length !== value.length) return false;
  for (let i = 0; i < bytes.length; i += 1) if (bytes[i] !== value[i]) return false;
  return true;
}
function allDigits(bytes: Uint8Array): boolean {
  for (let i = 0; i < bytes.length; i += 1) if (bytes[i] < 48 || bytes[i] > 57) return false;
  return true;
}
function caretAtEnd(text: Uint8Array): TextEditState {
  const size = text.length;
  if (!(size >= 0) || size > 2048) return { text, selection: { anchor: 0, focus: 0 }, composition: null };
  const end = Math.trunc(size);
  return { text, selection: { anchor: end, focus: end }, composition: null };
}
function orUnknown(value: Uint8Array): Uint8Array {
  return value.length > 0 ? value : asciiBytes("Unknown");
}

// Display helpers for the view. They only derive from engine-reported state.
export function engineText(model: Model): Uint8Array {
  return orUnknown(model.engineVersion);
}
export function wineText(model: Model): Uint8Array {
  return orUnknown(model.wineVersion);
}
export function gryphlinkText(model: Model): Uint8Array {
  if (!model.launcherReady) return asciiBytes("Not installed");
  return model.gryphlinkVersion.length > 0 ? model.gryphlinkVersion : asciiBytes("Installed");
}
export function sessionLive(model: Model): boolean {
  return model.action === "gryphlink" || model.action === "direct";
}
export function hasSession(model: Model): boolean {
  return model.lastLaunch !== "none";
}
export function sessionFailed(model: Model): boolean {
  return model.lastLaunch !== "none" && !model.sessionStopped && (model.launchFailed || model.gameExitCode > 0 || (model.phase === "failure" && !sessionLive(model)));
}
export function sessionIdle(model: Model): boolean {
  return !sessionLive(model) && !sessionFailed(model) && !model.unityStable && !model.sessionStopped;
}
export function sessionStable(model: Model): boolean {
  return model.unityStable && !sessionLive(model) && !model.sessionStopped;
}
// A session the user stopped (Force stop game, or Stop/Kill/Restart Wine while it ran)
// that ended with exit code 0 or 1 is an intended end, not a launch failure.
function intendedStop(model: Model): boolean {
  if (!model.stopRequested || !sessionLive(model)) return false;
  if (model.gameExitCode !== 0 && model.gameExitCode !== 1) return false;
  return model.doneOk || sameAscii(model.errorCode, asciiBytes("GAME_EXIT_CODE"));
}
// The engine appends " See <log path>" to launch errors; the view offers Open log instead.
function lastMatch(bytes: Uint8Array, needle: Uint8Array): number {
  for (let i = bytes.length - needle.length; i >= 0; i -= 1) {
    let hit = true;
    for (let j = 0; j < needle.length; j += 1) if (bytes[i + j] !== needle[j]) { hit = false; break; }
    if (hit) return i;
  }
  return -1;
}
export function errorText(model: Model): Uint8Array {
  const at = lastMatch(model.errorMessage, asciiBytes(" See "));
  return at > 0 ? model.errorMessage.slice(0, at) : model.errorMessage;
}
// Diagnostic sources are either labels ("Engine event") or log paths; paths stay behind Open log.
export function showDiagnosticSource(model: Model): boolean {
  const source = model.diagnosticSource;
  return source.length > 0 && lastMatch(source, asciiBytes("/")) < 0 && lastMatch(source, asciiBytes("\\")) < 0;
}
export function sessionTitle(model: Model): Uint8Array {
  if (model.lastLaunch === "gryphlink") return asciiBytes("GRYPHLINK session");
  if (model.lastLaunch === "direct") return asciiBytes("Direct launch session");
  return asciiBytes("No session yet");
}
function stepState(model: Model, done: boolean, reached: boolean): StepState {
  if (done) return "done";
  if (!reached) return "pending";
  if (sessionFailed(model)) return "failed";
  return sessionLive(model) ? "active" : "pending";
}
export function stepLauncher(model: Model): StepState {
  return stepState(model, model.sawLauncher, model.lastLaunch === "gryphlink");
}
export function stepGame(model: Model): StepState {
  return stepState(model, model.sawGame, model.lastLaunch === "direct" || model.sawLauncher);
}
export function stepUnity(model: Model): StepState {
  return stepState(model, model.sawUnity, model.sawGame);
}
export function stepStable(model: Model): StepState {
  return stepState(model, model.unityStable, model.sawUnity);
}
function checkState(model: Model, isReady: boolean, step: SetupStep): StepState {
  if (isReady) return "done";
  if (model.setupStep === step && model.action === "setup") return "active";
  return "pending";
}
export function rosettaCheck(model: Model): StepState {
  return checkState(model, model.rosetta, "rosetta");
}
export function prefixCheck(model: Model): StepState {
  return checkState(model, model.prefixReady, "prefix");
}
export function launcherCheck(model: Model): StepState {
  return checkState(model, model.launcherReady, "gryphlink");
}
function setupStepOf(step: Uint8Array): SetupStep {
  if (sameAscii(step, asciiBytes("rosetta"))) return "rosetta";
  if (sameAscii(step, asciiBytes("prefix"))) return "prefix";
  if (sameAscii(step, asciiBytes("gryphlink"))) return "gryphlink";
  return "none";
}
// A setup step event. A finished step marks its checklist row ready; the
// status refresh that ends setup replaces these flags with engine truth.
function applyStep(model: Model, e: EngineEvent): Model {
  const step = setupStepOf(e.step);
  const finished = sameAscii(e.state, asciiBytes("done")) || sameAscii(e.state, asciiBytes("skipped"));
  const next = { ...model, activity: e.message, setupStep: step === "none" ? model.setupStep : step };
  if (!finished || model.action !== "setup") return next;
  if (step === "rosetta") return { ...next, rosetta: true };
  if (step === "prefix") return { ...next, prefixReady: true };
  if (step === "gryphlink") return { ...next, launcherReady: true };
  return next;
}

export function update(model: Model, msg: Msg): Model | [Model, Cmd<Msg>] {
  switch (msg.kind) {
    case "enable_mock":
      return { ...model, mock: true, runner: asciiBytes("node"), script: asciiBytes("mock-engine.mjs"), phase: "boot", action: "none" };
    case "boot":
    case "refresh":
      if (model.action !== "none") return model;
      return [{ ...model, phase: "checking", action: "status", modal: "none", doneOk: false, doneReceived: false, processExited: false }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "setup":
      if (model.action !== "none") return model;
      return [{ ...model, phase: "setup", action: "setup", setupStep: "none", doneOk: false, doneReceived: false, processExited: false, activity: asciiBytes("Preparing Rosetta, Wine and GRYPHLINK...") }, Cmd.spawn([model.runner, model.script, asciiBytes("setup")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "run_launcher":
      if (!canRunLauncher(model)) return model;
      return [{ ...model, phase: "running", action: "gryphlink", lastLaunch: "gryphlink", stopRequested: false, sessionStopped: false, sawLauncher: false, sawGame: false, sawUnity: false, doneOk: false, doneReceived: false, processExited: false, unityStable: false, launchFailed: false, gameExitCode: -1, logPath: asciiBytes(""), diagnosticObserved: asciiBytes(""), diagnosticSource: asciiBytes(""), diagnosticExplanation: asciiBytes(""), diagnosticNextStep: asciiBytes(""), activity: asciiBytes("Starting GRYPHLINK...") }, Cmd.spawn([model.runner, model.script, asciiBytes("run-gryphlink"), asciiBytes("--graphics"), asciiBytes(model.graphics)], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "run_direct":
      if (!canRunDirect(model)) return model;
      return [{ ...model, phase: "running", action: "direct", lastLaunch: "direct", stopRequested: false, sessionStopped: false, sawLauncher: false, sawGame: false, sawUnity: false, doneOk: false, doneReceived: false, processExited: false, unityStable: false, launchFailed: false, gameExitCode: -1, logPath: asciiBytes(""), diagnosticObserved: asciiBytes(""), diagnosticSource: asciiBytes(""), diagnosticExplanation: asciiBytes(""), diagnosticNextStep: asciiBytes(""), activity: asciiBytes("Starting Endfield directly...") }, Cmd.spawn([model.runner, model.script, asciiBytes("run-direct"), asciiBytes("--graphics"), asciiBytes(model.graphics)], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "line":
      return [model, engineParse({ line: msg.bytes }, { ok: "parsed", err: "parse_failed" })];
    case "control_line":
      return [model, engineParse({ line: msg.bytes }, { ok: "control_parsed", err: "parse_failed" })];
    case "parsed": {
      const e = msg.result;
      if (e.event === "status") return [{ ...model, rosetta: e.rosetta, prefixReady: e.prefixReady, launcherReady: e.gryphlinkInstalled, gameReady: e.gameInstalled, wineRunning: e.wineserverRunning, launcherRunning: e.gryphlinkRunning, gameRunning: e.endfieldRunning, engineVersion: e.engineVersion, wineVersion: e.wineVersion, gryphlinkVersion: e.gryphlinkVersion, setupStep: "none", prefixPath: e.prefixPath, launcherPath: e.gryphlinkPath, gamePath: e.gamePath, locationPath: e.locationPath, locationCustom: e.locationCustom, locationAvailable: e.locationAvailable }, settingsLoad({ prefixPath: e.prefixPath }, { ok: "settings_loaded", err: "settings_failed" })];
      if (e.event === "step") return applyStep(model, e);
      if (e.event === "launcher_started") return { ...model, wineRunning: true, launcherRunning: true, sawLauncher: true, activity: asciiBytes("GRYPHLINK started") };
      if (e.event === "launcher_minimized") return { ...model, activity: asciiBytes("GRYPHLINK handed off to Endfield") };
      if (e.event === "game_started") return { ...model, wineRunning: true, gameRunning: true, sawGame: true, activity: asciiBytes("Endfield started") };
      if (e.event === "unity_started") return { ...model, sawUnity: true, activity: asciiBytes("Unity appeared; checking stability...") };
      if (e.event === "unity_stable") return { ...model, unityStable: true, activity: asciiBytes("Unity stayed alive for 30 seconds") };
      if (e.event === "game_exited") return { ...model, gameRunning: false, gameExitCode: e.exitCode, logPath: e.logPath.length ? e.logPath : model.logPath };
      if (e.event === "failed") return { ...model, launchFailed: true, logPath: e.logPath, activity: asciiBytes("Launch failed") };
      if (e.event === "done") return [{ ...model, doneOk: e.ok, doneReceived: true, errorCode: e.errorCode, errorMessage: e.message }, Cmd.delay("settle", 1, "settle")];
      return model;
    }
    case "exited":
      return [{ ...model, processExited: true, exitCode: msg.code }, Cmd.delay("settle", 1000, "settle")];
    case "settle":
      if (!model.processExited) return model;
      if (intendedStop(model)) return [{ ...model, phase: "checking", action: "status", doneOk: false, doneReceived: false, processExited: false, launcherRunning: false, gameRunning: false, launchFailed: false, sessionStopped: true, errorCode: asciiBytes(""), errorMessage: asciiBytes(""), activity: asciiBytes("Game stopped") }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
      if (model.exitCode !== 0 || !model.doneReceived || !model.doneOk || model.launchFailed || model.gameExitCode > 0) {
        const failed = { ...model, phase: "failure" as Phase, action: "none" as Action, modal: "failure" as Modal, errorMessage: model.errorMessage.length ? model.errorMessage : asciiBytes("Engine ended without a successful done event") };
        if (model.action === "direct" || model.action === "gryphlink") return [failed, diagnosticsDiagnose({ logPath: model.logPath, gamePath: model.gamePath, prefixPath: model.prefixPath, exitCode: model.gameExitCode }, { ok: "diagnosed", err: "diagnose_failed" })];
        return failed;
      }
      if (model.action === "setup") return [{ ...model, phase: "checking", action: "status", doneOk: false, doneReceived: false, processExited: false }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
      if (model.action === "status") return { ...model, phase: ready(model) ? "ready" : "setup", action: "none", activity: ready(model) ? asciiBytes("Ready to launch") : asciiBytes("First-run setup is needed") };
      return [{ ...model, phase: "checking", action: "status", doneOk: false, doneReceived: false, processExited: false, launcherRunning: false, gameRunning: false, activity: model.unityStable ? asciiBytes("Game session ended") : asciiBytes("Launcher session ended") }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "spawn_failed":
      return { ...model, phase: "failure", action: "none", modal: "failure", errorCode: asciiBytes("ENGINE_UNAVAILABLE"), errorMessage: model.mock ? asciiBytes("Development mock could not start.") : asciiBytes("Bundled engine unavailable. Put Gryphfield.app in Applications and keep its original name.") };
    case "parse_failed":
      return { ...model, phase: "failure", modal: "failure", errorCode: asciiBytes("ENGINE_PROTOCOL"), errorMessage: msg.reason };
    case "control_parsed":
      return msg.result.event === "done" && !msg.result.ok ? { ...model, modal: "failure", errorCode: msg.result.errorCode, errorMessage: msg.result.message } : model;
    case "control_exited":
      if (msg.code !== 0) return { ...model, controlBusy: false, modal: "failure", errorCode: asciiBytes("CONTROL_FAILED"), errorMessage: asciiBytes("The control command failed") };
      if (model.action !== "none") return { ...model, controlBusy: false };
      return [{ ...model, controlBusy: false, phase: "checking", action: "status", doneOk: false, doneReceived: false, processExited: false }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    case "control_failed":
      return { ...model, controlBusy: false, modal: "failure", errorCode: asciiBytes("CONTROL_FAILED"), errorMessage: msg.reason };
    case "stop_game": if (model.controlBusy || !model.gameRunning) return model; return [{ ...model, controlBusy: true, stopRequested: true }, Cmd.spawn([model.runner, model.script, asciiBytes("stop-game")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "stop_wine": if (model.controlBusy || !model.wineRunning) return model; return [{ ...model, controlBusy: true, stopRequested: model.stopRequested || sessionLive(model) }, Cmd.spawn([model.runner, model.script, asciiBytes("stop")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "kill_wine": if (model.controlBusy || !model.wineRunning) return model; return [{ ...model, controlBusy: true, stopRequested: model.stopRequested || sessionLive(model) }, Cmd.spawn([model.runner, model.script, asciiBytes("kill")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "restart": if (model.controlBusy || !ready(model)) return model; return [{ ...model, controlBusy: true, stopRequested: model.stopRequested || sessionLive(model) }, Cmd.spawn([model.runner, model.script, asciiBytes("restart")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "profile_ask": return model.profileBusy || model.settingsBusy || model.modal !== "settings" ? model : { ...model, modal: "profile" };
    case "profile_cancel": return model.modal === "profile" ? { ...model, modal: "settings" } : model;
    case "profile_start":
      if (model.profileBusy || model.modal !== "profile") return model;
      return [{ ...model, modal: "none", profileBusy: true, profileDone: false, profileOk: false, profileExited: false, profileExitCode: -1, profileError: asciiBytes(""), profilePath: asciiBytes(""), profileNote: asciiBytes("Preparing CPU capture...") }, Cmd.spawn([model.runner, model.script, asciiBytes("profile-cpu")], { key: "profile", line: "profile_line", exit: "profile_exited", err: "profile_spawn_failed" })];
    case "profile_line": return model.profileBusy ? [model, engineParse({ line: msg.bytes }, { ok: "profile_parsed", err: "profile_failed" })] : model;
    case "profile_parsed": {
      const e = msg.result;
      if (!model.profileBusy || model.profileDone || model.profileError.length > 0) return model;
      if (e.event === "profile") return { ...model, profileNote: e.message, profilePath: e.logPath.length ? e.logPath : model.profilePath };
      if (e.event === "done") return { ...model, profileDone: true, profileOk: e.ok, profileNote: e.ok ? asciiBytes("Finishing CPU capture...") : e.message.length ? e.message : asciiBytes("CPU capture failed.") };
      return model;
    }
    // A protocol error does not mean the child stopped. Keep the spawn key busy
    // until exit, and do not let a later success overwrite the error.
    case "profile_failed": return model.profileBusy ? { ...model, profileError: msg.reason.length ? msg.reason : asciiBytes("Could not read the CPU capture result."), profileNote: msg.reason } : model;
    case "profile_spawn_failed": return model.profileBusy ? { ...model, profileBusy: false, profileError: msg.reason, profileNote: msg.reason.length ? msg.reason : asciiBytes("CPU capture could not start.") } : model;
    case "profile_exited":
      if (!model.profileBusy || model.profileExited) return model;
      // Service parse replies may arrive after the process exit notification.
      return [{ ...model, profileExited: true, profileExitCode: msg.code }, Cmd.delay("profile_settle", 1000, "profile_settle")];
    case "profile_settle":
      if (!model.profileBusy || !model.profileExited) return model;
      return { ...model, profileBusy: false, profileNote: model.profileError.length ? model.profileError : model.profileDone && !model.profileOk ? model.profileNote : model.profileExitCode !== 0 ? asciiBytes("CPU capture process failed. Check the engine log.") : !model.profileDone ? asciiBytes("CPU capture ended without a result. Check the engine log.") : model.profileOk ? asciiBytes("CPU profile saved. Reveal in Finder to share the capture folder.") : model.profileNote };
    case "profile_reveal":
      if (!model.profilePath.length) return model;
      return [model, Cmd.revealPath(model.profilePath)];
    case "settings_open": return { ...model, modal: "settings" };
    case "modal_close":
      if (model.settingsBusy) return model;
      return { ...model, modal: "none" };
    case "dxmt": return { ...model, graphics: "dxmt", settingsSaved: false };
    case "wined3d": return { ...model, graphics: "wined3d", settingsSaved: false };
    case "vulkan": return { ...model, graphics: "vulkan", settingsSaved: false };
    case "d3dmetal": return { ...model, graphics: "d3dmetal", settingsSaved: false };
    case "toggle_metal_hud": return { ...model, metalHud: !model.metalHud, settingsSaved: false };
    case "hud_log_off": return { ...model, hudLog: "off", settingsSaved: false };
    case "hud_log_second": return { ...model, hudLog: "second", settingsSaved: false };
    case "hud_log_frame": return { ...model, hudLog: "frame", settingsSaved: false };
    case "uninstall_ask": return model.gameReady && !model.wineRunning && model.action === "none" && !model.controlBusy ? { ...model, modal: "uninstall1" } : model;
    case "uninstall_next": return { ...model, modal: "uninstall2" };
    case "uninstall_confirm": if (model.controlBusy || model.modal !== "uninstall2" || model.wineRunning || model.action !== "none") return model; return [{ ...model, modal: "none", controlBusy: true }, Cmd.spawn([model.runner, model.script, asciiBytes("uninstall-game"), asciiBytes("--yes")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "reset_ask": return !model.wineRunning && model.action === "none" && !model.controlBusy ? { ...model, modal: "reset1" } : model;
    case "reset_next": return { ...model, modal: "reset2" };
    case "toggle_keep_game": return { ...model, keepGameOnReset: !model.keepGameOnReset };
    case "reset_confirm":
      if (model.controlBusy || model.modal !== "reset2" || model.wineRunning || model.action !== "none") return model;
      if (model.keepGameOnReset) return [{ ...model, modal: "none", controlBusy: true }, Cmd.spawn([model.runner, model.script, asciiBytes("reset-prefix"), asciiBytes("--yes"), asciiBytes("--keep-game")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
      return [{ ...model, modal: "none", controlBusy: true }, Cmd.spawn([model.runner, model.script, asciiBytes("reset-prefix"), asciiBytes("--yes")], { key: "control", line: "control_line", exit: "control_exited", err: "control_failed" })];
    case "diagnosed": return { ...model, diagnosticObserved: msg.result.observed, diagnosticSource: msg.result.source, diagnosticExplanation: msg.result.explanation, diagnosticNextStep: msg.result.nextStep };
    case "diagnose_failed": return { ...model, diagnosticExplanation: asciiBytes("Could not read the diagnostic logs."), diagnosticNextStep: msg.reason };
    case "settings_loaded": {
      // Background status refreshes must not replace the user's settings draft,
      // including while the capture confirmation is open or after Start closes it.
      if (!model.settingsSaved) return model;
      const value = msg.result;
      const text = value.extraArgs;
      const size = text.length;
      if (!(size >= 0) || size > 2048) return model;
      const end = Math.trunc(size);
      return { ...model, graphics: sameAscii(value.graphics, asciiBytes("vulkan")) ? "vulkan" : sameAscii(value.graphics, asciiBytes("wined3d")) ? "wined3d" : sameAscii(value.graphics, asciiBytes("d3dmetal")) ? "d3dmetal" : "dxmt", argsEdit: { text, selection: { anchor: end, focus: end }, composition: null }, debugLogging: value.debugLogging, exceptionTrace: value.exceptionTrace, logCapEdit: caretAtEnd(value.logMaxMb), rosettaAvx: value.rosettaAvx, stockMoltenvk: value.stockMoltenvk, mvkArgumentBuffers: value.mvkArgumentBuffers, mvkDebug: value.mvkDebug, mvkBackgroundCompilation: value.mvkBackgroundCompilation, wined3dCsmt: value.wined3dCsmt, metalValidation: value.metalValidation, metalHud: value.metalHud, hudLog: sameAscii(value.metalHudLog, asciiBytes("1s")) ? "second" : sameAscii(value.metalHudLog, asciiBytes("frame")) ? "frame" : "off", hideWineVersions: value.hideWineVersions, discordPresence: value.discordPresence, settingsSaved: true };
    }
    // The service clamps the log cap to 50-5120 MB; show the value it stored.
    case "settings_saved": return { ...model, settingsSaved: true, settingsBusy: false, logCapEdit: caretAtEnd(msg.result.logMaxMb) };
    case "settings_failed": return { ...model, settingsBusy: false, locationBusy: false, modal: "failure", errorCode: asciiBytes("SETTINGS_ERROR"), errorMessage: msg.reason };
    case "args_edit": {
      const edit = applyTextInputEvent(model.argsEdit, msg.edit, 2048);
      return edit ? { ...model, argsEdit: edit, settingsSaved: false } : model;
    }
    case "toggle_debug": return { ...model, debugLogging: !model.debugLogging, settingsSaved: false };
    case "toggle_exception_trace": return { ...model, exceptionTrace: !model.exceptionTrace, settingsSaved: false };
    case "log_cap_edit": {
      const edit = applyTextInputEvent(model.logCapEdit, msg.edit, 4);
      return edit && allDigits(edit.text) ? { ...model, logCapEdit: edit, settingsSaved: false } : model;
    }
    case "toggle_avx": return { ...model, rosettaAvx: !model.rosettaAvx, settingsSaved: false };
    case "toggle_stock_mvk": return { ...model, stockMoltenvk: !model.stockMoltenvk, settingsSaved: false };
    case "toggle_mvk_argbuf": return { ...model, mvkArgumentBuffers: !model.mvkArgumentBuffers, settingsSaved: false };
    case "toggle_mvk_debug": return { ...model, mvkDebug: !model.mvkDebug, settingsSaved: false };
    case "toggle_mvk_background_compilation": return { ...model, mvkBackgroundCompilation: !model.mvkBackgroundCompilation, settingsSaved: false };
    case "toggle_csmt": return { ...model, wined3dCsmt: !model.wined3dCsmt, settingsSaved: false };
    case "toggle_metal_validation": return { ...model, metalValidation: !model.metalValidation, settingsSaved: false };
    case "toggle_hide_wine_versions": return { ...model, hideWineVersions: !model.hideWineVersions, settingsSaved: false };
    case "toggle_discord_presence": return { ...model, discordPresence: !model.discordPresence, settingsSaved: false };
    case "save_settings": if (model.settingsBusy) return model; return [{ ...model, settingsBusy: true }, settingsSave({ prefixPath: model.prefixPath, graphics: asciiBytes(model.graphics), extraArgs: model.argsEdit.text, debugLogging: model.debugLogging, exceptionTrace: model.exceptionTrace, logMaxMb: model.logCapEdit.text, rosettaAvx: model.rosettaAvx, stockMoltenvk: model.stockMoltenvk, mvkArgumentBuffers: model.mvkArgumentBuffers, mvkDebug: model.mvkDebug, mvkBackgroundCompilation: model.mvkBackgroundCompilation, wined3dCsmt: model.wined3dCsmt, metalValidation: model.metalValidation, metalHud: model.metalHud, metalHudLog: asciiBytes(model.hudLog === "second" ? "1s" : model.hudLog), hideWineVersions: model.hideWineVersions, discordPresence: model.discordPresence }, { ok: "settings_saved", err: "settings_failed" })];
    case "show_prefix": return [model, Cmd.revealPath(model.prefixPath)];
    case "show_launcher": return [model, Cmd.revealPath(model.launcherPath)];
    case "show_game": return [model, Cmd.revealPath(model.gamePath)];
    case "show_wine": return [model, Cmd.revealPath(model.winePath)];
    case "show_log": return [model, Cmd.revealPath(model.logPath)];
    // The engine shows the macOS folder picker and checks the folder; the settings service
    // saves it to config.json; a status refresh then shows whether setup is needed there.
    case "choose_location":
      if (!canChangeLocation(model)) return model;
      return [{ ...model, locationBusy: true, locationPending: asciiBytes(""), locationNote: asciiBytes("") }, Cmd.spawn([model.runner, model.script, asciiBytes("choose-location")], { key: "location", line: "location_line", exit: "location_exited", err: "location_failed" })];
    case "default_location":
      if (!canChangeLocation(model)) return model;
      return [{ ...model, locationBusy: true, locationPending: asciiBytes(""), locationNote: asciiBytes("") }, settingsSaveLocation({ prefixPath: model.prefixPath, libraryDir: asciiBytes("") }, { ok: "location_saved", err: "settings_failed" })];
    case "location_line":
      return [model, engineParse({ line: msg.bytes }, { ok: "location_parsed", err: "parse_failed" })];
    case "location_parsed": {
      const e = msg.result;
      if (e.event === "location") return { ...model, locationPending: e.locationPath, locationNote: e.message };
      if (e.event === "done" && !e.ok && !sameAscii(e.errorCode, asciiBytes("CANCELLED"))) return { ...model, modal: "failure", errorCode: e.errorCode, errorMessage: e.message };
      return model;
    }
    case "location_exited":
      // A failed or cancelled picker never saves; its done event already explained why.
      if (msg.code !== 0) return { ...model, locationBusy: false, locationPending: asciiBytes("") };
      return [model, Cmd.delay("location_settle", 500, "location_settle")];
    case "location_settle":
      if (!model.locationBusy) return model;
      if (model.locationPending.length === 0) return { ...model, locationBusy: false };
      return [model, settingsSaveLocation({ prefixPath: model.prefixPath, libraryDir: model.locationPending }, { ok: "location_saved", err: "settings_failed" })];
    case "location_failed":
      return { ...model, locationBusy: false, modal: "failure", errorCode: asciiBytes("LOCATION_PICKER"), errorMessage: msg.reason };
    case "location_saved": {
      const next = { ...model, locationBusy: false, locationPending: asciiBytes("") };
      if (model.action !== "none") return next;
      return [{ ...next, phase: "checking", action: "status", doneOk: false, doneReceived: false, processExited: false }, Cmd.spawn([model.runner, model.script, asciiBytes("status")], { key: "main", line: "line", exit: "exited", err: "spawn_failed" })];
    }
  }
}

export function subscriptions(model: Model): Sub<Msg> {
  return model.phase === "boot" ? Sub.timer("boot", 100, "boot") : Sub.none;
}
