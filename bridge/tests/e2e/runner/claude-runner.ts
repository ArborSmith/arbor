/**
 * Spawn the Claude Code CLI headless with the bridge attached, feed it a
 * prompt via stdin, capture the JSON response.
 *
 * Windows quirk: `claude` is normally `claude.cmd` (npm-installed) which Node's
 * `spawn` can't resolve directly. Route through `cmd.exe /s /c "claude ..."`
 * the same way `Source/Arbor/Private/ArborChatWidget.cpp:1086-1097` does.
 *
 * Permissions: this uses `--dangerously-skip-permissions` because the run is
 * unattended (no human to approve tool calls). Risk is bounded by the scenario
 * cleanup hooks (`clearActorsByPrefix` + `deleteAssetsUnder`) and the fact
 * that Claude is talking to a *test* editor port via the bridge.
 */

import { spawn } from "node:child_process";
import { writeMcpConfig } from "./mcp-config.js";

export interface ClaudeRunResult {
  exitCode: number;
  /** Final assistant text, parsed from `--output-format json`. */
  finalText: string | null;
  /** The Claude session id, used to find the transcript file. */
  sessionId: string | null;
  /** Total cost reported by Claude (USD), if available. */
  totalCostUsd: number | null;
  /** Wall-clock duration of the Claude process in ms. */
  durationMs: number;
  /** Number of turns Claude took (assistant messages). */
  numTurns: number | null;
  /** Stdout, captured verbatim — useful for the report when JSON parse fails. */
  rawStdout: string;
  /** Stderr, captured verbatim — useful when the process exits non-zero. */
  rawStderr: string;
  /** Whether the prompt-stream produced a parseable final JSON object. */
  parsed: boolean;
}

export interface RunClaudeOptions {
  /** Prompt to pass to Claude (via stdin to avoid shell-quoting issues). */
  prompt: string;
  /** Working directory for Claude (defaults to the project root). */
  cwd?: string;
  /** Hard cap in ms; the process is killed if it exceeds this. */
  timeoutMs?: number;
  /** Optional model override (e.g. "claude-sonnet-4-6"). */
  model?: string;
}

export async function runClaude(opts: RunClaudeOptions): Promise<ClaudeRunResult> {
  const mcp = await writeMcpConfig();
  const start = Date.now();

  try {
    const args = [
      "--print",
      "--mcp-config",
      mcp.configPath,
      "--dangerously-skip-permissions",
      "--output-format",
      "json",
    ];
    if (opts.model) args.push("--model", opts.model);

    const isWindows = process.platform === "win32";

    // On Windows, route through cmd.exe so PATHEXT (.cmd, .bat) resolves.
    // We don't need shell expansion on the prompt itself — that's fed via stdin.
    const cmd = isWindows ? "cmd.exe" : "claude";
    const spawnArgs = isWindows
      ? ["/s", "/c", `claude ${args.map(quoteForCmd).join(" ")}`]
      : args;

    const child = spawn(cmd, spawnArgs, {
      cwd: opts.cwd,
      stdio: ["pipe", "pipe", "pipe"],
      env: process.env,
    });

    const stdoutChunks: Buffer[] = [];
    const stderrChunks: Buffer[] = [];
    child.stdout.on("data", (b: Buffer) => stdoutChunks.push(b));
    child.stderr.on("data", (b: Buffer) => stderrChunks.push(b));

    // Feed prompt via stdin
    child.stdin.write(opts.prompt);
    child.stdin.end();

    let timedOut = false;
    let timer: NodeJS.Timeout | null = null;
    if (opts.timeoutMs) {
      timer = setTimeout(() => {
        timedOut = true;
        child.kill("SIGKILL");
      }, opts.timeoutMs);
    }

    const exitCode: number = await new Promise((resolve) => {
      child.on("exit", (code) => resolve(code ?? 1));
      child.on("error", () => resolve(1));
    });
    if (timer) clearTimeout(timer);

    const rawStdout = Buffer.concat(stdoutChunks).toString("utf-8");
    const rawStderr = Buffer.concat(stderrChunks).toString("utf-8");

    const parsed = parseFinalJson(rawStdout);
    return {
      exitCode: timedOut ? 124 : exitCode,
      finalText: parsed?.result ?? null,
      sessionId: parsed?.session_id ?? null,
      totalCostUsd: parsed?.total_cost_usd ?? null,
      numTurns: parsed?.num_turns ?? null,
      durationMs: Date.now() - start,
      rawStdout,
      rawStderr,
      parsed: parsed !== null,
    };
  } finally {
    await mcp.cleanup();
  }
}

interface ClaudePrintJson {
  result?: string;
  session_id?: string;
  total_cost_usd?: number;
  num_turns?: number;
}

/**
 * Claude's `--output-format json` writes a single JSON object to stdout when
 * done. We tolerate a trailing newline and a possible streamed initialization
 * line by trying to parse the last `{...}` block.
 */
function parseFinalJson(stdout: string): ClaudePrintJson | null {
  const trimmed = stdout.trim();
  if (!trimmed) return null;
  // Fast path: the whole stdout is the JSON object
  try {
    return JSON.parse(trimmed) as ClaudePrintJson;
  } catch {
    /* fall through */
  }
  // Fallback: find the last `{` that opens a top-level object
  const lastOpen = trimmed.lastIndexOf("\n{");
  if (lastOpen >= 0) {
    try {
      return JSON.parse(trimmed.slice(lastOpen + 1)) as ClaudePrintJson;
    } catch {
      /* give up */
    }
  }
  return null;
}

/** Minimal cmd.exe quoting for arg values. We control the call site, so the
 * paths are well-formed; we only need to wrap things with spaces. */
function quoteForCmd(arg: string): string {
  if (/\s/.test(arg)) return `"${arg.replace(/"/g, '\\"')}"`;
  return arg;
}
