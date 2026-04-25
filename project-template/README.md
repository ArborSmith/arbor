# Project template

Drop-in starter files to bootstrap a UE5 project for use with Arbor.

## `CLAUDE.md`

Copy this file to the **root of your UE5 project** (next to your `.uproject`), then edit the placeholder sections. The Arbor Chat widget launches Claude with cwd set to the project root, so this is the first file Claude reads each session.

```powershell
# From your UE5 project root
Copy-Item <path-to-arbor>\project-template\CLAUDE.md .\CLAUDE.md
```

The template uses Claude's `@path/file.md` import syntax to pull in `Plugins/Arbor/CLAUDE.md` automatically — you don't need to duplicate Arbor's API reference, just edit the project-specific sections.

## What's _not_ in here (yet)

- No `.claude/` directory template — start with the defaults and customize as you go
- No `.mcp.json` — MCP server registration lives in `~/.claude/settings.json` globally, not per-project
