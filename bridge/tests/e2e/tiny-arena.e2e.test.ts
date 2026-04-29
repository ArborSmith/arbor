/**
 * E2E: Claude builds the Tiny Arena scenario from a single high-level prompt.
 *
 * Skipped unless BOTH:
 *   - the UE5 editor is reachable (same gate as the integration suite)
 *   - RUN_CLAUDE_E2E=1 is set in the environment (this test spawns the Claude
 *     CLI, which costs API credits, so we don't run it on every `npm test`).
 *
 * Run:
 *   set RUN_CLAUDE_E2E=1 && npx vitest run tests/e2e   (Windows cmd)
 *   $env:RUN_CLAUDE_E2E="1"; npx vitest run tests/e2e  (PowerShell)
 *   RUN_CLAUDE_E2E=1 npx vitest run tests/e2e          (bash)
 */

import { describe, it, expect } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { tinyArena } from "./scenarios/tiny-arena.js";
import { runClaude } from "./runner/claude-runner.js";
import { writeReport } from "./runner/report-writer.js";
import { clearActorsByPrefix, deleteAssetsUnder } from "./helpers/cleanup.js";
import { findAllScreenshotsSince } from "./helpers/validators.js";
import type { RequirementResult } from "./helpers/requirement.js";

const editorRunning = await isConnected();
const e2eEnabled = process.env.RUN_CLAUDE_E2E === "1";

describe.runIf(editorRunning && e2eEnabled)(
  "E2E: Tiny Arena via Claude",
  () => {
    it(
      "Claude builds a valid tiny arena from the high-level prompt",
      async () => {
        // Clean slate: remove any leftover actors/assets from a prior run
        await clearActorsByPrefix(tinyArena.actorPrefix);
        await deleteAssetsUnder(tinyArena.assetCleanupRoot);

        const startedMs = Date.now();

        // Hand Claude the prompt. 10-min cap covers slow editor + network.
        const claudeResult = await runClaude({
          prompt: tinyArena.prompt,
          timeoutMs: 600_000,
        });

        // Run validators sequentially. Multiple `runPython` calls in parallel
        // race on UE5's shared Python globals (`_RESULT_PATH` is module-level
        // in run-python.ts's wrapper) — we've seen scripts write to each
        // other's result files, causing spurious timeouts and cross-attached
        // tracebacks. Sequential is slower but correct.
        const requirementResults: Array<
          RequirementResult & { id: string; name: string; category: string }
        > = [];
        for (const r of tinyArena.requirements) {
          try {
            const res = await r.check();
            requirementResults.push({
              id: r.id,
              name: r.name,
              category: r.category,
              ...res,
            });
          } catch (e) {
            requirementResults.push({
              id: r.id,
              name: r.name,
              category: r.category,
              passed: false,
              detail: `validator threw: ${(e as Error).message}`,
              observed: { error: (e as Error).message },
            });
          }
        }

        // Collect every screenshot Claude produced for the HTML report
        const screenshotPaths = await findAllScreenshotsSince(startedMs);

        const { dir, htmlPath } = await writeReport({
          scenario: tinyArena,
          startedMs,
          claudeResult,
          requirementResults,
          screenshotPaths,
        });

        const failed = requirementResults.filter((r) => !r.passed);
        const summary =
          `${requirementResults.length - failed.length}/${requirementResults.length} requirements passed`;
        // Always log where the report landed — that's how a human triages
        // a failed run.
        // eslint-disable-next-line no-console
        console.log(
          `\n[E2E] ${summary} (Claude exit ${claudeResult.exitCode})\n[E2E] Report: ${htmlPath}\n[E2E] Dir:    ${dir}\n`
        );

        expect(
          failed,
          `Failed requirements: ${failed.map((f) => `${f.id} (${f.detail})`).join("; ")}`
        ).toHaveLength(0);
        expect(
          claudeResult.exitCode,
          `Claude exited non-zero. See report at ${htmlPath}`
        ).toBe(0);

        // Cleanup is best-effort and runs after assertions, so a failed run
        // leaves the level intact for inspection.
      },
      900_000 // 15-minute hook timeout — Claude + UE5 builds can be slow
    );
  }
);

describe.skipIf(editorRunning && e2eEnabled)(
  "E2E: Tiny Arena (skipped)",
  () => {
    it("set RUN_CLAUDE_E2E=1 and start the UE5 editor to enable this test", () => {
      // eslint-disable-next-line no-console
      console.log(
        `[E2E] Skipped — editorRunning=${editorRunning}, RUN_CLAUDE_E2E=${process.env.RUN_CLAUDE_E2E ?? "unset"}`
      );
    });
  }
);
