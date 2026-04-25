# Arbor Bridge — `ue5-bridge` MCP server

TypeScript MCP server that exposes the Arbor UE5 plugin's capabilities as tools Claude can call. Talks to UE5 via the Remote Control API on port 30010.

## Architecture

```
src/
├── index.ts                    # Server entry — registers stable + experimental + overlay
├── ue5-client.ts               # Low-level Remote Control API wrapper
├── tool-factory.ts             # registerCategoryTool() — handles read/write split
├── features.ts                 # Boot-time feature query (env var or UE5 settings)
├── annotation-server.ts        # Express server for the annotate-screenshot UI
├── extensions.local.d.ts       # Type stub for the optional private overlay hook
├── registry/
│   ├── stable.ts               # 11 stable category tools (always loaded)
│   ├── experimental.ts         # 5 experimental category tools (gated)
│   ├── types.ts                # CategoryTool + ActionHandler interfaces
│   ├── index.ts                # Per-tool re-exports
│   └── <category>.ts           # Per-category implementations (one file each)
└── tools/core/                 # Standalone tools (ping, run-python, report-issue)
```

## Tool registration

Each MCP tool is a `CategoryTool` (see `registry/types.ts`):

```ts
interface CategoryTool {
  description: string;
  schema: Record<string, z.ZodTypeAny>;
  actions: Record<string, ActionHandler>;
  readOnlyActions?: string[];   // automatically split into `<name>_query` tool
  actionParams?: Record<string, ActionParamSpec>;
}
```

`registerCategoryTool(server, "ue5_<name>", tool)` in `tool-factory.ts` does the work — including auto-creating a `_query` companion tool for read-only actions, so write tools stay separate from queries (helps with permission UX).

## Feature flags

`fetchEnabledFeatures()` in `features.ts`:

1. Checks `ARBOR_TOOLS` env var first. Values: `stable`, `all`, or comma list (`codex,environment`).
2. If unset, queries UE5 via `PUT /remote/object/call` on `/Script/Arbor.Default__ArborSettings` calling `GetEnabledFeaturesJson` (2-second timeout).
3. Falls back to stable-only if UE5 isn't reachable.

Result drives which categories `index.ts` registers.

## Adding a new category

1. Create `src/registry/<name>.ts` exporting a `CategoryTool`
2. Add to `stable.ts` if well-tested, or `experimental.ts` (with a `featureKey`) if not
3. If experimental, add the matching flag to `../Source/Arbor/Public/ArborSettings.h` and `GetEnabledFeaturesJson()` in `ArborSettings.cpp`
4. Update `Features` interface in `features.ts` and `ALL_EXPERIMENTAL_KEYS`
5. Write integration tests under `tests/integration/<name>.test.ts`

## Stable tools (always loaded)

- **ue5_ping** — Health check
- **ue5_run_python** — Escape hatch: execute arbitrary Python in UE5
- **ue5_actors** — Actor CRUD: spawn, place, modify, delete, snap_to_ground, scatter
- **ue5_blueprint** — Blueprint editing: nodes, pins, components, character/AI controller creation, anim graph
- **ue5_ai** — Behavior trees + EQS: create/edit/query
- **ue5_terrain** — Landscape: create, paint, water bodies
- **ue5_materials** — Materials, instances, world-aligned
- **ue5_lighting** — Outdoor/indoor scene setup, post-process
- **ue5_foliage** — HISM foliage instancing
- **ue5_mesh** — Pivot, scale, collision fixes
- **ue5_assets** — find, scan, stats, import
- **ue5_capture** — Screenshots + browser-based annotation
- **ue5_playtest** — PIE control + WASD input

## Experimental tools (gated by `bEnableExperimentalFeatures` + per-feature flags)

- **ue5_codex** — Game Codex: design-bible CRUD across categories (location, character, feature, etc.) including structured `character_*` actions
- **ue5_environment** — Anchor-graph spatial building
- **ue5_anchors** — Mesh anchor metadata + debug visualization
- **ue5_concept_art_studio** — Unified concept art generation pipeline
- **ue5_pcg** — PCG graph editing + execution; landscape scattering

## Reporting bugs

`report_issue` MCP tool creates a GitHub issue via the `gh` CLI in the repo named by `GITHUB_TRACKER_REPO` env var (default: `ArborSmith/arbor`). Requires `gh` to be installed and authenticated.

## Setup

### UE5 side (one-time)
1. Enable `Remote Control API` plugin (Edit → Plugins)
2. Project Settings → Plugins → Remote Control → check "Enable Remote Execution"
3. Verify: open `http://127.0.0.1:30010/remote/info` — should return JSON

### Bridge side
```bash
npm install
npm run build
```

Register with Claude Code:
```bash
claude mcp add ue5-bridge node /absolute/path/to/bridge/dist/index.js
```

Or in `~/.claude/settings.json`:
```json
{
  "mcpServers": {
    "ue5-bridge": {
      "command": "node",
      "args": ["/absolute/path/to/bridge/dist/index.js"],
      "env": {
        "UE5_REMOTE_PORT": "30010",
        "GITHUB_TRACKER_REPO": "ArborSmith/arbor",
        "ARBOR_TOOLS": ""
      }
    }
  }
}
```

## Remote Control API gotchas

- All requests are `PUT` with JSON body (except `/remote/info` which is `GET`)
- Most tools call C++ UFUNCTIONs directly via `/Script/Arbor.Default__<ClassName>` — no Python in the hot path
- `ExecuteConsoleCommand` lives on `KismetSystemLibrary`, not `EditorLevelLibrary`
- Python scripts (used by screenshot, playtest input, asset import) run via `py /path/to/script.py` — fire-and-forget, results land in Output Log not the HTTP response. Use `arbor.utils.write_result()` to ferry data back via `Saved/Arbor/last_result.json`.

## Generated image output

When a tool generates images (concept art, textures), the bridge expects `output_dir` to point at a folder inside the UE5 project — typically `<ProjectDir>/Art/ConceptArt/` or `<ProjectDir>/Art/Textures/`. Get the project dir via `ue5_run_python` → `unreal.Paths.project_dir()`.
