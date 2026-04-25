import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";

import { registerCategoryTool } from "./tool-factory.js";
import { fetchEnabledFeatures, type Features } from "./features.js";
import { STABLE_TOOLS } from "./registry/stable.js";
import { EXPERIMENTAL_TOOLS } from "./registry/experimental.js";

// --- Standalone tools (too simple or too special to group) ---
import { pingSchema, ping } from "./tools/core/ping.js";
import { runPythonSchema, runPython } from "./tools/core/run-python.js";
import { reportIssueSchema, reportIssue } from "./tools/core/report-issue.js";

// ============================================================================

const server = new McpServer({
  name: "ue5-bridge",
  version: "0.2.0",
});

// --- Shared tool wrapper for standalone tools ---

type ToolHandler = (params: Record<string, unknown>) => Promise<unknown>;

function wrapTool(fn: ToolHandler) {
  return async (params: Record<string, unknown>) => {
    try {
      const result = await fn(params);
      return {
        content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        isError:
          typeof result === "object" &&
          result !== null &&
          (result as Record<string, unknown>).success === false,
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text" as const,
            text: `Error: ${err instanceof Error ? err.message : String(err)}`,
          },
        ],
        isError: true,
      };
    }
  };
}

// --- Register standalone tools ---

server.tool(
  "ue5_ping",
  "Check if the UE5 editor is running and the Remote Control API is responding.",
  pingSchema,
  { readOnlyHint: true },
  wrapTool(ping as ToolHandler)
);

server.tool(
  "ue5_run_python",
  "Execute arbitrary Python code inside UE5's editor Python environment via the Remote Control API. " +
    "Use this as a general-purpose escape hatch — write inline Python using the `unreal` module to perform any " +
    "editor operation. Call `_write_result({...})` to return structured data.",
  runPythonSchema,
  wrapTool(runPython as ToolHandler)
);

server.tool(
  "report_issue",
  "Report a bug or feature request for the arbor library. Creates a GitHub issue via the `gh` CLI " +
    "in the repo named by GITHUB_TRACKER_REPO. Set type='bug' for broken behavior, type='feature_request' " +
    "for missing capabilities.",
  reportIssueSchema,
  wrapTool(reportIssue as unknown as ToolHandler)
);

// --- Register category tools (stable + optional experimental + overlay) ---

async function registerAllTools(features: Features) {
  for (const { name, tool } of STABLE_TOOLS) {
    registerCategoryTool(server, name, tool);
  }

  if (features.experimental) {
    for (const { name, tool, featureKey } of EXPERIMENTAL_TOOLS) {
      if (features[featureKey] !== false) {
        registerCategoryTool(server, name, tool);
      }
    }
  }

  // Optional private overlay hook — only present in local dev environments that
  // have a sibling overlay repo dropping an `extensions.local.ts` into this
  // directory. When absent, the dynamic import throws a module-not-found error
  // which we swallow silently (expected in public builds). Anything else is a
  // real bug in the overlay — rethrow so it's visible at boot.
  try {
    const ext = (await import("./extensions.local.js")) as {
      register?: (server: McpServer, features: Features) => void;
    };
    if (typeof ext.register === "function") {
      ext.register(server, features);
      console.error("[ue5-bridge] Private overlay extensions registered");
    }
  } catch (err) {
    const e = err as NodeJS.ErrnoException & { code?: string };
    const code = e?.code;
    if (code === "ERR_MODULE_NOT_FOUND" || code === "MODULE_NOT_FOUND") {
      // expected in public builds — no overlay present
    } else {
      console.error(
        `[ue5-bridge] Private overlay present but failed to load: ${e.message}`
      );
      throw err;
    }
  }
}

// --- Start server ---

async function main() {
  const features = await fetchEnabledFeatures();
  const enabledExperimental = features.experimental
    ? EXPERIMENTAL_TOOLS.filter((e) => features[e.featureKey] !== false).map((e) => e.name)
    : [];

  await registerAllTools(features);

  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error(
    `[ue5-bridge] MCP server running on stdio (v0.2.0 — stable + ${enabledExperimental.length} experimental)`
  );
  if (enabledExperimental.length > 0) {
    console.error(`[ue5-bridge] Experimental tools enabled: ${enabledExperimental.join(", ")}`);
  }
}

main().catch((err) => {
  console.error("Fatal error:", err);
  process.exit(1);
});
