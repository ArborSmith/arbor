# Contributing to Arbor

Thanks for your interest. Arbor is an evolving toolkit and contributions are welcome — bug reports, fixes, new MCP categories, plugin builders, or docs improvements.

## Dev setup

### Plugin side (C++)
- Unreal Engine 5.4+
- A C++ UE5 project with `Plugins/Arbor/` symlinked or copied into it
- Live Coding enabled for fast iteration on builders

### Bridge side (TypeScript)
- Node.js 20+
- `cd bridge && npm install`

## Running tests

The bridge has integration tests that talk to a live UE5 editor:

```bash
cd bridge
npm test
```

Tests use `describe.runIf(editorRunning)` — if UE5 isn't running, those tests skip gracefully. To exercise the full suite:

1. Open a UE5 project with the Arbor plugin loaded
2. Confirm Remote Control API is enabled and listening on port 30010
3. Run `npm test`

Test assets are written under `/Game/IntTest/` and cleaned up in `afterAll` via `deleteTestAssets()`. Tests use unique names (`IntTest_<timestamp>_<counter>`) to avoid collisions.

**Tests validate Arbor's builders and MCP actions, not UE5 itself.** Verify that Arbor produces the correct output (e.g. correct properties on a CDO/component) — don't test UE5 engine behavior.

## Adding a new MCP tool category

1. Create `bridge/src/registry/<name>.ts` exporting a `CategoryTool`
2. Add it to either `bridge/src/registry/stable.ts` or `experimental.ts` (choose based on test coverage and stability)
3. If experimental, add a `featureKey` and a matching flag in `Source/Arbor/Public/ArborSettings.h`
4. Add integration tests under `bridge/tests/integration/`
5. Document the new actions in `bridge/CLAUDE.md` and the root `CLAUDE.md` if relevant

## UE version compatibility

Arbor targets **UE 5.4+**. When Epic's API changes between versions (e.g. the landscape edit-layer refactor in 5.5), use preprocessor guards to support both sides rather than dropping the older version. Pattern:

```cpp
#include "Runtime/Launch/Resources/Version.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    // 5.5+ API path
#else
    // 5.4 fallback
#endif
```

Put the conditional behind a single helper function (e.g. `GetFirstEditLayerGuid()` in `LandscapeBuilder.cpp`) so call sites don't fork — only the helper does. Comment the `#if` block with one line explaining what changed and in which version.

When bumping the minimum UE version, update `Arbor.uplugin`'s `EngineVersion` field and mention the bump in the PR description.

## PR guidelines

- One logical change per PR — small + focused beats large + sweeping
- Write or update integration tests for new functionality
- Run `npm test` locally and confirm stable categories still pass (codex tests have known pre-existing failures unrelated to your change — note in the PR if anything new fails)
- Update `CLAUDE.md` files if your change affects builder/MCP behavior — those are the source of truth that Claude reads
- Keep commits focused and well-described; the maintainer may rebase/squash on merge

## Reporting bugs

Use the GitHub issue tracker at https://github.com/arborsmith/arbor/issues. The bridge ships with a `report_issue` MCP tool that auto-creates well-formatted issues via the `gh` CLI — Claude can call it directly when it hits an Arbor bug.

## Code of conduct

Be kind, be specific, assume good intent. This is a small project; we don't have a formal CoC document yet.
