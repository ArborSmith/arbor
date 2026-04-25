import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { ActionHandler, CategoryTool } from "./registry/types.js";

/**
 * Build a rich description from per-action param metadata.
 * Falls back to the raw description when `actionParams` is absent.
 */
export function buildDescription(
  tool: CategoryTool,
  actionNames: string[],
  suffix?: string
): string {
  if (!tool.actionParams) {
    return suffix ? `${tool.description} ${suffix}` : tool.description;
  }

  const lines: string[] = [
    suffix ? `${tool.description} ${suffix}` : tool.description,
    "",
    "Actions:",
  ];

  for (const name of actionNames) {
    const spec = tool.actionParams[name];
    if (!spec) {
      lines.push(`- ${name}`);
      continue;
    }

    const reqParts = spec.required ?? [];
    const optParts = (spec.optional ?? []).map((p) => `${p}?`);
    const allParts = [...reqParts, ...optParts];
    const paramStr = allParts.length > 0 ? allParts.join(", ") : "";

    lines.push(`- ${name}(${paramStr}) — ${spec.summary}`);
  }

  return lines.join("\n");
}

/**
 * Validate actionParams against the tool's actions and schema at registration time.
 */
function validateActionParams(name: string, tool: CategoryTool): void {
  if (!tool.actionParams) return;

  for (const [actionName, spec] of Object.entries(tool.actionParams)) {
    if (!tool.actions[actionName]) {
      console.error(
        `[${name}] actionParams declares "${actionName}" but no handler exists`
      );
    }
    for (const p of [...(spec.required ?? []), ...(spec.optional ?? [])]) {
      if (!tool.schema[p]) {
        console.error(
          `[${name}] action "${actionName}" references param "${p}" not in schema`
        );
      }
    }
  }
}

/**
 * Create the standard MCP callback that dispatches to action handlers.
 */
function makeCallback(
  actions: Record<string, ActionHandler>,
  actionNames: string[]
) {
  return async (params: Record<string, unknown>) => {
    try {
      const action = params.action as string;
      const handler = actions[action];
      if (!handler) {
        return {
          content: [
            {
              type: "text" as const,
              text: `Error: Unknown action "${action}". Available: ${actionNames.join(", ")}`,
            },
          ],
          isError: true,
        };
      }

      const result = await handler(params);

      // If the handler returned raw MCP content (e.g. screenshots with images),
      // pass it through directly. Cast to any to satisfy the SDK's strict
      // content type discriminated union.
      if (
        typeof result === "object" &&
        result !== null &&
        "content" in result &&
        Array.isArray((result as { content: unknown }).content)
      ) {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        return result as any;
      }

      // Otherwise wrap the result as JSON text
      return {
        content: [
          { type: "text" as const, text: JSON.stringify(result, null, 2) },
        ],
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

/**
 * Register a category tool on the MCP server.
 *
 * If the tool defines `readOnlyActions`, it is split into two MCP tools:
 *   - `{name}_query` with `readOnlyHint: true` (safe for plan mode)
 *   - `{name}` with the remaining write actions
 *
 * Otherwise, registers a single tool as before.
 */
export function registerCategoryTool(
  server: McpServer,
  name: string,
  tool: CategoryTool
): void {
  validateActionParams(name, tool);
  const readOnlySet = new Set(tool.readOnlyActions ?? []);

  if (readOnlySet.size > 0) {
    // Split actions into read-only and write groups
    const readActions: Record<string, ActionHandler> = {};
    const writeActions: Record<string, ActionHandler> = {};

    for (const [actionName, handler] of Object.entries(tool.actions)) {
      if (readOnlySet.has(actionName)) {
        readActions[actionName] = handler;
      } else {
        writeActions[actionName] = handler;
      }
    }

    // Register read-only query tool
    const queryActionNames = Object.keys(readActions) as [string, ...string[]];
    if (queryActionNames.length > 0) {
      const querySchema = {
        action: z
          .enum(queryActionNames)
          .describe(`Query action. One of: ${queryActionNames.join(", ")}`),
        ...tool.schema,
      };
      server.tool(
        `${name}_query`,
        buildDescription(tool, queryActionNames, "(read-only queries)"),
        querySchema,
        { readOnlyHint: true },
        makeCallback(readActions, queryActionNames)
      );
    }

    // Register write tool (original name)
    const writeActionNames = Object.keys(writeActions) as [string, ...string[]];
    if (writeActionNames.length > 0) {
      const writeSchema = {
        action: z
          .enum(writeActionNames)
          .describe(`Action to perform. One of: ${writeActionNames.join(", ")}`),
        ...tool.schema,
      };
      server.tool(
        name,
        buildDescription(tool, writeActionNames),
        writeSchema,
        makeCallback(writeActions, writeActionNames)
      );
    }
  } else {
    // No split — register as single tool (existing behavior)
    const actionNames = Object.keys(tool.actions) as [string, ...string[]];
    const schema = {
      action: z
        .enum(actionNames)
        .describe(`Action to perform. One of: ${actionNames.join(", ")}`),
      ...tool.schema,
    };
    server.tool(
      name,
      buildDescription(tool, actionNames),
      schema,
      makeCallback(tool.actions, actionNames)
    );
  }
}
