# Arbor

> **AI-driven gamedev toolkit for Unreal Engine 5.** Lets Claude build levels, blueprints, behavior trees, materials, and more — directly inside the editor.

> ⚠️ **Early stage — not production ready.** Arbor is published as open source so people can try it, learn from it, and contribute. APIs will change, things will break, tools will move between stable/experimental, and there is no support contract. Don't use it on a project where stability matters yet.

Arbor is:
- A UE5 editor plugin (C++ + Python) exposing builders for actors, blueprints, behavior trees, EQS, terrain, PCG, materials, lighting, foliage, and a Game Codex design system.
- An MCP bridge server (`bridge/`) that translates Claude's tool calls into Remote Control API requests against the running UE5 editor.

## Status

Open source, early access — APIs unstable. The 11 stable MCP categories (actors, blueprint, AI, terrain, materials, lighting, foliage, mesh, assets, capture, playtest) have integration coverage. 5 experimental categories (codex, environment, anchors, concept-art-studio, pcg) ship behind a feature flag, off by default.

## Quick start

### Requirements
- **Unreal Engine 5.4+** with a **C++ project** (Arbor is an editor C++ plugin — Blueprint-only projects won't compile it)
- **Node.js 20+** (older Node versions produce cryptic errors)
- **Claude Code CLI** installed and on your **system or user `PATH`** — not just a shell-specific profile (`$PROFILE.ps1`, `.bashrc`). UE5 inherits `PATH` from the OS environment, not from your terminal session.
  - **Windows: log out / log back in (or restart) after installing Claude Code.** Windows doesn't reliably propagate `PATH` updates to already-running processes, including the Explorer that spawns UE5. If `claude` works fine in your terminal but the in-editor Arbor Chat reports `Claude process exited (code 1)`, this is almost always the cause. Verify with a *fresh* `cmd` window: `where claude` should show a path. If it does, UE5 will see it too.
- **`gh` CLI** installed and authenticated (only if you want `report_issue` to file GitHub issues)

> ⚠️ **Security note** — the `ue5_run_python` tool executes arbitrary Python inside the UE5 editor with full filesystem + process access. Treat any Claude/MCP session you run against Arbor the same way you'd treat running code from a peer reviewer. Don't expose the Remote Control API port (30010) outside localhost.

> ⚠️ **Issue filing is public by default.** The `report_issue` MCP tool files bug reports on whatever `GITHUB_TRACKER_REPO` you configure (default: `ArborSmith/arbor` — public). Two-layer guard before anything ships:
>
> 1. Claude is instructed (via the tool schema and `CLAUDE.md`) to substitute project-specific identifiers (asset paths, Blueprint/class/level/character names, IP references, absolute filesystem paths) with generic placeholders before submitting.
> 2. The bridge opens a **local browser-based review UI** showing the rendered title and body, with editable fields and a generic-leak scanner. Click Submit to file, Cancel to abort. You are the final checkpoint.
>
> Bypass the review UI for headless/CI contexts with `ARBOR_ISSUE_REVIEW=skip`. Point `GITHUB_TRACKER_REPO` at a tracker you control to keep issues private.

### 1 — Install the plugin

Clone this repo **directly into your UE5 project's `Plugins/` directory**:

**PowerShell / Windows:**
```powershell
cd <YourProject>\Plugins
git clone https://github.com/ArborSmith/arbor.git Arbor
```

**bash / macOS / Linux:**
```bash
cd <YourProject>/Plugins
git clone https://github.com/ArborSmith/arbor.git Arbor
```

The repo itself IS the plugin — `Arbor.uplugin` lives at the root. The `bridge/` and `project-template/` folders are maintainer extras that UE5 ignores (no `.uplugin` in them).

Regenerate your UE5 project files (right-click the `.uproject` → "Generate Visual Studio project files" on Windows) and rebuild the project from your IDE. UE5 loads the plugin on next editor launch.

### 2 — Enable UE5 Remote Control API

With the editor open and Arbor loaded:

1. `Edit → Plugins` → search **"Remote Control API"** → enable → restart editor
2. `Edit → Project Settings → Plugins → Remote Control` → check **"Enable Remote Execution"**
3. Verify in a terminal:
   ```bash
   curl http://127.0.0.1:30010/remote/info
   ```
   You should see JSON like `{"HttpServerVersion":"0.1","RCVersion":{...}}`. If you get `connection refused`, the plugin didn't enable correctly — re-check step 1 and make sure the editor is still running.

### 3 — Build the MCP bridge
```bash
cd <YourProject>/Plugins/Arbor/bridge
npm install
npm run build
```

### 4 — Register the bridge with Claude Code

**Recommended: edit `~/.claude/settings.json`** (works with env vars, which you need for `report_issue`):

```json
{
  "mcpServers": {
    "ue5-bridge": {
      "command": "node",
      "args": ["/absolute/path/to/YourProject/Plugins/Arbor/bridge/dist/index.js"],
      "env": {
        "UE5_REMOTE_PORT": "30010",
        "GITHUB_TRACKER_REPO": "ArborSmith/arbor"
      }
    }
  }
}
```

(Alternatively `claude mcp add ue5-bridge node <path>/bridge/dist/index.js` — but that can't pass env vars, so `report_issue` will silently refuse to file issues. Stick with `settings.json` unless you don't care about bug reporting.)

If you maintain a per-project `.mcp.json` at your UE5 project root instead, the same shape applies.

### 5 — Bootstrap your project's `CLAUDE.md`

Arbor's in-editor Chat widget launches Claude with the UE5 project root as cwd. Copy the starter template so Claude picks up Arbor's API docs plus any project-specific conventions:

**PowerShell:**
```powershell
Copy-Item <YourProject>\Plugins\Arbor\project-template\CLAUDE.md <YourProject>\CLAUDE.md
```

**bash:**
```bash
cp <YourProject>/Plugins/Arbor/project-template/CLAUDE.md <YourProject>/CLAUDE.md
```

The template uses `@Plugins/Arbor/CLAUDE.md` to auto-import Arbor's full reference — no copy-paste needed.

### 6 — Verification & first run

Before the "red cube" demo, confirm each link in the chain:

1. **UE5 editor** is running with the Arbor plugin loaded and Remote Control API enabled.
2. **Bridge is reachable by Claude.** In Claude Code or the Arbor Chat widget, ask: *"list your ue5_* tools"*. You should see `ue5_ping`, `ue5_actors`, etc. If nothing's there, your MCP config is wrong — re-check step 4.
3. **Plugin is reachable by the bridge.** Ask Claude: *"call ue5_ping"*. Expected: `{"success": true, ...}`. If it times out or errors, the editor isn't running or Remote Control isn't enabled.
4. **CLAUDE.md is loaded.** Ask: *"which Python modules does `arbor` expose?"*. Claude should list them from memory.

### 7 — Try it

> "Spawn a red cube at the origin, take a screenshot."

Claude calls `ue5_actors`, `ue5_materials`, and `ue5_capture` in sequence. Cube appears, screenshot lands on disk, Claude confirms.

### Changing experimental feature flags

Flags live in `Edit → Project Settings → Plugins → Arbor → Experimental Features`. **Toggling a flag requires restarting the bridge** — the bridge queries flags once at boot, not per-request.

## Architecture

```
┌────────────┐   MCP/stdio   ┌──────────────┐   HTTP PUT    ┌─────────────────┐
│   Claude   │ ────────────▶ │ ue5-bridge   │ ────────────▶ │  UE5 editor     │
│  (or CC)   │  tool calls   │  MCP server  │  /remote/...  │  + Arbor plugin │
└────────────┘ ◀──────────── └──────────────┘ ◀──────────── └─────────────────┘
                JSON results                  JSON results
```

Most bridge tools call C++ UFUNCTIONs directly (no Python in the hot path). Python is reserved for screenshots, playtest input, and asset import — operations that need the editor's main thread.

## Repo layout

```
Arbor/                       # The repo — lives at <YourProject>/Plugins/Arbor/
├── Arbor.uplugin            # UE5 plugin manifest (repo root)
├── Source/Arbor/            # C++ builders + tools
├── Content/Python/arbor/    # Python utility library (~30 modules)
├── Config/                  # UE5 packaging filter
├── CLAUDE.md                # Plugin API reference (Claude reads this)
├── README.md                # This file
├── CONTRIBUTING.md
├── LICENSE
├── bridge/                  # MCP server — UE5 ignores (no .uplugin)
│   ├── package.json
│   ├── src/
│   ├── tests/
│   └── CLAUDE.md
├── project-template/        # Starter CLAUDE.md for your UE5 project root
│   ├── CLAUDE.md
│   └── README.md
└── .github/                 # CI + issue templates
```

## Experimental features

Off by default. Enable in UE5: `Edit → Project Settings → Plugins → Arbor → Experimental Features → Enable Experimental Features` (master switch + per-feature toggles), then restart the bridge.

The 5 experimental categories:
- **codex** — Game design bible (locations, characters, features, lore as structured data assets)
- **concept_art_studio** — Unified concept art generation pipeline
- **environment** — Anchor-graph spatial building
- **anchors** — Mesh anchor metadata + debug visualization
- **pcg** — Procedural Content Generation graphs + landscape scattering

Override at the bridge level with `ARBOR_TOOLS=stable | all | <comma-list>`.

## Documentation

- **`CLAUDE.md`** — Plugin API reference. Read this if you're extending Arbor or want Claude to write good Arbor code.
- **`bridge/CLAUDE.md`** — Bridge tool reference + setup details.
- **`CONTRIBUTING.md`** — Dev setup, test running, PR guidelines.

## Troubleshooting

### Arbor Chat shows `Claude process exited (code 1)` immediately

Most common cause on Windows: `claude` is on `PATH` for your terminal session but not for UE5's process tree. The fix is almost always **log out / log back in (or restart Windows)** after installing Claude Code, then relaunch UE5. See the Requirements section for why.

Verify the fix worked: open a **fresh** `cmd` window (not one that was open before the install) and run `where claude`. If it shows a path, UE5 will see it too. If it shows nothing, the directory containing `claude.exe` (or `claude.cmd`) isn't on your system/user `PATH` — add it via *System Properties → Environment Variables* (don't put it in just `$PROFILE.ps1`), then log out / back in.

### `import arbor.registry` fails on editor startup

`Edit → Plugins`, confirm both **Arbor** and **Python Editor Script Plugin** are enabled, then close UE5 completely (kill any leftover `UnrealEditor.exe` in Task Manager) and reopen. The Python plugin needs a clean restart to pick up Arbor's `Content/Python/` directory.

### Bridge tools missing in Claude (`ue5_pcg`, etc. don't show up)

Experimental categories are gated. Open `Edit → Project Settings → Plugins → Arbor → Experimental Features`, flip the master switch, enable the per-feature toggle for the category you want, then **restart the bridge** (the bridge queries flags once at boot, not per-request).

## License

MIT — see [`LICENSE`](LICENSE).
