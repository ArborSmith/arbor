/**
 * Generates a temp `.mcp.json` that registers the local bridge build under
 * `ue5-bridge`, so the spawned Claude process talks to the *same* UE5 editor
 * the test suite is using.
 *
 * The bridge is the one in `bridge/dist/index.js` relative to this file. The
 * UE5_REMOTE_PORT env var is propagated from `global-setup.ts` so Claude's
 * bridge instance points at the test-suite editor port (e.g. 30020), not the
 * default 30010.
 */

import { writeFile, mkdtemp, unlink, rmdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { dirname, resolve, join } from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/** Absolute path to the built bridge entry point (`bridge/dist/index.js`). */
function bridgeEntryPath(): string {
  // tests/e2e/runner/mcp-config.ts -> bridge/dist/index.js
  return resolve(__dirname, "..", "..", "..", "dist", "index.js");
}

export interface McpConfigHandle {
  configPath: string;
  cleanup(): Promise<void>;
}

export async function writeMcpConfig(): Promise<McpConfigHandle> {
  const dir = await mkdtemp(join(tmpdir(), "arbor-e2e-mcp-"));
  const configPath = join(dir, "mcp.json");

  const port = process.env.UE5_REMOTE_PORT ?? "30010";

  const config = {
    mcpServers: {
      "ue5-bridge": {
        command: "node",
        args: [bridgeEntryPath()],
        env: {
          UE5_REMOTE_PORT: port,
          // Inherit feature flags so e2e runs use whatever the host has enabled
          ARBOR_TOOLS: process.env.ARBOR_TOOLS ?? "",
        },
      },
    },
  };

  await writeFile(configPath, JSON.stringify(config, null, 2), "utf-8");

  return {
    configPath,
    cleanup: async () => {
      try {
        await unlink(configPath);
      } catch {
        /* best-effort */
      }
      try {
        await rmdir(dir);
      } catch {
        /* best-effort */
      }
    },
  };
}
