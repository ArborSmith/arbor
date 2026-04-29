/**
 * Writes the per-run JSON + self-contained HTML report.
 *
 * The HTML report is intentionally one file with inlined CSS and base64-encoded
 * screenshots so it can be moved/emailed/zipped without breaking. No runtime
 * deps beyond Node built-ins.
 */

import { promises as fs } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve, join, basename } from "node:path";
import type { Scenario, RequirementResult } from "../helpers/requirement.js";
import type { ClaudeRunResult } from "./claude-runner.js";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/** Output root: `bridge/tests/e2e/.results/`. */
function resultsRoot(): string {
  return resolve(__dirname, "..", ".results");
}

export interface ScenarioReport {
  scenario: { id: string; name: string };
  /** Set when ARBOR_E2E_PROJECT is in the env at run time. */
  projectTag: string | null;
  startedIso: string;
  finishedIso: string;
  durationMs: number;
  claude: {
    exitCode: number;
    sessionId: string | null;
    finalText: string | null;
    totalCostUsd: number | null;
    numTurns: number | null;
    durationMs: number;
    parsed: boolean;
    /** Truncated stdout/stderr — always available, even when parse failed. */
    rawStdoutTail: string;
    rawStderrTail: string;
  };
  requirements: Array<{
    id: string;
    name: string;
    category: string;
    passed: boolean;
    detail: string;
    observed?: unknown;
  }>;
  screenshots: string[];
  summary: { total: number; passed: number; failed: number };
}

export interface WriteReportInput {
  scenario: Scenario;
  startedMs: number;
  claudeResult: ClaudeRunResult;
  requirementResults: Array<
    RequirementResult & { id: string; name: string; category: string }
  >;
  screenshotPaths: string[];
}

export async function writeReport(
  input: WriteReportInput
): Promise<{ dir: string; jsonPath: string; htmlPath: string }> {
  const finished = Date.now();
  const stamp = new Date(input.startedMs)
    .toISOString()
    .replace(/[:.]/g, "-");
  // ARBOR_E2E_PROJECT lets a sweep across multiple UE projects keep their
  // reports separated. Sanitize to avoid weird filesystem chars.
  const projectTag =
    process.env.ARBOR_E2E_PROJECT?.replace(/[^\w.-]/g, "_") || null;
  const dirName = projectTag
    ? `${input.scenario.id}__${projectTag}__${stamp}`
    : `${input.scenario.id}-${stamp}`;
  const dir = join(resultsRoot(), dirName);
  await fs.mkdir(dir, { recursive: true });

  const passed = input.requirementResults.filter((r) => r.passed).length;
  const report: ScenarioReport = {
    scenario: { id: input.scenario.id, name: input.scenario.name },
    projectTag,
    startedIso: new Date(input.startedMs).toISOString(),
    finishedIso: new Date(finished).toISOString(),
    durationMs: finished - input.startedMs,
    claude: {
      exitCode: input.claudeResult.exitCode,
      sessionId: input.claudeResult.sessionId,
      finalText: input.claudeResult.finalText,
      totalCostUsd: input.claudeResult.totalCostUsd,
      numTurns: input.claudeResult.numTurns,
      durationMs: input.claudeResult.durationMs,
      parsed: input.claudeResult.parsed,
      rawStdoutTail: input.claudeResult.rawStdout.slice(-4000),
      rawStderrTail: input.claudeResult.rawStderr.slice(-4000),
    },
    requirements: input.requirementResults.map((r) => ({
      id: r.id,
      name: r.name,
      category: r.category,
      passed: r.passed,
      detail: r.detail,
      observed: r.observed,
    })),
    screenshots: input.screenshotPaths,
    summary: {
      total: input.requirementResults.length,
      passed,
      failed: input.requirementResults.length - passed,
    },
  };

  const jsonPath = join(dir, "report.json");
  await fs.writeFile(jsonPath, JSON.stringify(report, null, 2), "utf-8");

  const htmlPath = join(dir, "report.html");
  await fs.writeFile(htmlPath, await renderHtml(report), "utf-8");

  return { dir, jsonPath, htmlPath };
}

async function renderHtml(report: ScenarioReport): Promise<string> {
  const screenshotMarkup = await Promise.all(
    report.screenshots.map(async (p) => {
      try {
        const data = await fs.readFile(p);
        const mime = p.toLowerCase().endsWith(".jpg") ||
          p.toLowerCase().endsWith(".jpeg")
          ? "image/jpeg"
          : "image/png";
        const b64 = data.toString("base64");
        return `<figure class="shot"><img src="data:${mime};base64,${b64}" alt="${escapeHtml(basename(p))}"/><figcaption>${escapeHtml(basename(p))}</figcaption></figure>`;
      } catch {
        return `<figure class="shot missing"><figcaption>missing: ${escapeHtml(p)}</figcaption></figure>`;
      }
    })
  );

  const reqRows = report.requirements
    .map((r) => {
      const cls = r.passed ? "pass" : "fail";
      const observed =
        r.observed !== undefined
          ? `<details><summary>observed</summary><pre>${escapeHtml(JSON.stringify(r.observed, null, 2))}</pre></details>`
          : "";
      return `<tr class="${cls}">
  <td class="status">${r.passed ? "PASS" : "FAIL"}</td>
  <td><code>${escapeHtml(r.category)}</code></td>
  <td>${escapeHtml(r.name)}</td>
  <td>${escapeHtml(r.detail)}${observed}</td>
</tr>`;
    })
    .join("\n");

  const summaryClass = report.summary.failed === 0 ? "all-pass" : "has-fail";

  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Arbor E2E — ${escapeHtml(report.scenario.name)}</title>
<style>
  :root { color-scheme: light dark; }
  body { font: 14px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif; max-width: 1100px; margin: 1.5rem auto; padding: 0 1rem; }
  h1 { margin-bottom: 0.25rem; }
  .meta { color: #666; font-size: 13px; margin-bottom: 1rem; }
  .summary { padding: 0.75rem 1rem; border-radius: 6px; margin-bottom: 1.25rem; font-weight: 600; }
  .summary.all-pass { background: #d4edda; color: #155724; }
  .summary.has-fail { background: #f8d7da; color: #721c24; }
  table { width: 100%; border-collapse: collapse; margin-bottom: 1.5rem; }
  th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid #e0e0e0; vertical-align: top; }
  th { font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.04em; color: #555; }
  tr.pass .status { color: #198754; font-weight: 600; }
  tr.fail .status { color: #dc3545; font-weight: 600; }
  tr.fail { background: rgba(220, 53, 69, 0.06); }
  pre { background: #f5f5f5; padding: 8px 10px; border-radius: 4px; font-size: 12px; overflow-x: auto; max-height: 300px; }
  details summary { cursor: pointer; color: #555; font-size: 12px; margin-top: 4px; }
  .shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; }
  figure.shot { margin: 0; }
  figure.shot img { width: 100%; border: 1px solid #ddd; border-radius: 4px; }
  figure.shot figcaption { font-size: 12px; color: #666; margin-top: 4px; word-break: break-all; }
  figure.missing { padding: 1rem; background: #f5f5f5; border-radius: 4px; text-align: center; color: #999; }
  section { margin-bottom: 2rem; }
  code { background: #f5f5f5; padding: 1px 6px; border-radius: 3px; font-size: 12.5px; }
  .claude-meta { display: grid; grid-template-columns: max-content auto; gap: 4px 12px; font-size: 13px; }
  .claude-meta dt { color: #555; font-weight: 600; }
  .final-text { white-space: pre-wrap; background: #f9f9f9; padding: 10px; border-radius: 4px; border-left: 3px solid #aaa; }
</style>
</head>
<body>
<h1>${escapeHtml(report.scenario.name)}${report.projectTag ? ` <span style="color:#666;font-weight:400;">[${escapeHtml(report.projectTag)}]</span>` : ""}</h1>
<div class="meta">
  scenario id: <code>${escapeHtml(report.scenario.id)}</code>
  ${report.projectTag ? `· project: <code>${escapeHtml(report.projectTag)}</code>` : ""}
  · started ${escapeHtml(report.startedIso)}
  · duration ${(report.durationMs / 1000).toFixed(1)}s
</div>

<div class="summary ${summaryClass}">
  ${report.summary.passed} / ${report.summary.total} requirements passed
  ${report.summary.failed > 0 ? `· <strong>${report.summary.failed} failed</strong>` : ""}
  · Claude exit ${report.claude.exitCode}${report.claude.totalCostUsd !== null ? ` · cost $${report.claude.totalCostUsd.toFixed(4)}` : ""}
</div>

<section>
  <h2>Requirements</h2>
  <table>
    <thead><tr><th>Status</th><th>Category</th><th>Requirement</th><th>Detail</th></tr></thead>
    <tbody>
${reqRows}
    </tbody>
  </table>
</section>

<section>
  <h2>Screenshots (${report.screenshots.length})</h2>
  ${report.screenshots.length === 0
    ? "<p><em>No screenshots collected.</em></p>"
    : `<div class="shots">${screenshotMarkup.join("")}</div>`}
</section>

<section>
  <h2>Claude run</h2>
  <dl class="claude-meta">
    <dt>exit code</dt><dd>${report.claude.exitCode}</dd>
    <dt>parsed JSON</dt><dd>${report.claude.parsed ? "yes" : "no"}</dd>
    <dt>session id</dt><dd>${escapeHtml(report.claude.sessionId ?? "(none)")}</dd>
    <dt>turns</dt><dd>${report.claude.numTurns ?? "(unknown)"}</dd>
    <dt>duration</dt><dd>${(report.claude.durationMs / 1000).toFixed(1)}s</dd>
    <dt>cost</dt><dd>${report.claude.totalCostUsd !== null ? `$${report.claude.totalCostUsd.toFixed(4)}` : "(unknown)"}</dd>
  </dl>
  ${report.claude.finalText ? `<h3>Final assistant text</h3><div class="final-text">${escapeHtml(report.claude.finalText)}</div>` : ""}
  <details><summary>raw stdout (tail)</summary><pre>${escapeHtml(report.claude.rawStdoutTail)}</pre></details>
  <details><summary>raw stderr (tail)</summary><pre>${escapeHtml(report.claude.rawStderrTail)}</pre></details>
</section>
</body>
</html>`;
}

function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
