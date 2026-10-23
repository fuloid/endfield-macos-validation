// Build-only workaround for SDK 0.11.0 importing TS from its npm directory.
// Node's default loader refuses that location. No npm cache files are patched.
import { readFileSync } from "node:fs";
import { registerHooks, stripTypeScriptTypes } from "node:module";
import { fileURLToPath } from "node:url";

registerHooks({
  load(url, context, nextLoad) {
    if (url.startsWith("file:") && url.endsWith(".ts")) {
      return {
        format: "module",
        source: stripTypeScriptTypes(readFileSync(fileURLToPath(url), "utf8"), { mode: "transform", sourceUrl: url }),
        shortCircuit: true,
      };
    }
    return nextLoad(url, context);
  },
});
