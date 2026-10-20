import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import type { LocationWrite, SettingsRequest, SettingsResult, SettingsWrite } from "../shared.ts";

const decode = (value: Uint8Array) => new TextDecoder().decode(value);
const encode = (value: string) => new TextEncoder().encode(value);
function pathFor(prefix: Uint8Array): string {
  const raw = decode(prefix);
  if (!raw) throw { kind: "INVALID_PATH", message: "Prefix path is unavailable" };
  const full = raw.startsWith("~/") ? join(homedir(), raw.slice(2)) : raw;
  return join(dirname(full), "config.json");
}
function rawConfig(path: string): Record<string, unknown> {
  if (!existsSync(path)) return {};
  const value = JSON.parse(readFileSync(path, "utf8"));
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
}
function result(config: Record<string, unknown>): SettingsResult {
  return {
    graphics: encode(["dxmt", "wined3d", "vulkan"].includes(String(config.graphics)) ? String(config.graphics) : "dxmt"),
    extraArgs: encode(typeof config.extra_args === "string" ? config.extra_args.slice(0, 2048) : ""),
    debugLogging: config.debug_logging === true,
    rosettaAvx: config.rosetta_avx !== false,
    stockMoltenvk: config.moltenvk_stock === true,
    mvkArgumentBuffers: config.mvk_argument_buffers !== false,
    wined3dCsmt: config.wined3d_csmt !== false,
    metalValidation: config.metal_validation === true,
    hideWineVersions: config.hide_wine_versions === true,
  };
}
export function load(request: SettingsRequest): SettingsResult {
  return result(rawConfig(pathFor(request.prefixPath)));
}
function write(path: string, config: Record<string, unknown>): void {
  mkdirSync(dirname(path), { recursive: true });
  const temporary = `${path}.gryphfield-ui.tmp`;
  writeFileSync(temporary, `${JSON.stringify(config, null, 2)}\n`, { mode: 0o600 });
  renameSync(temporary, path);
}
// Where GRYPHLINK and the game live: an absolute folder the engine has checked, or "" for the
// default inside the Wine prefix. The engine reads library_dir from this same config.json.
export function saveLocation(request: LocationWrite): SettingsResult {
  const path = pathFor(request.prefixPath);
  const dir = decode(request.libraryDir);
  if (dir !== "" && !dir.startsWith("/")) throw { kind: "INVALID_PATH", message: "The location must be an absolute folder path" };
  const config = rawConfig(path);
  if (dir === "") delete config.library_dir;
  else config.library_dir = dir;
  write(path, config);
  return result(config);
}
export function save(request: SettingsWrite): SettingsResult {
  const path = pathFor(request.prefixPath);
  const graphics = decode(request.graphics);
  if (!["dxmt", "wined3d", "vulkan"].includes(graphics)) throw { kind: "INVALID_MODE", message: "Invalid graphics mode" };
  const config = rawConfig(path);
  config.graphics = graphics;
  config.extra_args = decode(request.extraArgs);
  config.debug_logging = request.debugLogging;
  config.rosetta_avx = request.rosettaAvx;
  config.moltenvk_stock = request.stockMoltenvk;
  config.mvk_argument_buffers = request.mvkArgumentBuffers;
  config.wined3d_csmt = request.wined3dCsmt;
  config.metal_validation = request.metalValidation;
  config.hide_wine_versions = request.hideWineVersions;
  write(path, config);
  return result(config);
}
