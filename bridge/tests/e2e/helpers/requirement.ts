/**
 * Types for declarative E2E scenarios.
 *
 * A Scenario is a single high-level prompt handed to Claude plus a list of
 * Requirements. After Claude runs, every requirement's `check` is invoked
 * against the live UE5 editor. The results land in the report.
 */

export interface RequirementResult {
  passed: boolean;
  /** Short human-readable line — what was checked. */
  detail: string;
  /** Raw observed value (object/array/scalar) for the report. */
  observed?: unknown;
}

export interface Requirement {
  /** Stable id used in JSON; should be a short slug. */
  id: string;
  /** Display name in the report. */
  name: string;
  /** The MCP category this requirement exercises (e.g. "actors", "ai"). */
  category: string;
  check(): Promise<RequirementResult>;
}

export interface Scenario {
  id: string;
  /** Human-readable scenario title for the report. */
  name: string;
  /** Prompt verbatim — handed to `claude -p`. */
  prompt: string;
  /** Asset path prefix to clean up after the run (e.g. "/Game/E2E"). */
  assetCleanupRoot: string;
  /** Actor label prefix to clean up after the run (e.g. "E2E_TinyArena_"). */
  actorPrefix: string;
  requirements: Requirement[];
}
