# Arbor E2E — Claude-driven scenario tests

Tests under this folder put **Claude in the loop**: each scenario hands the
Claude Code CLI a high-level natural-language prompt, lets it drive the MCP
tool surface to build something in UE5, then runs assertion queries against
the live editor and writes a JSON + HTML report.

This is different from `tests/integration/` — those tests call MCP action
handlers directly with hardcoded args. The integration suite verifies the
*tools* work; the e2e suite verifies that *Claude can use* the tools.

## When to run

- After a meaningful change to the bridge or to the C++ Arbor tools, to
  confirm the public surface still composes end-to-end through Claude.
- Before publishing a new bridge version.

Not on every `npm test` — Claude spawns cost API credits and the test takes
several minutes.

## Requirements

1. UE5 editor running with the project loaded and the Remote Control API
   enabled (same as `tests/integration/`).
2. `claude` (Claude Code CLI) on `PATH` and signed in.
3. `npm run build` already executed in `bridge/` so `dist/index.js` exists —
   the spawned Claude reads the bridge from there.

## Running

The e2e tests are gated behind `RUN_CLAUDE_E2E=1` so they don't fire on a
plain `npm test`:

```sh
# bash
RUN_CLAUDE_E2E=1 npx vitest run tests/e2e

# Windows cmd
set RUN_CLAUDE_E2E=1 && npx vitest run tests/e2e

# PowerShell
$env:RUN_CLAUDE_E2E="1"; npx vitest run tests/e2e
```

When the test finishes (pass or fail) it prints a path to a self-contained
HTML report in `tests/e2e/.results/<scenario>-<iso>/report.html`. Open it
in a browser to see per-requirement pass/fail, embedded screenshots, and the
tail of Claude's stdout/stderr.

## Adding a scenario

A scenario is one file under `scenarios/<name>.ts` that exports a `Scenario`
object. Then add a sibling `<name>.e2e.test.ts` that wires it into Vitest.

```ts
// scenarios/my-scene.ts
import type { Scenario } from "../helpers/requirement.js";
import { actorWithLabelExists, /* ... */ } from "../helpers/validators.js";

export const myScene: Scenario = {
  id: "my-scene",
  name: "My Scene",
  assetCleanupRoot: "/Game/E2E",
  actorPrefix: "E2E_MyScene_",
  prompt: `Build ... Use ue5_* tools. Don't ask for input. Use these labels: ...`,
  requirements: [
    { id: "...", name: "...", category: "actors", check: () => actorWithLabelExists("E2E_MyScene_Foo") },
    // ...
  ],
};
```

```ts
// my-scene.e2e.test.ts — copy tiny-arena.e2e.test.ts and swap the import.
```

### Writing prompts

- **Pin down labels and asset paths** that validators will look for. The point
  of an e2e test is that Claude composes the *tools*, not that it picks names.
- **Forbid playtest** unless validating PIE-driven behavior — PIE input causes
  flakes.
- **Confine cleanup scope**: tell Claude not to touch anything outside
  `assetCleanupRoot` or actors without `actorPrefix`. Cleanup hooks rely on
  these prefixes.
- **One-shot**: tell Claude not to ask the user for input and not to pause —
  the runner is non-interactive.

### Writing requirements

- Reuse primitives in `helpers/validators.ts` first; add new ones only when
  none of them fit.
- Each `check()` returns `{ passed, detail, observed }`. `observed` shows up
  in the HTML report under an expandable "observed" block, so make it useful
  for triage when a requirement fails.
- Validators are read-only — they query the editor via MCP `*_query` actions
  or `ue5_run_python` introspection. Never mutate the level inside a check.

## File layout

```
tests/e2e/
├── helpers/
│   ├── requirement.ts    # Scenario / Requirement types
│   ├── validators.ts     # Reusable assertion primitives
│   └── cleanup.ts        # Per-prefix actor + asset cleanup
├── runner/
│   ├── mcp-config.ts     # Writes a temp .mcp.json for the spawned Claude
│   ├── claude-runner.ts  # Spawns `claude --print` with the bridge attached
│   └── report-writer.ts  # JSON + self-contained HTML report
├── scenarios/
│   └── tiny-arena.ts     # First scenario
├── tiny-arena.e2e.test.ts
├── .results/             # Output (gitignored — see below)
└── README.md
```

`.results/` is local-only — add it to your `.gitignore` if it isn't already.
