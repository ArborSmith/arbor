import { z } from "zod";

/**
 * Handler for a single action within a category tool.
 * Returns either:
 * - A plain object → auto-wrapped in MCP text content via JSON.stringify
 * - An object with `content` array → passed through as raw MCP response (for images, etc.)
 */
export type ActionHandler = (
  params: Record<string, unknown>
) => Promise<unknown>;

/**
 * Declares which params each action uses.
 * Used by tool-factory to auto-generate rich per-action descriptions.
 */
export interface ActionParamSpec {
  /** Brief one-line description of what this action does */
  summary: string;
  /** Param names required for this action (keys in `schema`) */
  required?: string[];
  /** Param names optional for this action (keys in `schema`) */
  optional?: string[];
}

/**
 * A category tool groups related actions under a single MCP tool.
 * Claude sees one tool with an `action` enum instead of many individual tools.
 */
export interface CategoryTool {
  /** MCP tool description shown to Claude */
  description: string;

  /**
   * Combined Zod schema for all actions' params (minus `action`, which is auto-added).
   * All fields should be optional since different actions need different params.
   * Per-action validation happens in the handler.
   */
  schema: Record<string, z.ZodTypeAny>;

  /** Map of action name → handler function */
  actions: Record<string, ActionHandler>;

  /**
   * Action names that are read-only (queries, listings, introspection).
   * When set, the tool is split into two MCP tools:
   *   - `{name}_query` with readOnlyHint: true (these actions)
   *   - `{name}` with the remaining write actions
   */
  readOnlyActions?: string[];

  /**
   * Per-action parameter declarations.
   * When present, tool-factory auto-generates a rich description
   * showing which params belong to each action.
   * Keys must match keys in `actions`.
   */
  actionParams?: Record<string, ActionParamSpec>;
}
