import { closeSync, existsSync, openSync, readSync, readdirSync, statSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import type { DiagnosticRequest, DiagnosticResult } from "../shared.ts";

const decode = (value: Uint8Array) => new TextDecoder().decode(value);
const encode = (value: string) => new TextEncoder().encode(value);
const expand = (value: string) => value.startsWith("~/") ? join(homedir(), value.slice(2)) : value;

function tail(path: string, since: number): string {
  if (!path || !existsSync(path)) return "";
  const stat = statSync(path);
  if (!stat.isFile() || stat.mtimeMs + 5000 < since) return "";
  const max = 512 * 1024;
  const length = Math.min(max, stat.size);
  const bytes = new Uint8Array(length);
  const fd = openSync(path, "r");
  try {
    const count = readSync(fd, bytes, 0, length, stat.size - length);
    return new TextDecoder().decode(bytes.subarray(0, count));
  } finally {
    closeSync(fd);
  }
}

export function diagnose(request: DiagnosticRequest): DiagnosticResult {
  const winePath = expand(decode(request.logPath));
  const gamePath = expand(decode(request.gamePath));
  const prefixPath = expand(decode(request.prefixPath));
  const wineTime = winePath && existsSync(winePath) ? statSync(winePath).mtimeMs : 0;
  const wine = tail(winePath, 0);
  const sdkPath = join(gamePath, "sdklogs", "OneChannelLC.log");
  const sdk = tail(sdkPath, wineTime);
  const usersPath = join(prefixPath, "drive_c", "users");
  let playerPath = "";
  if (existsSync(usersPath)) {
    for (const user of readdirSync(usersPath)) {
      const candidate = join(usersPath, user, "AppData", "LocalLow", "Gryphline", "Endfield", "Player.log");
      if (existsSync(candidate) && (!playerPath || statSync(candidate).mtimeMs > statSync(playerPath).mtimeMs)) playerPath = candidate;
    }
  }
  const player = tail(playerPath, wineTime);
  let observed = request.exitCode >= 0 ? `Exit code ${request.exitCode}` : "No game exit code available";
  let source = winePath || "Engine event";
  let explanation = "The launch ended before the game reached a stable Unity session. The exact cause is unknown.";
  let nextStep = "Open the Wine log and try launching through GRYPHLINK.";

  if ((/errorCode[\"'\s:]+10100/i.test(sdk) && /code[\"'\s:]+2003/i.test(sdk)) || /Not found matched name/i.test(sdk)) {
    observed = "Launch check 2003 / 10100"; source = sdkPath;
    explanation = "The game may not recognize its signed GRYPHLINK parent process.";
    nextStep = "Try Run GRYPHLINK and launch from its Play button.";
  } else if (/meta file not exists.*Launcher\.meta/i.test(sdk)) {
    observed = "Launch check: Launcher.meta missing"; source = sdkPath;
    explanation = "The game may not recognize this as a GRYPHLINK-managed installation.";
    nextStep = "Start GRYPHLINK and let it verify or update the game.";
  } else if (/KeBugCheckEx\s+(?:0x)?65636157/i.test(wine)) {
    observed = "ACE bugcheck 65636157 (Wace)"; source = winePath;
    explanation = "ACE initialization may have rejected the driver entry or missing lsass service.";
    nextStep = "Keep this log and check that the bundled engine is current.";
  } else if (/\(1-1812-1020\)/.test(wine + sdk + player)) {
    observed = "ACE Center (1-1812-1020)";
    explanation = "An ACE security component may have hit a memory mapping error.";
    nextStep = "Keep the logs and try the latest bundled engine.";
  } else if (/\(1-1720-1000015145\)/.test(wine + sdk + player)) {
    observed = "ACE Center (1-1720-1000015145)";
    explanation = "The ACE driver may have failed to load.";
    nextStep = "Keep the logs and check the bundled engine version.";
  } else if (/\(1-1705-0\)/.test(wine + sdk + player)) {
    observed = "ACE Center (1-1705-0)";
    explanation = "The ACE service may have stopped or timed out.";
    nextStep = "Retry once without debug logging, then inspect the Wine log.";
  } else if (/\(1-\d+-\d+\)/.test(wine + sdk + player)) {
    observed = (wine + sdk + player).match(/\(1-\d+-\d+\)/)?.[0] ?? "ACE Center code";
    explanation = "An ACE Center dialog code appeared. This code is not in the current reference table.";
    nextStep = "Keep the exact code and launch logs for diagnosis.";
  } else if (/Unhandled page fault|Unhandled exception/i.test(wine)) {
    observed = "Unhandled Wine exception"; source = winePath;
    explanation = "Wine or the game appears to have crashed. The faulting module in the log may narrow it down.";
    nextStep = "Open the Wine log and look at the lines around the first unhandled exception.";
  } else if (/unimplemented function/i.test(wine)) {
    observed = "Unimplemented Wine function"; source = winePath;
    explanation = "The bundled Wine may lack a function this game version needs.";
    nextStep = "Keep the log and check for an engine update.";
  } else if (/Failed to create internal root context|Failed to get a GL context for adapter/i.test(wine)) {
    observed = "Graphics context failed"; source = winePath;
    explanation = "The selected graphics mode may be unavailable on this Mac.";
    nextStep = "Try another graphics mode in Settings.";
  } else if (/Couldn't start lsass\.exe|Couldn't open SamSs service/i.test(wine)) {
    observed = "ACE service dependency did not start"; source = winePath;
    explanation = "The lsass service may be unavailable, which can prevent ACE initialization.";
    nextStep = "Keep the Wine log and check the bundled engine version.";
  } else if (/CreateWindow failed with error 1411/i.test(wine)) {
    observed = "Window initialization error 1411"; source = winePath;
    explanation = "The game may have failed while creating its window or COM context.";
    nextStep = "Try another graphics mode and keep the Wine log.";
  } else if (/c00000fd/i.test(wine)) {
    observed = "Windows exception c00000fd"; source = winePath;
    explanation = "A stack overflow may have occurred during early startup.";
    nextStep = "Keep the log and check for an engine update.";
  } else if (/c0000135/i.test(wine)) {
    observed = "Windows exception c0000135"; source = winePath;
    explanation = "A DLL may be missing. This code can also occur harmlessly during prefix setup.";
    nextStep = "Check the surrounding Wine log lines for the missing component.";
  } else if (request.exitCode === 222) {
    observed = "Game exit code 222"; source = "Endfield.exe exit event";
    explanation = "This has been reported with an unsupported ACE driver, but other causes are possible.";
    nextStep = "Check the Wine and launch-check logs before changing anything.";
  } else if (/KeBugCheckEx\s+(?:0x)?65636154/i.test(wine)) {
    observed += "; ACE 65636154 (Tace) also logged";
    explanation = "Tace and service exit 1067 can be informational when the game keeps running; this launch ended for an unknown reason.";
  }
  if (player && /Initialize engine version/i.test(player)) {
    explanation += " Unity initialization was recorded in Player.log.";
  }
  return { observed: encode(observed), source: encode(source), explanation: encode(explanation), nextStep: encode(nextStep) };
}
