import { fileURLToPath } from "node:url";
import { dirname, resolve, join } from "node:path";
import { tmpdir } from "node:os";
import { readFile, unlink, access } from "node:fs/promises";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

function getBaseUrl(): string {
  const port = process.env.UE5_REMOTE_PORT || "30010";
  return `http://127.0.0.1:${port}`;
}

// ---------------------------------------------------------------------------
// Low-level HTTP
// ---------------------------------------------------------------------------

function handleNetworkError(err: unknown): never {
  const cause = (err as { cause?: { code?: string } }).cause;
  if (
    cause?.code === "ECONNREFUSED" ||
    cause?.code === "ENOTFOUND" ||
    cause?.code === "ETIMEDOUT"
  ) {
    throw new Error(
      `Cannot connect to UE5 editor at ${getBaseUrl()}. ` +
        `Is the editor running with the Remote Control API plugin enabled?`
    );
  }
  throw err;
}

async function request<T>(endpoint: string, body: unknown): Promise<T> {
  const url = `${getBaseUrl()}${endpoint}`;
  let res: Response;
  try {
    res = await fetch(url, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  } catch (err: unknown) {
    handleNetworkError(err);
  }

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`UE5 Remote Control error ${res.status}: ${text}`);
  }

  // Some endpoints return empty bodies on success
  const text = await res.text();
  if (!text) return {} as T;
  try {
    return JSON.parse(text) as T;
  } catch (err) {
    throw new Error(
      `UE5 Remote Control returned non-JSON response for ${endpoint} ` +
        `(${text.length} bytes): ${(err as Error).message}. ` +
        `First 200 chars: ${text.slice(0, 200)}`
    );
  }
}

async function requestGet<T>(endpoint: string): Promise<T> {
  const url = `${getBaseUrl()}${endpoint}`;
  let res: Response;
  try {
    res = await fetch(url);
  } catch (err: unknown) {
    handleNetworkError(err);
  }

  if (!res.ok) {
    const text = await res.text();
    throw new Error(`UE5 Remote Control error ${res.status}: ${text}`);
  }

  const text = await res.text();
  if (!text) return {} as T;
  try {
    return JSON.parse(text) as T;
  } catch (err) {
    throw new Error(
      `UE5 Remote Control returned non-JSON response for GET ${endpoint} ` +
        `(${text.length} bytes): ${(err as Error).message}. ` +
        `First 200 chars: ${text.slice(0, 200)}`
    );
  }
}

// ---------------------------------------------------------------------------
// Remote Control API wrappers
// ---------------------------------------------------------------------------

export async function callFunction(
  objectPath: string,
  functionName: string,
  parameters: Record<string, unknown> = {}
): Promise<unknown> {
  return request("/remote/object/call", {
    objectPath,
    functionName,
    parameters,
  });
}

export async function getProperty(
  objectPath: string,
  propertyName: string
): Promise<unknown> {
  return request("/remote/object/property", {
    objectPath,
    propertyName,
    access: "READ_ACCESS",
  });
}

export async function setProperty(
  objectPath: string,
  propertyName: string,
  propertyValue: unknown
): Promise<unknown> {
  return request("/remote/object/property", {
    objectPath,
    propertyName,
    propertyValue,
    access: "WRITE_ACCESS",
  });
}

// ---------------------------------------------------------------------------
// Convenience helpers
// ---------------------------------------------------------------------------

export async function executeConsoleCommand(
  command: string
): Promise<unknown> {
  return callFunction(
    "/Script/Engine.Default__KismetSystemLibrary",
    "ExecuteConsoleCommand",
    { WorldContextObject: "", Command: command, SpecificPlayer: "" }
  );
}

export async function executePython(
  scriptName: string,
  args: string[] = []
): Promise<unknown> {
  const scriptPath = getPythonScriptPath(scriptName);
  // Quote the script path and each argument for safety (spaces in paths)
  const quotedArgs = args
    .map((a) => `"${a.replace(/\\/g, "/").replace(/"/g, '\\"')}"`)
    .join(" ");
  const command = `py "${scriptPath.replace(/\\/g, "/")}" ${quotedArgs}`;
  console.error(`[ue5-bridge] Executing Python: ${command}`);
  return executeConsoleCommand(command);
}

/**
 * Execute a Python script inside UE5 and read back a JSON result via temp file.
 *
 * The script receives a --result-file <path> argument. It must write JSON to
 * that file when done. This function polls for the file, reads it, and returns
 * the parsed contents.
 */
export async function executePythonWithResult<T = unknown>(
  scriptName: string,
  args: string[] = [],
  timeoutMs = 15000
): Promise<T> {
  const resultFile = join(
    tmpdir(),
    `ue5_bridge_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.json`
  );
  const resultFilePosix = resultFile.replace(/\\/g, "/");

  // Clean up any stale file
  try {
    await unlink(resultFile);
  } catch {
    // ignore
  }

  // Pass the result file path as first args so every script can use it
  await executePython(scriptName, ["--result-file", resultFilePosix, ...args]);

  // Poll for the result file
  const start = Date.now();
  const pollInterval = 250;
  while (Date.now() - start < timeoutMs) {
    // Wait for the file to exist — anything here is a retryable "not ready yet"
    try {
      await access(resultFile);
    } catch {
      await new Promise((r) => setTimeout(r, pollInterval));
      continue;
    }

    // File exists — read + parse must succeed or we surface the error
    const data = await readFile(resultFile, "utf-8");
    try {
      await unlink(resultFile);
    } catch {
      // cleanup best-effort — don't mask a real parse error below
    }

    try {
      return JSON.parse(data) as T;
    } catch (err) {
      throw new Error(
        `Python script ${scriptName} produced a result file with malformed JSON ` +
          `(${data.length} bytes at ${resultFile}): ${(err as Error).message}. ` +
          `First 200 chars: ${data.slice(0, 200)}`
      );
    }
  }

  throw new Error(
    `Timed out waiting for Python script result (${timeoutMs}ms). ` +
      `The script may still be running inside UE5 — check the Output Log.`
  );
}

// ---------------------------------------------------------------------------
// Arbor C++ direct calls (bypass Python)
// ---------------------------------------------------------------------------

/**
 * Call a static UFUNCTION on an Arbor C++ builder class directly via
 * Remote Control API.  The HTTP response contains the return value
 * synchronously — no temp files, no Python interpreter, no polling.
 *
 * For functions returning FString (JSON), parse the ReturnValue from the
 * response.
 */
export async function callArbor(
  className: string,
  functionName: string,
  parameters: Record<string, unknown> = {}
): Promise<{ ReturnValue?: string; [key: string]: unknown }> {
  const objectPath = `/Script/Arbor.Default__${className}`;
  const result = (await callFunction(objectPath, functionName, parameters)) as {
    ReturnValue?: string;
    [key: string]: unknown;
  };
  return result;
}

/**
 * Call an Arbor C++ UFUNCTION that returns an FString containing JSON.
 * Parses the JSON automatically and returns the result object.
 * Throws on failure (empty return, invalid JSON, etc.).
 */
export async function callArborJson<T = Record<string, unknown>>(
  className: string,
  functionName: string,
  parameters: Record<string, unknown> = {}
): Promise<T> {
  const result = await callArbor(className, functionName, parameters);
  const jsonStr = result.ReturnValue;
  if (!jsonStr) {
    throw new Error(
      `${className}::${functionName} returned empty result — check UE5 Output Log`
    );
  }
  try {
    return JSON.parse(jsonStr) as T;
  } catch (err) {
    throw new Error(
      `${className}::${functionName} returned malformed JSON (${jsonStr.length} bytes): ` +
        `${(err as Error).message}. First 200 chars: ${jsonStr.slice(0, 200)}`
    );
  }
}

export async function isConnected(): Promise<boolean> {
  try {
    await requestGet("/remote/info");
    return true;
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

export function getPythonScriptPath(scriptName: string): string {
  // Resolve from compiled dist/ up to the package root, then into python/
  const thisFile = fileURLToPath(import.meta.url);
  const distDir = dirname(thisFile); // dist/
  const packageRoot = resolve(distDir, ".."); // servers/ue5-bridge/
  return join(packageRoot, "python", scriptName);
}

export function getPort(): number {
  return parseInt(process.env.UE5_REMOTE_PORT || "30010", 10);
}
