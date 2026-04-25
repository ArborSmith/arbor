# `@arborsmith/ue5-bridge`

MCP (Model Context Protocol) server that bridges Claude to a running Unreal Engine 5 editor via the Remote Control API.

Part of the [Arbor toolkit](https://github.com/ArborSmith/arbor) — see the root README for the full picture.

## Install

```bash
npm install
npm run build
```

Requires Node.js 20+.

## Register with Claude Code

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

## Environment variables

| Var | Default | Purpose |
|---|---|---|
| `UE5_REMOTE_PORT` | `30010` | Port the UE5 Remote Control API listens on |
| `GITHUB_TRACKER_REPO` | (required for `report_issue`) | Repo to file issues in via `gh` CLI |
| `ARBOR_TOOLS` | (queries UE5 settings) | Override which tool categories load. Values: `stable`, `all`, or comma list (`codex,environment`) |

## Run tests

```bash
npm test
```

Integration tests use `describe.runIf(editorRunning)` and skip cleanly if UE5 isn't reachable. See `tests/integration/` for what's covered.

## Tool list

See `CLAUDE.md` in this directory for the full registered tool catalogue and architectural notes.
