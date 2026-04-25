import { z } from "zod";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

export const reportIssueSchema = {
  type: z
    .enum(["bug", "feature_request"])
    .describe(
      "Type of issue. 'bug' for something broken or behaving incorrectly, " +
        "'feature_request' for a missing capability or function."
    ),
  description: z
    .string()
    .describe(
      "Clear description of the bug or missing feature. " +
        'E.g. "snap_to_ground corrupts actor rotation when sweep fallback triggers."'
    ),
  module: z
    .string()
    .optional()
    .describe(
      "Which arbor Python module this relates to, if known. " +
        "Common modules: layout, actors, lighting, nav, materials, terrain, " +
        "structure, scatter, foliage, vfx, capture, inspect, mesh, textures, blueprints."
    ),
  attempted_task: z
    .string()
    .optional()
    .describe(
      "What you were trying to accomplish when you encountered this issue. " +
        'E.g. "Building a dungeon level with interconnected rooms."'
    ),
  suggested_signature: z
    .string()
    .optional()
    .describe(
      "Suggested Python function signature for a missing feature. " +
        'E.g. "arbor.layout.make_corridor(start_pos, end_pos, width=300, height=400)". ' +
        "Most relevant for feature_request type."
    ),
  error_message: z
    .string()
    .optional()
    .describe("Error message or traceback observed. Most relevant for bug type."),
  steps_to_reproduce: z
    .string()
    .optional()
    .describe("Steps to reproduce the bug. Most relevant for bug type."),
};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface ReportIssueParams {
  type: "bug" | "feature_request";
  description: string;
  module?: string;
  attempted_task?: string;
  suggested_signature?: string;
  error_message?: string;
  steps_to_reproduce?: string;
}

interface ReportIssueResult {
  sent: boolean;
  message: string;
  url?: string;
}

// ---------------------------------------------------------------------------
// GitHub
// ---------------------------------------------------------------------------

async function createGithubIssue(
  params: ReportIssueParams
): Promise<ReportIssueResult> {
  const repo = process.env.GITHUB_TRACKER_REPO;

  if (!repo) {
    return {
      sent: false,
      message: "GitHub not configured (GITHUB_TRACKER_REPO missing).",
    };
  }

  // Build title
  const prefix = params.type === "bug" ? "[Bug]" : "[Feature]";
  const moduleTag = params.module ? ` [arbor.${params.module}]` : "";
  const shortDesc =
    params.description.length > 80
      ? params.description.slice(0, 77).replace(/\s+\S*$/, "") + "..."
      : params.description;
  const title = `${prefix}${moduleTag} ${shortDesc}`;

  // Build body (GitHub-flavored Markdown)
  const bodyLines: string[] = [];

  bodyLines.push("## Description", "", params.description, "");

  if (params.module) {
    bodyLines.push(`**Module:** \`arbor.${params.module}\``, "");
  }
  if (params.attempted_task) {
    bodyLines.push("## Context", "", params.attempted_task, "");
  }
  if (params.error_message) {
    bodyLines.push("## Error", "", "```", params.error_message, "```", "");
  }
  if (params.steps_to_reproduce) {
    bodyLines.push("## Steps to Reproduce", "", params.steps_to_reproduce, "");
  }
  if (params.suggested_signature) {
    bodyLines.push(
      "## Suggested Signature",
      "",
      "```python",
      params.suggested_signature,
      "```",
      ""
    );
  }

  bodyLines.push("---", "*Reported automatically by Claude via ue5-bridge MCP.*");

  const body = bodyLines.join("\n");
  const label = params.type === "bug" ? "bug" : "feature-request";

  console.error(`[ue5-bridge] Creating GitHub issue in ${repo}`);

  try {
    const { stdout } = await execFileAsync("gh", [
      "issue",
      "create",
      "--repo",
      repo,
      "--title",
      title,
      "--body",
      body,
      "--label",
      label,
    ]);

    const issueUrl = stdout.trim();

    return {
      sent: true,
      message: `GitHub issue created: ${issueUrl}`,
      url: issueUrl,
    };
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return { sent: false, message: `GitHub issue creation failed: ${msg}` };
  }
}

// ---------------------------------------------------------------------------
// Main handler
// ---------------------------------------------------------------------------

export async function reportIssue(
  params: ReportIssueParams
): Promise<ReportIssueResult> {
  console.error(
    `[ue5-bridge] Reporting ${params.type}: ${params.description.slice(0, 100)}`
  );

  return createGithubIssue(params);
}
