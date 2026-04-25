import { spawn, type ChildProcess, exec } from "node:child_process";
import {
  readFileSync,
  writeFileSync,
  existsSync,
  copyFileSync,
  unlinkSync,
  mkdirSync,
} from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve, join } from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

let editorProcess: ChildProcess | null = null;
let editorPid: number | null = null;
let weSpawnedEditor = false;
let configBackupPath: string | null = null;
let configPath: string | null = null;

// ---------------------------------------------------------------------------
// Minimal .env parser (avoids adding dotenv dependency)
// ---------------------------------------------------------------------------

function loadEnvFile(): void {
  try {
    const envPath = resolve(__dirname, "..", ".env");
    const content = readFileSync(envPath, "utf-8");
    for (const line of content.split("\n")) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith("#")) continue;
      const eqIdx = trimmed.indexOf("=");
      if (eqIdx === -1) continue;
      const key = trimmed.slice(0, eqIdx).trim();
      let value = trimmed.slice(eqIdx + 1).trim();
      if (
        (value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith("'") && value.endsWith("'"))
      ) {
        value = value.slice(1, -1);
      }
      // Existing env vars take precedence
      if (!(key in process.env)) {
        process.env[key] = value;
      }
    }
  } catch {
    // No .env file — that's fine
  }
}

// ---------------------------------------------------------------------------
// Config patching — UE5 saved config overrides -ini: flags, so we must
// patch the RemoteControl.ini file directly before launch.
// ---------------------------------------------------------------------------

const RC_INI_SECTION =
  "[/Script/RemoteControlCommon.RemoteControlSettings]";
const RC_PORT_KEY = "RemoteControlHttpServerPort";

function patchRemoteControlConfig(
  projectPath: string,
  port: number
): void {
  const projectDir = dirname(projectPath);
  const configDir = join(
    projectDir,
    "Saved",
    "Config",
    "WindowsEditor"
  );
  configPath = join(configDir, "RemoteControl.ini");
  configBackupPath = join(configDir, "RemoteControl.ini.testbackup");

  // Back up the existing config if it exists
  if (existsSync(configPath)) {
    copyFileSync(configPath, configBackupPath);
    // Patch the port in the existing file
    let content = readFileSync(configPath, "utf-8");
    const portRegex = new RegExp(
      `^(${RC_PORT_KEY}\\s*=\\s*)\\d+`,
      "m"
    );
    if (portRegex.test(content)) {
      content = content.replace(portRegex, `$1${port}`);
    } else if (content.includes(RC_INI_SECTION)) {
      // Section exists but no port key — add it after the section header
      content = content.replace(
        RC_INI_SECTION,
        `${RC_INI_SECTION}\n${RC_PORT_KEY}=${port}`
      );
    } else {
      // No section at all — append
      content += `\n${RC_INI_SECTION}\n${RC_PORT_KEY}=${port}\n`;
    }
    writeFileSync(configPath, content, "utf-8");
  } else {
    // No config file — create one with just the port setting
    mkdirSync(configDir, { recursive: true });
    writeFileSync(
      configPath,
      `${RC_INI_SECTION}\n${RC_PORT_KEY}=${port}\n`,
      "utf-8"
    );
    configBackupPath = null; // Nothing to restore — just delete on cleanup
  }
}

function restoreRemoteControlConfig(): void {
  if (!configPath) return;
  if (configBackupPath && existsSync(configBackupPath)) {
    copyFileSync(configBackupPath, configPath);
    unlinkSync(configBackupPath);
  } else if (configBackupPath === null && existsSync(configPath)) {
    // We created it from scratch — delete it
    unlinkSync(configPath);
  }
  configPath = null;
  configBackupPath = null;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function isPortResponding(port: number): Promise<boolean> {
  try {
    const res = await fetch(`http://127.0.0.1:${port}/remote/info`);
    return res.ok;
  } catch {
    return false;
  }
}

async function pollForReady(port: number, timeoutMs: number): Promise<void> {
  const url = `http://127.0.0.1:${port}/remote/info`;
  const start = Date.now();
  const interval = 2000;
  while (Date.now() - start < timeoutMs) {
    try {
      const res = await fetch(url);
      if (res.ok) return;
    } catch {
      // Not ready yet
    }
    await new Promise((r) => setTimeout(r, interval));
  }
  throw new Error(
    `UE5 editor did not respond on port ${port} within ${timeoutMs / 1000}s`
  );
}

function killProcessTree(pid: number): Promise<void> {
  return new Promise((resolve) => {
    // Windows: taskkill /T kills the whole process tree
    exec(`taskkill /pid ${pid} /T /F`, () => resolve());
  });
}

// ---------------------------------------------------------------------------
// Cleanup on unexpected exit
// ---------------------------------------------------------------------------

function cleanupOnExit() {
  if (editorPid && weSpawnedEditor) {
    killProcessTree(editorPid);
  }
  restoreRemoteControlConfig();
}

process.on("SIGINT", cleanupOnExit);
process.on("SIGTERM", cleanupOnExit);

// ---------------------------------------------------------------------------
// globalSetup / globalTeardown
// ---------------------------------------------------------------------------

export async function setup(): Promise<void> {
  loadEnvFile();

  const editorPath = process.env.UE5_EDITOR_PATH;
  const projectPath = process.env.UE5_PROJECT_PATH;
  const testPort = parseInt(process.env.UE5_TEST_PORT || "30020", 10);
  const headless = process.env.UE5_HEADLESS === "true";
  const timeoutMs = parseInt(
    process.env.UE5_STARTUP_TIMEOUT_MS || "120000",
    10
  );

  // Always point tests at the test port when configured
  if (process.env.UE5_TEST_PORT) {
    process.env.UE5_REMOTE_PORT = String(testPort);
  }

  if (!editorPath || !projectPath) {
    console.log(
      "[global-setup] UE5_EDITOR_PATH or UE5_PROJECT_PATH not set — skipping editor launch"
    );
    return;
  }

  // Reuse an editor already running on the test port
  if (await isPortResponding(testPort)) {
    console.log(
      `[global-setup] Editor already responding on port ${testPort}, reusing`
    );
    process.env.UE5_REMOTE_PORT = String(testPort);
    return;
  }

  // Patch the saved config to use the test port
  patchRemoteControlConfig(projectPath, testPort);

  // Build launch args
  const args: string[] = [projectPath, "-log", "-nosplash"];

  if (headless) {
    args.push("-nullrhi", "-nosound", "-unattended");
  }

  console.log(`[global-setup] Launching UE5 editor on port ${testPort}...`);
  console.log(`[global-setup] ${editorPath} ${args.join(" ")}`);

  editorProcess = spawn(editorPath, args, {
    stdio: "ignore",
    detached: false,
  });
  editorPid = editorProcess.pid ?? null;

  if (!editorPid) {
    restoreRemoteControlConfig();
    throw new Error(
      `Failed to spawn UE5 editor. Check UE5_EDITOR_PATH: ${editorPath}`
    );
  }

  weSpawnedEditor = true;

  editorProcess.on("error", (err) => {
    console.error(`[global-setup] Editor process error: ${err.message}`);
  });

  editorProcess.on("exit", (code) => {
    if (weSpawnedEditor) {
      console.error(
        `[global-setup] UE5 editor exited unexpectedly (code ${code})`
      );
    }
    editorProcess = null;
    editorPid = null;
  });

  // Point all test code at the test port
  process.env.UE5_REMOTE_PORT = String(testPort);

  // Wait for editor to be ready
  await pollForReady(testPort, timeoutMs);
  console.log(`[global-setup] UE5 editor ready on port ${testPort}`);
}

export async function teardown(): Promise<void> {
  if (editorPid && weSpawnedEditor) {
    console.log(
      `[global-setup] Shutting down UE5 editor (PID ${editorPid})...`
    );
    await killProcessTree(editorPid);
    editorProcess = null;
    editorPid = null;
    weSpawnedEditor = false;
  }
  restoreRemoteControlConfig();
}
