---
name: run-tests
description: Use to run Arbor's test suites — bridge integration tests, single-engine e2e, the bridge TypeScript build, or the multi-engine compile + e2e sweep. Supports running one suite or all in sequence. Triggers: "run tests", "run all tests", "test arbor", "compile across versions", "run integration", "run e2e", "tiny arena", "build the bridge". Skip for: writing new tests (use the test files directly), debugging a single failing test (run vitest with `--reporter=verbose` directly).
---

# Running Arbor Tests

Arbor ships four test/build surfaces. Pick one or run them in sequence. **Default to confirming with the user before running the multi-engine sweep — it takes ~45 min and costs ~$10 in Claude API credits.**

## Test surfaces

| Suite | What it covers | Command |
|---|---|---|
| `build` | Bridge TypeScript compiles cleanly | `npm run build` in `<bridge>` |
| `integration` | Bridge MCP actions hit a live editor | `npm test` in `<bridge>` |
| `e2e` | Claude builds a scenario from a prompt; validators check the result against the *currently running* editor | `npm run test:e2e` in `<bridge>` (with `RUN_CLAUDE_E2E=1` in env) |
| `sweep` | Per-version compile + boot + e2e for each engine in the rig | `$env:ARBOR_RIG_SCRIPT` (see "Multi-engine rig" below) |

**Note on "unit tests":** Arbor doesn't ship a dedicated unit-test suite — `tests/integration/` is the lowest layer. If the user asks for "unit tests", clarify whether they mean integration. (For UE5 *automation* tests in their own game, point them at `arbor.automation.run_tests()` per the Arbor CLAUDE.md — that's their game's tests, not Arbor's.)

## Configurable paths

The skill never hardcodes paths. Resolve in this order:

| Var | Default | Meaning |
|---|---|---|
| `ARBOR_BRIDGE_DIR` | first match of `./bridge`, `./Plugins/Arbor/bridge`, then walk up looking for an `Arbor.uplugin` sibling and use `<arbor-root>/bridge` | Where the bridge npm scripts run from |
| `ARBOR_TEST_RIG` | *unset* | Directory containing one `Test_<version>/` subfolder per UE engine to test |
| `ARBOR_RIG_SCRIPT` | `$ARBOR_TEST_RIG/run-e2e-all.ps1` | The orchestrator script in the rig |

If any required var is unset for the requested suite, **stop and ask the user** — don't guess a path.

## Preconditions

- **build**: only requires Node + the bridge's `node_modules`. If `node_modules` is missing, `npm ci` first.
- **integration**: UE5 editor running with an Arbor-enabled project loaded; Remote Control responding (default port 30010, override via `UE5_REMOTE_PORT`). Tests skip cleanly if editor unreachable.
- **e2e**: same as integration, plus `RUN_CLAUDE_E2E=1` and the `claude` CLI on `PATH`.
- **sweep**: the rig (next section). The orchestrator spawns its own editors per project.

Ping first if you're unsure: `mcp__ue5-bridge__ue5_ping`.

## Multi-engine rig

The orchestrator script lives outside the Arbor repo because it depends on local engine installs and project layout choices. The convention this skill expects:

- `$ARBOR_TEST_RIG` points at a directory containing `Test_<version>/<Project>.uproject` folders (one per UE version, e.g. `Test_5.4/TP_Blank.uproject`, `Test_5.5/TP_Blank.uproject`).
- Each `Test_<version>/Plugins/Arbor` is an NTFS junction → the canonical Arbor plugin source so all engines share one source tree. (Only one engine's `Binaries/` can sit there at a time — the orchestrator handles the per-version rebuild.)
- The orchestrator (`run-e2e-all.ps1` by default) iterates `Test_*/`, recompiles Arbor for each engine, launches UE5, sets `ARBOR_E2E_PROJECT=<name>` so reports are tagged per project, and runs the e2e suite.

If `$env:ARBOR_TEST_RIG` isn't set when the user asks for a sweep:

1. Explain the convention above.
2. Offer to point at an existing rig if they have one.
3. Don't fabricate or auto-create a rig.

## Sequencing for "all"

When the user says "all tests" without qualification:

1. `build` (bridge TS compile — fast, fails loud).
2. `integration` (vitest — assumes editor is up).
3. **Pause and confirm** before `sweep` because of cost/duration. Skip if they say no.

Stop on first failure — there's no point running e2e when integration is red, or sweep when integration is red, etc.

If they say "all but skip sweep" or "all except e2e", honour it. If they say "everything including sweep", proceed without confirmation.

## Running one

| User says | Do |
|---|---|
| "build the bridge", "tsc", "compile the bridge" | `npm run build` in `$ARBOR_BRIDGE_DIR` |
| "run tests" (no qualifier), "integration", "vitest", "bridge tests" | `npm test` in `$ARBOR_BRIDGE_DIR` |
| "run e2e", "tiny arena", "claude e2e" | `npm run test:e2e` in `$ARBOR_BRIDGE_DIR` with `RUN_CLAUDE_E2E=1`. Verify editor first; if down, ask. |
| "run sweep", "across versions", "all engines" | `$ARBOR_RIG_SCRIPT` — confirm first. |
| "test 5.7" or "only 5.7" | `$ARBOR_RIG_SCRIPT -Only Test_5.7` |
| "sweep, skip rebuild" | `$ARBOR_RIG_SCRIPT -SkipBuild` |

## Reporting results

After each suite, summarize in 1–2 sentences. Reuse the suite's own counters — don't recount.

- **build**: success / first error.
- **integration**: pass/fail counts.
- **e2e**: pass/fail count *and* the report HTML path (`bridge/tests/e2e/.results/<dir>/report.html`).
- **sweep**: the per-project table the orchestrator already prints. Don't reformat it.

Don't paste raw vitest output back to the user — they'll read the file if they want details.

## When NOT to use this skill

- Authoring or fixing a single test → invoke `vitest run <file> --reporter=verbose` directly.
- Diagnosing a known failure → read the existing `bridge/tests/e2e/.results/<latest>/report.html` or test output before re-running.
- Inside CI → there's no CI for Arbor yet; this skill is for ad-hoc local runs.
