# <Your Project Name>

<!--
  Starter CLAUDE.md for a UE5 project using the Arbor toolkit.

  Copy this file to the root of your UE5 project (next to your .uproject),
  rename it to `CLAUDE.md`, and fill in the sections below. The Arbor
  plugin launches Claude with cwd = project root, so this is the first
  file Claude reads when you open the in-editor Chat widget.

  The `@Plugins/Arbor/CLAUDE.md` import below pulls in the full Arbor API
  reference automatically — you do NOT need to copy its contents here.
-->

## Project

Briefly describe your game: genre, tone, core loop, target platform. Two or three sentences.

## Arbor toolkit

This project uses [Arbor](https://github.com/arborsmith/arbor). Claude's Arbor-specific knowledge is loaded automatically from the plugin's docs:

@Plugins/Arbor/CLAUDE.md

<!-- If you're using the private overlay plugin, uncomment: -->
<!-- @Plugins/ArborOverlay/CLAUDE.md -->

## Project-specific conventions

Anything unique to *this* project that Arbor's general rules don't cover. Delete any sections that don't apply, and replace the examples with your actual values.

### Asset paths
```
Meshes       → /Game/Meshes/
Materials    → /Game/Materials/
Textures     → /Game/Textures/
Characters   → /Game/Characters/
Environments → /Game/Environments/<Biome>/
```

### Naming conventions
```
Static meshes         SM_<Name>
Materials             M_<Name>
Material instances    MI_<Name>
Textures              T_<Name>_<Albedo|Normal|Roughness|...>
Blueprints            BP_<Name>
AI controllers        BP_<NPC>AI
Behavior trees        BT_<NPC>
Data assets           DA_<Name>
```

### Default game context

Point Claude at a canonical `UArborGameContextAsset` so codex operations default to the right world:
```
Default: /Game/GameCodex/GC_MyGame
```

### Level structure

*Describe sub-level organization, persistent vs streaming, etc. Example:*
```
L_Main         persistent level (lighting, atmosphere)
L_Arena        gameplay sub-level (levels, encounters)
L_Interiors    streaming sub-level (houses, caves)
```

### Source control

*Document any project-specific conventions — do-not-edit paths, how changelists should be grouped, any areas requiring special coordination. Delete this section if your project has no special rules.*

## Reference

- **Plugin API**: `@Plugins/Arbor/CLAUDE.md` (imported above)
- **MCP tools**: expose yourself to `ue5_actors`, `ue5_blueprint`, `ue5_terrain`, etc. via the `ue5-bridge` MCP server — see Arbor's bridge README for setup.
- **Report bugs in Arbor itself**: call the `report_issue` MCP tool — it files to [arborsmith/arbor](https://github.com/arborsmith/arbor/issues).
