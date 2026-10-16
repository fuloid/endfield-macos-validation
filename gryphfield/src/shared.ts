export type EngineEventName = "status" | "step" | "launcher_started" | "launcher_minimized" |
  "game_started" | "unity_started" | "unity_stable" | "game_exited" |
  "failed" | "logs" | "done" | "unknown";

export type EngineEvent = {
  readonly event: EngineEventName;
  readonly ok: boolean;
  readonly rosetta: boolean;
  readonly prefixReady: boolean;
  readonly gryphlinkInstalled: boolean;
  readonly gameInstalled: boolean;
  readonly wineserverRunning: boolean;
  readonly gryphlinkRunning: boolean;
  readonly endfieldRunning: boolean;
  readonly unityRunning: boolean;
  readonly exitCode: number;
  readonly engineVersion: Uint8Array;
  readonly gryphlinkVersion: Uint8Array;
  readonly graphics: Uint8Array;
  readonly prefixPath: Uint8Array;
  readonly gryphlinkPath: Uint8Array;
  readonly gamePath: Uint8Array;
  readonly logPath: Uint8Array;
  readonly step: Uint8Array;
  readonly state: Uint8Array;
  readonly message: Uint8Array;
  readonly errorCode: Uint8Array;
};

export type ParseRequest = { readonly line: Uint8Array };

export type DiagnosticRequest = {
  readonly logPath: Uint8Array;
  readonly gamePath: Uint8Array;
  readonly prefixPath: Uint8Array;
  readonly exitCode: number;
};
export type DiagnosticResult = {
  readonly observed: Uint8Array;
  readonly source: Uint8Array;
  readonly explanation: Uint8Array;
  readonly nextStep: Uint8Array;
};
export type SettingsRequest = { readonly prefixPath: Uint8Array };
export type SettingsWrite = {
  readonly prefixPath: Uint8Array;
  readonly graphics: Uint8Array;
  readonly extraArgs: Uint8Array;
  readonly debugLogging: boolean;
  readonly rosettaAvx: boolean;
};
export type SettingsResult = {
  readonly graphics: Uint8Array;
  readonly extraArgs: Uint8Array;
  readonly debugLogging: boolean;
  readonly rosettaAvx: boolean;
};
