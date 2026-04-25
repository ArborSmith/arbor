import { z } from "zod";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { writeFile, unlink, readFile, access } from "node:fs/promises";
import { executeConsoleCommand } from "../../ue5-client.js";

export const runPythonSchema = {
  code: z
    .string()
    .optional()
    .describe(
      "Inline Python code to execute inside UE5. Must use the `unreal` module. " +
        "Call `_write_result({...})` or `arbor.utils.write_result({...})` to return structured data."
    ),
  script_path: z
    .string()
    .optional()
    .describe(
      "Absolute path to a .py file to execute instead of inline code."
    ),
};

const POLL_INTERVAL_MS = 250;
const POLL_TIMEOUT_MS = 30_000;

function resultFilePath(): string {
  return join(
    tmpdir(),
    `ue5_bridge_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.json`
  ).replace(/\\/g, "/");
}

/**
 * Build a wrapper script that:
 * 1. Sets UE5_BRIDGE_RESULT_PATH env var (so arbor.utils.write_result uses the bridge path)
 * 2. Defines _write_result() and _RESULT_PATH for inline code convenience
 * 3. exec()'s the target script inside try/except
 * 4. On exception, writes error + traceback to the result file
 */
function buildWrapper(resultPath: string, targetScriptPath: string): string {
  return `import json, os, traceback

_RESULT_PATH = r"${resultPath}"
os.environ["UE5_BRIDGE_RESULT_PATH"] = _RESULT_PATH

def _write_result(data):
    with open(_RESULT_PATH, 'w') as f:
        json.dump(data, f)

try:
    _code_path = r"${targetScriptPath}"
    with open(_code_path, 'r', encoding='utf-8') as _f:
        _code = _f.read()
    exec(compile(_code, _code_path, 'exec'))
except Exception as _e:
    import unreal as _unreal
    _tb = traceback.format_exc()
    _unreal.log_error(f"[ue5-bridge] Python error:\\n{_tb}")
    try:
        with open(_RESULT_PATH, 'w') as _rf:
            json.dump({"success": False, "error": str(_e), "traceback": _tb}, _rf)
    except Exception:
        pass
finally:
    os.environ.pop("UE5_BRIDGE_RESULT_PATH", None)
`;
}

export async function runPython(params: {
  code?: string;
  script_path?: string;
}): Promise<{
  success: boolean;
  result?: unknown;
  error?: string;
  traceback?: string;
  note?: string;
  script_path: string;
}> {
  if (!params.code && !params.script_path) {
    throw new Error("Either `code` or `script_path` must be provided.");
  }

  const resultPath = resultFilePath();

  // Delete stale result file
  try {
    await unlink(resultPath);
  } catch {
    // ignore — file may not exist
  }

  const timestamp = Date.now();
  let targetScriptPath: string;

  if (params.code) {
    // Write raw user code to a separate file (unmodified — no preamble)
    targetScriptPath = join(
      tmpdir(),
      `ue5_bridge_${timestamp}_code.py`
    ).replace(/\\/g, "/");
    await writeFile(targetScriptPath, params.code, "utf-8");
  } else {
    targetScriptPath = params.script_path!.replace(/\\/g, "/");
  }

  // Write the wrapper that exec()'s the target inside try/except
  const wrapperScriptPath = join(
    tmpdir(),
    `ue5_bridge_${timestamp}.py`
  ).replace(/\\/g, "/");
  await writeFile(
    wrapperScriptPath,
    buildWrapper(resultPath, targetScriptPath),
    "utf-8"
  );

  console.error(`[ue5-bridge] ue5_run_python: py "${wrapperScriptPath}"`);
  await executeConsoleCommand(`py "${wrapperScriptPath}"`);

  // Poll for the result file
  const start = Date.now();
  while (Date.now() - start < POLL_TIMEOUT_MS) {
    await new Promise((r) => setTimeout(r, POLL_INTERVAL_MS));
    try {
      await access(resultPath);
      const data = await readFile(resultPath, "utf-8");
      // Clean up result file
      try {
        await unlink(resultPath);
      } catch {
        // ignore
      }
      const parsed = JSON.parse(data);

      // If the wrapper caught an error, propagate it
      if (parsed.success === false && parsed.error) {
        return {
          success: false,
          error: parsed.error,
          traceback: parsed.traceback,
          script_path: targetScriptPath,
        };
      }

      return {
        success: true,
        result: parsed,
        script_path: targetScriptPath,
      };
    } catch {
      // Not ready yet
    }
  }

  // Timed out — return failure, not misleading success
  return {
    success: false,
    error:
      "Timed out waiting for result (30s). The script may still be running, " +
      "or it may not have called _write_result(). Check UE5 Output Log.",
    script_path: targetScriptPath,
  };
}
