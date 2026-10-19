import type { EngineEvent, ParseRequest } from "../shared.ts";

const bytes = (value: unknown): Uint8Array => new TextEncoder().encode(typeof value === "string" ? value : "");
const flag = (value: unknown): boolean => value === true;
const field = (value: unknown): Record<string, unknown> => value !== null && typeof value === "object" ? value as Record<string, unknown> : {};

export function parse(request: ParseRequest): EngineEvent {
  const raw = field(JSON.parse(new TextDecoder().decode(request.line)));
  const prefix = field(raw.prefix);
  const gryphlink = field(raw.gryphlink);
  const game = field(raw.game);
  const running = field(raw.running);
  const engine = field(raw.engine);
  const error = field(raw.error);
  const location = field(raw.location);
  const valid = ["status", "step", "launcher_started", "launcher_minimized", "game_started", "unity_started", "unity_stable", "game_exited", "failed", "logs", "location", "done"].includes(String(raw.event));
  const event = valid ? raw.event as EngineEvent["event"] : "unknown";
  return {
    event,
    ok: flag(raw.ok),
    rosetta: flag(raw.rosetta),
    prefixReady: flag(prefix.ready),
    gryphlinkInstalled: flag(gryphlink.installed),
    gameInstalled: flag(game.installed),
    wineserverRunning: flag(running.wineserver),
    gryphlinkRunning: flag(running.gryphlink),
    endfieldRunning: flag(running.endfield),
    unityRunning: flag(running.unity),
    exitCode: typeof raw.exit_code === "number" ? raw.exit_code : -1,
    engineVersion: bytes(engine.version),
    wineVersion: bytes(engine.wine),
    gryphlinkVersion: bytes(gryphlink.version),
    graphics: bytes(raw.graphics),
    prefixPath: bytes(prefix.path),
    gryphlinkPath: bytes(gryphlink.path),
    gamePath: bytes(game.path),
    locationPath: bytes(raw.event === "location" ? raw.path : location.path),
    locationCustom: flag(location.custom),
    locationAvailable: location.available !== false,
    logPath: bytes(raw.log ?? raw.latest_wine),
    step: bytes(raw.step),
    state: bytes(raw.state),
    message: bytes(raw.message ?? error.message),
    errorCode: bytes(error.code),
  };
}
