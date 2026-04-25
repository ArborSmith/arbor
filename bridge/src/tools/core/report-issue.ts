import { z } from "zod";
import { execFile, exec } from "node:child_process";
import { promisify } from "node:util";
import { startIssueReviewServer } from "../../issue-review-server.js";

const execFileAsync = promisify(execFile);

// Spawn the platform's URL handler. Best-effort — failure to auto-open is
// not fatal; the URL is also logged to stderr so the user can click it.
function openInBrowser(url: string): void {
  let cmd: string;
  if (process.platform === "win32") cmd = `start "" "${url}"`;
  else if (process.platform === "darwin") cmd = `open "${url}"`;
  else cmd = `xdg-open "${url}"`;
  exec(cmd, (err) => {
    if (err) {
      console.error(`[ue5-bridge] Could not auto-open browser: ${err.message}`);
    }
  });
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
//
// IMPORTANT — GitHub issues are PUBLIC. Before filling any field below,
// sanitize project-specific identifiers and replace them with generic
// placeholders. The maintainers do not need your real names to fix Arbor
// bugs, and you should not assume your project is intended to be public.
//
//   Substitute:                     With:
//   /Game/Levels/MyForestRealm  →   /Game/Levels/MyLevel
//   BP_DragonRiderHero          →   BP_TestCharacter / BP_MyCharacter
//   AHexCrawlController         →   AMyController
//   D:/Studios/MyGame/...       →   <ProjectRoot>/...
//   "the dragon's combat AI"    →   "an AI character's combat behavior"
//
// Keep the technical content (which Arbor API was called, expected vs actual
// behavior, error traceback structure) — strip the project-specific naming
// and storyline context.

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
        'E.g. "snap_to_ground corrupts actor rotation when sweep fallback triggers." ' +
        "IMPORTANT: substitute project-specific identifiers (asset paths, Blueprint/" +
        "class/level/character names, IP/storyline references) with generic " +
        "placeholders like /Game/MyAssets/SM_Cube, BP_MyCharacter, MyLevel."
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
        "Describe in generic terms — the maintainers do not need your real " +
        "project context to fix Arbor bugs. " +
        'E.g. "Building a multi-room blockout with a navmesh and a patrolling AI" ' +
        '— NOT "Setting up the Dragon Lord boss arena for ProjectShadowfall."'
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
    .describe(
      "Error message or traceback observed. Most relevant for bug type. " +
        "Strip absolute filesystem paths (replace with <ProjectRoot>/...) and " +
        "any project-specific class names that appear in the trace."
    ),
  steps_to_reproduce: z
    .string()
    .optional()
    .describe(
      "Steps to reproduce the bug. Most relevant for bug type. " +
        "Use generic example identifiers (BP_TestCharacter, /Game/MyAssets/SM_Test) " +
        "rather than your real project's asset names. A minimal repro that uses " +
        "fresh placeholder assets is more useful than one that requires the " +
        "maintainers to mentally substitute every identifier."
    ),
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
  let title = `${prefix}${moduleTag} ${shortDesc}`;

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

  let body = bodyLines.join("\n");
  const label = params.type === "bug" ? "bug" : "feature-request";

  // Human review checkpoint. Default ON. Set ARBOR_ISSUE_REVIEW=skip to bypass
  // (useful for headless / CI contexts). Returning "cancel" aborts the file.
  if ((process.env.ARBOR_ISSUE_REVIEW || "").toLowerCase() !== "skip") {
    console.error(`[ue5-bridge] Opening issue review UI; awaiting user confirmation...`);
    try {
      const { url, result } = startIssueReviewServer({ repo, title, body });
      const reviewUrl = await url;
      console.error(`[ue5-bridge] Review at: ${reviewUrl}`);
      openInBrowser(reviewUrl);
      const review = await result;
      if (review.action === "cancel") {
        return { sent: false, message: "User cancelled the issue submission via the review UI." };
      }
      // User-edited values win
      title = review.title ?? title;
      body = review.body ?? body;
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      return { sent: false, message: `Issue review failed: ${msg}` };
    }
  }

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
