# Arbor — Gamedev AI Toolkit for UE5

Editor-only Unreal Engine 5 plugin: builds BehaviorTree, Blueprint, and EQS assets from JSON **and** provides a Python utility library for level building, lighting, materials, scattering, terrain, and AI navigation setup. Designed to be driven by Claude via the `ue5-bridge` MCP server.

**If you're working on the MCP bridge side, read `bridge/CLAUDE.md` as well** — this file covers the plugin (C++ + Python); `bridge/CLAUDE.md` covers the TypeScript MCP server.

## Plugin Structure

The repo's root IS the plugin — clone it directly into `<YourProject>/Plugins/Arbor/` and UE5 finds `Arbor.uplugin` at the expected path.

```
Arbor/                             # Repo root = UE5 plugin root
├── Arbor.uplugin
├── Source/Arbor/                  # C++ asset builders + tools (13 UCLASS files, ~115 UFUNCTIONs)
│   ├── Public/                    # Headers
│   └── Private/                   # Implementations + console command registration
├── Content/Python/arbor/          # Python utility library (28 modules, auto-discovered by UE5)
├── Config/                        # UE5 packaging filter
├── bridge/                        # MCP server (UE5 ignores — no .uplugin in it)
├── project-template/              # Starter CLAUDE.md for UE5 project roots
└── .github/                       # CI + issue templates
```

---

## Python Modules

All modules use `import unreal` internally. UE5 auto-adds `Content/Python/` to `sys.path`. All functions accept plain tuples for coordinates.

**Usage:** `import arbor.layout` or `from arbor import make_room`

### Module Index

| Module | Purpose |
|--------|---------|
| `utils` | Foundation: type coercion, actor find/spawn/delete, `write_result()` MCP bridge, `safe_run()` |
| `actors` | Actor inspect, transform, duplicate, select, bounds, `snap_to_ground()` |
| `layout` | Blockout primitives: `spawn_primitive`, `make_wall`, `make_floor`, `make_room`, `make_ramp`, `make_stairs` |
| `materials` | Create/assign materials and material instances |
| `lighting` | Lights, atmosphere, post-process; `setup_outdoor_scene()`, `setup_indoor_scene()` |
| `terrain` | Landscape creation, heightmaps, layer painting, water bodies (rivers/lakes) |
| `scatter` | `scatter_meshes()` for <50 actors with ground snapping |
| `foliage` | Instanced foliage (HISM) for >50 identical instances: grass, flowers, ground cover |
| `pcg` | PCG graph creation/editing/execution, preset helpers, `scatter_on_landscape` |
| `nav` | NavMesh volumes, `build_paths()`, AI controller setup, EQS preset builders |
| `blueprints` | Asset creation + granular editing for BP, BT, EQS, AnimGraph, components |
| `mesh` | Pivot correction, scale fix, collision setup |
| `vfx` | Local fog volumes, Niagara search/spawn, decals |
| `structure` | 2D floor plans → 3D buildings: `build_from_plan`, `make_house`, `make_tower`, `make_castle` |
| `capture` | Screenshots: viewport, top-down, orbit, custom camera position |
| `inspect` | Actor/asset/Blueprint property introspection |
| `textures` | AI texture review, import, PBR material creation |
| `preview` | Open assets in UE5 for visual inspection |
| `registry` | Project asset index: `scan_project`, `find_asset`, `find_meshes`, `find_materials` |
| `playtest` | PIE testing: start/stop, teleport, screenshot, walk_path, run_playtest |
| `tags` | FGameplayTag construction by string and assignment to UPROPERTYs (works around UE's read-only `tag_name`) |
| `compile` | Live Coding helpers: trigger compiles, block on completion, poll state (works around no-Python-signal for OnPatchComplete) |
| `automation` | Run UE automation tests with structured JSON results — bypasses controller queue + FPS gate |

### Key Behavioral Rules

- **Never guess at class names or parameters.** Use runtime discovery: `list_bt_types`, `list_eqs_generators`, `list_node_types`, `list_component_types`, `get_class_params`, `list_functions`. These query UE5 at runtime and include project/plugin classes.
- **Never guess at property names.** Use `arbor.inspect` first: `inspect_actor("Name")`, `find_property("Name", "mesh")`
- **Never guess asset paths.** Use `arbor.registry` first: `find_meshes("rock")`, `find_materials("metal")`
- **Never ask the user to pick textures/materials/meshes from text descriptions.** Open them in UE5 instead with `arbor.preview`.
- **scatter vs foliage vs PCG:** `arbor.scatter` for <50 unique actors. `arbor.foliage` for >50 identical instances. `arbor.pcg` for GPU-instanced landscape scattering.
- **Structures:** Describe buildings as 2D floor plans via `arbor.structure`, NOT as individual wall coordinates.
- **Screenshots:** Always take a screenshot after major layout changes to verify placement.
- **Check existing project assets first.** Before downloading from Fab or generating, search with `arbor.registry`.
- **Prefer Fab over AI-generated assets.** Search Fab first, fall back to AI generation only if needed.
- **Fab search settings.** Read user preferences via `arbor.utils.get_settings()` and pass `include_thumbnails` accordingly (key: `fab_search_thumbnails`, default `true`). Always present results and let user choose before claiming.
- **Fab download location.** Place imported Fab assets under `/Game/Fab/<PackName>/`.
- **Fab login.** Call `arbor.fab.check_fab_login()` before any Fab operation. If not logged in, ask the user to sign in via the Fab window in UE5 Editor (Window → Fab).
- **Fab import workflow.** Use `arbor.fab.import_asset(download_url, asset_id, asset_name, asset_type, ...)` via `ue5_run_python` to trigger the Fab plugin's native import. Import runs asynchronously and returns immediately — poll with `arbor.fab.get_import_status(asset_id)` until done, then clear the tracking entry with `arbor.fab.clear_import_tracking(asset_id)`. For direct file downloads outside Fab (local FBX/GLB), use `ue5_assets(action="import")` instead.
- **Fab library listing.** `arbor.fab.fab_library(page=1, per_page=20)` returns assets already in the user's Fab library. Useful to check what they've already claimed before suggesting an import.
- **Report broken/missing Arbor features.** Call the `report_issue` MCP tool with `type="bug"` or `type="feature_request"`. Do this every time — don't silently work around Arbor bugs.
- **Sanitize project specifics before filing issues.** GitHub issues are PUBLIC. Replace project-specific identifiers (asset paths, Blueprint/class/level/character names, IP and storyline references, absolute filesystem paths) with generic placeholders like `/Game/MyAssets/SM_Cube`, `BP_MyCharacter`, `<ProjectRoot>/...` before passing them to `report_issue`. Keep the technical content (which Arbor API was called, expected vs actual behavior, traceback structure) — strip the names. Do not assume the user's project is intended to be public.
- **Issue tracker.** All issues go to **`ArborSmith/arbor`** on GitHub.
- **Never use raw curl** to talk to UE5 or the Remote Control API. Always use MCP tools or `ue5_run_python` with `arbor.*` modules.
- **NEVER use raw `unreal` API calls** (e.g. `set_editor_property`, `get_default_object`) to set properties on Blueprint CDOs, mesh components, or anim classes. These calls appear to succeed but silently fail to hook up references at runtime. Always use Arbor builders (`create_character`, `create_ai_controller`, `setup_anim`) which handle internal wiring correctly. If an Arbor builder doesn't set a property, report a bug — do NOT fall back to raw `unreal` calls as a workaround.
- **Snapping to ground:** Use `arbor.actors.snap_to_ground(name, offset=0.0)`, `snap_all_to_ground(filter_labels=[...])`, or `snap_selected_to_ground()`. Never write inline line-trace code.
- **VFX:** Local fog: `arbor.vfx.add_local_fog_volume()`. Niagara: `arbor.vfx.spawn_niagara_system()`. Decals: `arbor.vfx.spawn_decal()` / `scatter_decals()`. Always call `list_niagara_systems()` first before spawning. Engine Niagara content may need "Niagara Extra" plugin enabled.
- **Meshy import:** Always use `auto_fix_pivot: true` when importing Meshy-generated assets to fix pivot to bottom.
- **Collision for Meshy models:** Trees/rocks/props: `fix_collision(path, "complex_simple")`. Characters: `fix_collision(path, "capsule")`. Floors/walls: `fix_collision(path, "complex_only")`. Small decoration: `disable_collision(path)`.
- **No auto-memory for Arbor.** Never save memories about Arbor APIs, module usage, class names, parameters, or workarounds. This CLAUDE.md is the single source of truth — it is always loaded. Saving memories would create stale duplicates.

---

## UE5 Coordinate System

| Axis | Direction | Notes |
|------|-----------|-------|
| **X** | Forward | |
| **Y** | Right | |
| **Z** | Up | |
| **Units** | Centimetres | 1 unit = 1 cm |
| **Rotator** | `(Pitch, Yaw, Roll)` | Degrees |

> **IMPORTANT: Always use named parameters for `unreal.Rotator`.** Positional args silently put values in wrong fields. Always write `unreal.Rotator(pitch=0, yaw=45, roll=0)`, NEVER `unreal.Rotator(0, 45, 0)`.

A default BasicShapes cube is **100 x 100 x 100 cm** centred at its pivot. Scale of `(2, 3, 1)` → 200 x 300 x 100 cm.

### Common Asset Paths

```
/Engine/BasicShapes/Cube.Cube
/Engine/BasicShapes/Sphere.Sphere
/Engine/BasicShapes/Cylinder.Cylinder
/Engine/BasicShapes/Cone.Cone
/Engine/BasicShapes/Plane.Plane
/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial
```

---

## MCP Bridge

All arbor modules work in `ue5_run_python`. Tuples work for coordinates. `arbor.utils.write_result(data)` writes JSON to `Saved/Arbor/last_result.json` for the bridge to read back. Most MCP tools now call C++ UFUNCTIONs directly — only screenshot, playtest input, and asset import still use Python.

### Meshy Import Scale

Meshy exports metres, UE5 uses centimetres — models import **100x too small**. `ue5_assets(action="import")` auto-scales by default. To fix already-imported meshes: `arbor.mesh.fix_mesh_scale(path, 100.0)`.

---

## ArborAnchorComponent (Anchor Debug Visualization)

`UArborAnchorComponent` is an editor-only tick component that auto-draws anchor debug shapes for any `StaticMeshActor` it's attached to.

**How it works:**
- On register, resolves the owning actor's static mesh → loads anchors from registry (or legacy sidecar JSON)
- Every tick, draws single-frame debug shapes (spheres + arrows + labels) in world space — no persistent draws
- Controlled by `UArborSettings::bShowAnchorDebug` (Project Settings → Arbor → "Show anchor debug visualization")
- Has **distance culling** (5000cm from editor camera) — only anchors near the viewport are drawn

**Adding to actors (C++):**
- `UArborAnchorAnalyzer::AddAnchorDebugToActors(LabelPrefix)` — batch-adds the component to all `StaticMeshActors` matching a label prefix. Skips actors that already have it. Returns `{success, added, skipped}`.

**Adding to actors (Python):**
```python
import unreal, json
result = json.loads(unreal.ArborAnchorAnalyzer.add_anchor_debug_to_actors("Cat_"))
# {success: true, added: 143, skipped: 0}
```

**Enable/disable debug drawing:**
```python
cdo = unreal.get_default_object(unreal.load_class(None, "/Script/Arbor.ArborSettings"))
cdo.set_editor_property("bShowAnchorDebug", True)  # or False
```

**Note:** `AddAnchorDebugToActors` is a UFUNCTION — requires a full editor restart after compilation to be available (LiveCoding cannot add new UFUNCTIONs, only patch existing ones).

---

## ArborLayoutSolver — Wall Placement Rules

**CRITICAL: Never chain walls to walls via `wall_right → wall_left`.** This makes all walls extend in a straight line (BFS follows the chain). Instead, connect each wall independently to a floor tile's `wall_snap` anchor. The floor edge orientation gives each wall the correct rotation automatically.

### Anchor Pairings by Wall Orientation

| Wall runs along | Floor anchor | Wall anchor | Result yaw |
|-----------------|-------------|-------------|------------|
| **South** (east-west at min X) | `south_edge_west` | `wall_left` | 90° |
| **North** (east-west at max X) | `north_edge_west` | `wall_left` | 90° |
| **West** (north-south at min Y) | `west_edge_south` | `wall_left` | 0° |
| **East** (north-south at max Y) | `east_edge_south` | `wall_right` | 180° |

Each 300cm wall covers 2 floor tiles (150cm each). Connect to the tile at the start of the wall's span using the even-column (or even-row) tile.

### Example: Perimeter wall segment
```python
# South wall segment covering cols 0-1:
add_wall("sw_0", WALL_MESH, "f_0_0", "south_edge_west", "wall_left")
# South wall segment covering cols 2-3:
add_wall("sw_1", WALL_MESH, "f_0_2", "south_edge_west", "wall_left")
```

### Interior partitions
- East-west partitions (like room dividers): use `north_edge_west → wall_left` on the row just south of the partition
- North-south partitions: use `east_edge_south → wall_right` on the column just west of the partition

---

## GameCodex

The GameCodex is a **high-level game design documentation system** — it captures design intent, narrative context, and world-building data. It is **not** a mechanical stat database.

### Design Philosophy

- Categories capture design intent, narrative context, and world-building — not specific numeric values
- When adding fields to codex types, prefer descriptive text and tag arrays over numeric stats
- Each entry documents the creative vision; mechanical implementation lives in Blueprints/DataTables

### Consistency Contract

All codex data asset types (in `ArborGameContextTypes.h`, `ArborCharacterTypes.h`) **must** have these shared properties:

| Property | Type | Category |
|----------|------|----------|
| `GameContext` | `TSoftObjectPtr<UArborGameContextAsset>` | varies |
| `ConceptArt` | `TSoftObjectPtr<UTexture2D>` | `"Concept Art"` |
| `ConceptArtGallery` | `TArray<TSoftObjectPtr<UTexture2D>>` | `"Concept Art"` |
| `ConceptArtPrompt` | `FString` | `"Concept Art"` |
| `Tags` | `TArray<FString>` | varies |
| `Status` | `EArborCodexStatus` | `"Status"` |
| `LockedFields` | `TSet<FString>` | `"Lock"` |

### Adding a New Category

1. Create a `UDataAsset` subclass with all shared properties above
2. Register in `ArborCodexSearch.cpp` category map (`GetCategories()`)
3. Create a Slate widget (`SArborCodex<Name>Widget`)
4. Add the tab label + widget slot in `ArborGameCodexWidget.cpp`
5. MCP action routing is handled automatically by `codex.ts` via reflection

---

## Known Limitations

| Area | Limitation |
|------|-----------|
| **Material editor** | `get_material_expressions()` **hangs/freezes** — never call it. Use `query_material` / Material Instances instead. |
| **Material thumbnails** | Freshly-built materials render as the default lit-grey material until shaders finish compiling. Run `r.ShaderCompiler.AsyncCompiling 0` + `recompile_material(m)` before `render_thumbnail`. See "Material Graph & Function Editing > Gotchas". |
| **Foliage painting** | HISM fallback instances won't appear in Foliage editor paint tool but render correctly. |
| **NavMesh** | `add_navmesh_volume()` spawns the volume but rebuild may not trigger. Call `arbor.nav.build_paths()`. |
| **GLB/glTF orientation** | Meshy GLB uses Y-up; UE5 uses Z-up. Fix rotations after import. |
| **GLB pivot** | Meshy models have pivot at center. Fix: `arbor.mesh.fix_mesh_pivot(path, "bottom")`. |
| **Blueprint builder** | No function graphs or BP interfaces. |
| **Structure module** | Cube-based geometry only. No curved walls or multi-floor buildings. |

---

## Recipes

### Blockout an Arena
```python
import arbor.layout as layout
import arbor.nav as nav
import arbor.lighting as lighting

floor = layout.make_floor((0, 0, 0), 2000, 2000)
walls = [
    layout.make_wall((-1000, -1000), (1000, -1000), height=400),
    layout.make_wall((1000, -1000), (1000, 1000), height=400),
    layout.make_wall((1000, 1000), (-1000, 1000), height=400),
    layout.make_wall((-1000, 1000), (-1000, -1000), height=400),
]
nav.add_navmesh_volume((0, 0, 200), (1200, 1200, 400))
lighting.setup_outdoor_scene()
```

### AI Enemy Blueprint
```python
import arbor.blueprints as bp

# Character BP — mesh, anim, and the AI controller it should possess
bp.create_character_bp(
    "BP_Wolf",
    mesh_path="/Game/Assets/wolf_rigged",
    anim_bp_path="/Game/AI/ABP_Wolf",
    ai_controller_path="/Game/AI/BP_WolfAI",
    variables=[
        {"name": "MaxHealth", "type": "Float", "default": 100.0},
        {"name": "AttackDamage", "type": "Float", "default": 25.0},
    ]
)

# AI controller BP — parents to engine-stock AIController, optional perception.
# To auto-run a BehaviorTree on possess, wire ReceivePossess → UseBlackboard →
# RunBehaviorTree in the event graph via ue5_blueprint add_node/connect.
bp.create_ai_controller_bp(
    "BP_WolfAI",
    perception_config=[
        {"type": "AISense_Sight", "SightRadius": 3000, "LoseSightRadius": 3500}
    ]
)
```

### Run Automation Tests

UE's `Automation RunTests` console command serialises through one queue, gates on `FWaitForInteractiveFrameRate`, and only surfaces results via log lines. `arbor.automation` drives `FAutomationTestFramework` directly — no queue, no FPS gate, structured JSON results:

```python
import arbor.automation as automation

# Discover:
print(automation.list_tests("MyGame.Combat"))

# Run + inspect:
result = automation.run_tests("MyGame.Combat")
print(f"{result['summary']['passed']}/{result['summary']['total']} passed")
for t in result["tests"]:
    if not t["success"]:
        print(f"FAIL {t['path']}: {t['errors']}")

# Or fail loud:
automation.assert_run("MyGame.Combat")  # raises on any failure
```

**Filter semantics:** matches the engine's `Automation RunTests` — exact match OR starts-with-`filter.`. So `"MyGame.Combat"` runs every test in that namespace; `"MyGame.Combat.Conditions"` runs just the conditions tests.

**Each test runs on the editor's main thread synchronously.** Fine for unit/integration tests. PIE-heavy tests will appear to freeze the editor for the run duration — async variant is future work.

**Caveat:** new UFUNCTIONs require a full editor restart after first compile (Live Coding can't register new UFUNCTIONs). Same restriction as `arbor.tags` and `arbor.compile`.

### Trigger Live Coding from a Script

UE's stock Python can't tell when a Live Coding compile finishes — `LiveCoding.Compile` returns immediately and the engine fires `OnPatchComplete` only via C++ delegate. Use `arbor.compile`:

```python
import arbor.compile as compile

result = compile.compile_and_wait()  # blocks until done
if not result["success"]:
    raise RuntimeError(f"Compile failed: {result['message']}")

# Or async:
compile.start()
while compile.is_compiling():
    time.sleep(0.5)
print(compile.last_result())
```

`compile_and_wait` returns the actual result code (`Success`, `NoChanges`, `Failure`, etc.). The async path's `last_result` only knows "patch landed since I started observing" — not success/failure (UE's delegate doesn't carry it).

**Caveat:** new UFUNCTIONs require a full editor restart after first compile (Live Coding can't register new UFUNCTIONs). Same restriction as `arbor.tags`.

### Set Gameplay Tags on a Data Asset

UE5's stock Python can't construct an `FGameplayTag` from a string — `tag_name` is read-only and `RequestGameplayTag` isn't reflected. Use `arbor.tags`:

```python
import arbor.tags as tags

asset = unreal.EditorAssetLibrary.load_asset("/Game/Data/Quests/DA_QP_GymMain")

# Single-tag UPROPERTY:
tags.set_on_object(asset, "QuestRootTag", "Quest.Gym.Main")

# Tag inside an array element:
tags.set_on_object(asset, "Branches.0.BranchTag", "Quest.Gym.Branch.Default")

# Tag container (replaces existing contents):
tags.set_container_on_object(asset, "Branches.0.GrantsTagsOnComplete",
                              ["Quest.Gym.Main.Completed"])

unreal.EditorAssetLibrary.save_loaded_asset(asset)
```

Tags must be registered in `DefaultGameplayTags.ini` (or via a native module's `StartupModule`) before resolution succeeds. Discover what's available with `tags.list(prefix="Quest.Gym")`. `request(name)` raises `arbor.tags.TagNotRegisteredError` if missing — fix the registration, don't catch the error.

**Note:** New UFUNCTIONs in `UArborTagTools` require a full editor restart after the first compile (LiveCoding cannot register new UFUNCTIONs).

### Build a Cabin from Floor Plan
```python
cabin = structure.build_from_plan({
    "name": "Cabin", "wall_height": 300, "wall_thickness": 20,
    "rooms": [
        {"name": "MainRoom", "x": 0, "y": 0, "width": 600, "depth": 400, "has_floor": True},
        {"name": "Bedroom", "x": 600, "y": 0, "width": 400, "depth": 400, "has_floor": True},
    ],
    "doors": [{"between": ["MainRoom", "Bedroom"], "width": 120, "height": 220, "offset": 0.5}],
    "windows": [{"room": "MainRoom", "wall": "south", "width": 200, "height": 150, "sill_height": 100, "count": 2}],
    "roof": {"type": "gable", "overhang": 50, "pitch": 30},
})
```

Room positions (`x`, `y`) are 2D — Arbor handles Z. Doors go `"between"` two named rooms. Windows go on a named wall (`"north"`, `"south"`, `"east"`, `"west"`). Presets: `make_house(rooms=3)`, `make_castle(size="small")`, `make_tower(sides=8)`.

---

## Terrain, Layers & Water

```python
import arbor.terrain as terrain

# Rolling hills with auto-painted layers
landscape = terrain.create_rolling_hills(component_count=8, seed=42)

# Full pipeline: terrain + river + material
result = terrain.setup_terrain_with_river(river=True, river_width=500)
```

MCP: `ue5_terrain(action="create", preset="rolling_hills", river=true, auto_paint=true)`

**Layer painting:** `auto_paint_layers(landscape, rules=[...])` paints from heightmap (height + slope rules with noise). Default layers: Grass/Dirt/Rock.

**Water bodies:** `add_water_body_river(spline_points, width, snap_to_terrain=True)` and `add_water_body_lake(location, radius)`. Water Plugin's built-in brush handles terrain carving + grass suppression automatically when `affects_landscape=True` (default). Call `refresh_water_body()` after programmatic changes.

---

## PCG (Procedural Content Generation)

> The `ue5_pcg` MCP tool is **experimental** — enable via Project Settings → Plugins → Arbor → Experimental Features → PCG, then restart the bridge. The `arbor.pcg` Python module always works in `ue5_run_python` regardless of the flag.

Use PCG for GPU-instanced scattering of hundreds/thousands of meshes on landscapes.

```python
import arbor.pcg as pcg

# One-liner: create graph + find landscape + execute
pcg.scatter_on_landscape(mesh_paths=["/Game/Meshes/SM_Grass_01"], density=0.3)

# Presets: create_foliage_scatter, create_rock_scatter, create_debris_scatter
pcg.create_foliage_scatter(name="PCG_Grass",
    mesh_paths=["/Game/Meshes/SM_Grass_01"], density=0.5)
```

MCP: `ue5_pcg(action="create"|"query"|"execute")`. Granular editing: `add_node`, `remove_node`, `set_params`, `connect`, `disconnect`, `add_component`.

**Runtime discovery:** Use `ue5_pcg(action="list_node_types")` to discover all available PCG node types at runtime (including project/plugin classes). Use `ue5_pcg(action="get_node_params", class_name="PCGSurfaceSamplerSettings")` to get editable properties for a specific node type. Don't guess node types or params — discover them.

---

## Previewing Assets

Claude Code cannot display images. **NEVER** ask the user to pick textures/materials/meshes from text descriptions. Open them in UE5:

```python
import arbor.preview as preview
preview.preview_textures(["/Game/Textures/Var1", "/Game/Textures/Var2"])
preview.preview_material("/Game/Materials/M_Stone")  # on viewport sphere
preview.preview_mesh("/Game/Meshes/SM_Rock")
```

Then ask the user to look at UE5 and choose. Clean up: `preview.remove_preview_sphere()`.

---

## Material Graph & Function Editing

Granular material editing lives in `UArborMaterialGraphTools` (MCP `ue5_materials`, or `arbor.materials`). It edits a material's expression graph by stable sentinel IDs stored in each expression's `Desc` field (`__arbor_id:<id>`), surviving save/reload. `build_material(spec)` builds/updates a whole material idempotently in one `FMaterialUpdateContext`.

### Graph layout

`layout(path)` / `arbor.materials.layout_material(path)` auto-arranges a material's or material function's nodes into a readable left-to-right column layout (UE's built-in `LayoutMaterialExpressions` / `LayoutMaterialFunctionExpressions`; auto-detects the asset type, no editor window needed, no recompile - it only moves nodes). `build_material` / `build_material_function` run this automatically at the end so Arbor-built graphs open tidy instead of as a pile of overlapping nodes at the origin. On by default; it overrides any manual `x`/`y` in the spec, so pass `"auto_layout": false` to keep hand-placed positions.

### Material Functions (authoring)

`build_material_function(spec)` / `query_material_function(path)` author a `UMaterialFunction` with the same spec shape as `build_material`, with these differences:

- No `flags`/`shading_model`/`outputs` block. A function's inputs are `MaterialExpressionFunctionInput` nodes and its outputs are `MaterialExpressionFunctionOutput` nodes - both live in the graph.
- **A `FunctionOutput`'s input pin is unnamed** - wire into it with an empty `to_input` (`""`). Binary math nodes (Add/Subtract/Multiply/Divide/Distance/Max/Min/DotProduct) use `"A"`/`"B"`; single-input nodes (Floor/Abs/Saturate/OneMinus/Sine/Cosine/Frac/ComponentMask/Normalize/RgbToHsv/HsvToRgb) use `""`. As of the connection-resolver fix, a single-input target also accepts its C++ pin name (`"Input"`/`"VectorInput"`): `build_material`/`build_material_function` retry the lone pin automatically when a named `to_input` fails on a one-input node, so these no longer drop silently.
- `FunctionInput` props: `InputName` (FName), `InputType` (e.g. `FunctionInput_Vector2`/`_Vector3`/`_Scalar`), `SortPriority`. `FunctionOutput` props: `OutputName`, `SortPriority`.
- Returns `{inputs:[{name,type,sort}], outputs:[{name,sort}], expression_count, connection_count}`. Any wire that still can't be resolved (genuinely wrong pin on a multi-input node) is returned in an **`unresolved_connections`** array (`[{from,to,to_input}]`) instead of being dropped silently - check it rather than diffing `connection_count`.

To **use** a function in a material, add a `MaterialExpressionMaterialFunctionCall` with `properties.MaterialFunction = "/Game/.../MF_X"`; a post-property hook rebuilds its input/output pins so you can wire them by the function's `InputName`/`OutputName`. Compose - don't inline.

A procedural-primitives library (SDF shapes, gradients, posterize, remap, perlin/worley/FBM/value noise, UV tile/rotate/wave) lives under `/Game/Assets/Materials/Functions/Procedural/` and is cataloged (see below).

### Catalog `pattern` entries

The material catalog (`<project>/MaterialCatalog/`, browsed via **Tools > Arbor > Material Catalog**) now has two entry types: `reference_material` (a whole material, inline `spec`, in `source`) and `pattern` (a Material Function referenced by `mf_path`, with `inputs`/`outputs`). `search_catalog.py --type pattern|reference_material` filters; `extraction/extract_function.py` registers an MF as a pattern. Missing `type` is treated as `reference_material` (backward compatible).

### Gotchas (material editing)

- **A material rendering as the default lit-grey material = it failed to compile.** Do NOT guess why from screenshots. `query_material(path)` returns a `compile_errors` array (e.g. `"(Node ComponentMask) Missing ComponentMask input"`) - read it. Empty array = compiled clean. Recompile first if you just edited the graph. This is the fastest material-debugging tool by far.
  - Watch for **silently-incomplete connection lists**: `build_material`'s `connection_count` only counts the connections you *listed*, so a forgotten `uv -> node` edge still reports "wired". A `to_input` that can't be resolved is now surfaced in the result's `unresolved_connections` array (single-input pin-name mismatches auto-heal); if a node still says "Missing input", you forgot to list that edge.
- **Thumbnails of freshly-built materials render as the default lit-grey material** because shaders compile asynchronously. Before `render_thumbnail`, run `r.ShaderCompiler.AsyncCompiling 0` (console) then `MaterialEditingLibrary.recompile_material(m)` **in the same call**, then render. A still-lit-grey-but-shaded sphere = shaders not ready, NOT a bad material.
- **`ComponentMask` channels are authoritative in the spec.** UE's ComponentMask CDO defaults to R+G; the build path now clears all four channels whenever the spec names any of `R`/`G`/`B`/`A`, so `{"R":true}` gives R-only as written. Always pass the channels you want true (unlisted = false). A mask that secretly keeps an extra channel widens its output (float1 -> float2) and silently breaks downstream math with no compile error.
- **Glow/bloom needs HDR emissive.** A plain colored emissive reads flat/washed; multiply emissive by ~8-30 so it blooms. This is what makes neon/corruption/effects "glow".
- **Decals (UE 5.7):** `DecalBlendMode` is deprecated ("No longer used") - decals are governed by the material's `BlendMode` + which outputs are connected. Set the domain via `m.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)` (the domain IS settable from Python; the old blend-mode enum is not). Emissive decals (BlendMode Translucent + Emissive/Opacity outputs + HDR emissive) read on any surface; project with a `DecalActor` rotated `pitch=-90` (X points down) and tune `decal_size` (X=depth, Y/Z=footprint).
- **Dynamic material instance ordering:** call `set_decal_material(m)` / `set_material(0, m)` **before** `create_dynamic_material_instance()`, or the MID wraps the *default* material and your parameter sets silently do nothing.
- **Engine `MaterialExpressionNoise` Position input is the first (unnamed) pin** - wire with `to_input=""`, not `"Position"`, and feed a Vector3 (append a 0 onto a Vector2 UV; a Vector2 into the Vector3 input fails coercion and the connection is silently dropped).
- **Time-driven animation** (panner flow, sine pulse/flicker via `MaterialExpressionTime`+`Sine`) does not show in a static thumbnail/screenshot - it only animates in PIE/standalone. Keep a brightness floor so stills stay lit.

---

## Blueprint Editing

All BP operations are now available via MCP `ue5_blueprint(action=...)`. For `ue5_run_python`, use `arbor.blueprints`.

### Node Types (for add_node)

| Type | Key fields | Pins |
|------|------------|------|
| `Event` | `event` (e.g. `"BeginPlay"`, `"Tick"`) | Out: `Then` + event params |
| `ComponentEvent` | `component`, `event` (e.g. `"OnComponentBeginOverlap"`) | Out: `Then` + delegate params |
| `CallFunction` | `function`, `target` (opt), `owner_class` (opt), `defaults` (opt) | In: `Execute` / Out: `Then`, `ReturnValue` |
| `VariableGet` | `variable` | Out: value pin (named after variable) |
| `VariableSet` | `variable`, `defaults` (opt) | In: `Execute` / Out: `Then` |
| `Branch` | — | In: `execute`, `Condition` / Out: `then`, `else` |
| `CastTo` | `class` | In: `execute`, `Object` / Out: `then`, `CastFailed`, `As<Class>` |
| `Timeline` | `timeline` (name) | In: `Play`, `PlayFromStart`, `Stop`, `Reverse` / Out: `Update`, `Finished`, track pins |
| `FormatText` | `format` (e.g. `"{cur}/{req}"`) | Out: `Result` (text) + one arg pin per `{placeholder}` |
| `CreateWidget` | `defaults.Class` (widget class path) | In: `execute`, `Class`, `OwningPlayer` / Out: `then`, `ReturnValue` (+ ExposeOnSpawn pins) |
| *(any UK2Node class)* | `properties` (opt), `defaults` (opt) | *(resolved at runtime)* |

The `UK2Node_` prefix is optional when specifying node classes - the builder resolves K2 nodes in any module (BlueprintGraph, UMGEditor, ...) via reflection. `FormatText` generates an argument pin for each `{name}` in its format string; `CreateWidget` surfaces the widget's ExposeOnSpawn pins once `defaults.Class` is set.

**Runtime discovery:** Use `ue5_blueprint(action="list_node_types", filter="Sequence")` to discover available K2Node types. Use `ue5_blueprint(action="list_functions", class_name="KismetMathLibrary")` to list callable functions on a class. Use `ue5_blueprint(action="list_component_types", filter="Mesh")` to discover component types. Don't guess node/function/component names — discover them.

### Critical Pin Name Gotchas

- Branch: outputs are `"then"` and `"else"`, NOT `"True"`/`"False"`
- Branch: input exec is `"execute"`, condition is `"Condition"`
- Always use `ue5_blueprint(action="get_node_pins")` or `query_blueprint` to verify pin names if unsure

### Function Resolution

For cross-class calls, use `owner_class` (e.g. `"BlackboardComponent"`, `"AIController"`). Use `ue5_blueprint(action="list_functions", class_name="...")` to discover available functions on any class.

### Function Name Gotchas

| What you want | Correct name | Wrong name |
|---|---|---|
| Get blackboard | `GetBlackboard` | `GetBlackboardComponent` |
| Clear BB key | `ClearValue` | `ClearBlackboardValue` |
| Set BB object | `SetValueAsObject` | `SetBlackboardValueAsObject` |

### By-ref Pins

By-ref FName/FString/FText pins (e.g. `KeyName` on `SetValueAsObject`) are handled automatically — the builder creates `MakeLiteralName`/`MakeLiteralString` helper nodes and wires them in. Just set the default value normally.

### AIControllerClass on Character BPs

You MUST set `AIControllerClass` on the CDO manually after building:
```python
char_bp = unreal.EditorAssetLibrary.load_asset("/Game/AI/BP_MyCharacter")
aic_bp = unreal.EditorAssetLibrary.load_asset("/Game/AI/BP_MyAIController")
cdo = unreal.get_default_object(char_bp.generated_class())
cdo.set_editor_property("ai_controller_class", aic_bp.generated_class())
char_bp.modify()
unreal.EditorAssetLibrary.save_loaded_asset(char_bp)
bp.compile_blueprint("/Game/AI/BP_MyCharacter")  # REQUIRED
```

### Component Editing

MCP: `ue5_blueprint(action="add_component"|"remove_component"|"set_component_property"|"set_component_transform")`.

`set_component_transform` takes `location={x,y,z}`, `rotation={pitch,yaw,roll}`, `scale={x,y,z}` — any subset. Translates to `RelativeLocation`/`RelativeRotation`/`RelativeScale3D` internally.

**Variable types:** `Float`, `Int`, `Bool`, `String`, `Name`, `Vector`, `Rotator`, `Object`.

### Builders Update in Place

All Arbor builders (BuildBT, BuildBP, BuildEQS) update existing assets. Never delete and recreate — references are preserved.

---

## BehaviorTree Editing

MCP: `ue5_ai(action="create_bt"|"query_bt"|"add_bt_node"|"remove_bt_node"|"set_bt_params"|"layout_bt")`.

Supported composites: `Selector`, `Sequence`, `SimpleParallel`.

**Runtime discovery:** Use `ue5_ai(action="list_bt_types", bt_category="task")` to discover available BT node classes. Use `ue5_ai(action="get_class_params", class_name="BTTask_MoveTo")` to get editable properties and their types/defaults. Also: `list_eqs_generators`, `list_eqs_tests` for EQS type discovery. Don't guess class names or params — discover them.

### Tree Paths

BT nodes use dot-delimited child indices as identifiers: `""` (root), `"0"` (first child), `"1.2"` (third child of second), `"0:decorator:0"` (first decorator), `":service:0"` (root service).

### Node Spec (add_bt_node)

| Field | Values |
|-------|--------|
| `type` | `Selector`, `Sequence`, `SimpleParallel`, `Task` |
| `class` | e.g. `BTTask_Wait`, `BTDecorator_Cooldown` |
| `role` | `child` (default), `decorator`, `service` |
| `params` | Dict of property → value (via reflection) |

---

## EQS Editing

MCP: `ue5_ai(action="create_eqs"|"query_eqs"|"add_eqs_generator"|"remove_eqs_generator"|...)`.

Wire EQS into BT using `BTTask_RunEQSQuery` or `BTService_RunEQS`. Use `ue5_ai(action="list_eqs_generators")` and `ue5_ai(action="list_eqs_tests")` to discover available generator/test types and `get_class_params` for their properties.

---

## AnimGraph Editing

MCP: `ue5_blueprint(action="setup_anim"|"query_anim")`.

Creates BlendSpacePlayer → OutputPose graph with variable binding(s). ABP must exist first (create with `create_character` or `build_bp` with `parent_class: "AnimInstance"`).

**Multi-axis BlendSpaces:** For 2D BlendSpaces (e.g. X=Direction, Y=Speed), use `variable_bindings` array instead of single `variable_name`/`variable_axis`:
```
setup_anim(asset_path, blendspace_path, variable_bindings=[
  {variable_name: "Direction", variable_axis: "X"},
  {variable_name: "Speed", variable_axis: "Y"}
])
```

**Pin discovery:** `query_anim` returns `blendspace_axes` on BlendSpacePlayer nodes with axis display names, min/max ranges — use this to determine which variable to wire to which axis.

**Granular editing works on AnimGraph nodes too.** The `connect`, `disconnect`, `set_pin`, and `remove_node` actions search all graphs (Event Graph + AnimGraph) by GUID. Use `query_anim` to get node GUIDs, then use the standard actions to edit them.

---

## Widget Editing (UMG)

> Experimental: enable Project Settings -> Plugins -> Arbor -> Experimental Features -> "Widget (UMG)", then restart the bridge. New UFUNCTIONs (`UWidgetBlueprintBuilder`, `UWidgetAnimationBuilder`) require a full editor restart after first compile (Live Coding cannot register new UFUNCTIONs).

MCP: `ue5_widget(action="create"|"query"|"add_widget"|"remove_widget"|"set_widget_property"|"set_root"|"list_widget_types"|"compile")`.

Authoring goes through C++ builders (UMG/UMGEditor APIs), never raw `unreal` `set_editor_property` on the WidgetTree/CDO (the hard rule applies here too). Builders update in place and never delete-recreate, so references survive.

**Widget tree**: lives on `UWidgetBlueprint::WidgetTree`. `create` builds a WidgetBlueprint subclass of any `UUserWidget`/`UCommonActivatableWidget` (resolve `parent_class` by short name or `/Script/...` path) and can build the whole tree in one call via a `tree` array. Each widget spec: `{name, type, parent?, root?, index?, is_variable?, properties?, slot_properties?}`. List parents before children. `index` (optional) sets the child's slot order within its parent panel (appended then shifted) - use it to control overlay/box z-ordering. Discover types with `list_widget_types` - never guess class names.

**BindWidget is the whole point**: for a C++ `UPROPERTY(meta=(BindWidget)) UTextBlock* Foo;` to bind, the UMG widget must be named **exactly** `Foo` and be a variable (default `is_variable: true`). `query` reports the parent class's `BindWidget`/`BindWidgetOptional` properties (name + type + optional) so you know which names to use; mismatches make `compile` throw "A required widget binding is missing" (surfaced verbatim).

**Brush images / textures**: `set_widget_property` accepts a brush spec, e.g. `{"Brush":{"image":"/Game/UI/T_Panel","draw_as":"Box","image_size":{"x":256,"y":64},"tint":{"r":1,"g":1,"b":1,"a":1},"margin":{"left":12,"top":12,"right":12,"bottom":12}}}`. Use `draw_as:"Box"` (9-slice) for panel backgrounds, `"Image"` for icons. Slot fields go under `{"__slot":{...}}` or `slot_properties` on add_widget.

**Event graph is reused, not duplicated**: a WidgetBlueprint IS-A UBlueprint, so wire `ShowTracker -> PlayAnimation`, `UpdateObjective -> SetText/SetPercent`, etc. via the existing `ue5_blueprint` `add_node`/`connect`/`compile` pointed at the widget asset path. The animation variable name (see below) is the `VariableGet` to feed `PlayAnimation`'s `InAnimation` pin.

### Widget Animations

MCP: `ue5_widget_animation(action="add_preset"|"query"|"remove"|"add_track")`.

Preset-driven, so you get good motion without hand-keying. One animation `name` holds one or more tracks (so `Intro` = fade_in + slide_in plays together). After authoring, the blueprint recompiles and the animation surfaces as a `UWidgetAnimation*` variable you reference from the event graph.

`add_preset` spec: `{ name, tracks: [ { preset, target, duration?, direction?, distance?, overshoot? } ] }`.

| preset | effect | key params (defaults) |
|--------|--------|-----------------------|
| `fade_in` / `fade_out` | RenderOpacity 0->1 / 1->0 | duration (0.35 / 0.25) |
| `slide_in` / `slide_out` | RenderTransform translation | direction (left/right/top/bottom), distance (120), duration (0.35) |
| `pop` / `scale_in` | RenderTransform scale 0->1 | overshoot (1.1 for pop), duration (0.3). Pivot defaults to center. |
| `pulse` | scale 1->peak->1 | overshoot (1.08), duration (0.5). Loop via PlayAnimation NumLoopsToPlay. |
| `strikeoff` | left-to-right wipe over a text widget | duration (0.4). Auto-creates a `<target>_Strike` Image overlay (best-effort; Overlay/Canvas parent recommended). |

Low-level hatch `add_track`: `{ animation, target, track_type:"float"|"transform2d", property?, duration?, channels:[{component, keys:[{t,v}]}] }`. transform2d components: `TranslationX|TranslationY|ScaleX|ScaleY|Rotation`. Use when a preset is not enough.

## AI Texture Generation

**`get_material_expressions()` hangs** — never call it. Use Material Instances:

```python
import arbor.materials as mat

base_path = mat.ensure_pbr_base_material()  # → "/Game/Materials/M_PBR_Parameterized"
mat.create_material_instance(base_path, "MI_StoneWall", params={
    "Albedo": "/Game/Textures/T_StoneWall_Albedo",
    "Normal": "/Game/Textures/T_StoneWall_Normal",
    "Tiling": 4.0,
})
```

Parameters on `M_PBR_Parameterized`: `Albedo`, `Normal`, `Roughness`, `Metallic`, `AO` (Texture2D), `Tiling` (Scalar).

### MCP Texture Workflow

**IMPORTANT:** When generating images, always set `output_dir` to `<ProjectDir>/Art/ConceptArt/` (concept art) or `<ProjectDir>/Art/Textures/` (textures). Never save generated images to the project root.

1. Generate images via `replicate_texture_with_pbr`, `scenario_generate_texture`, or `fal_generate_image`
2. Show review: `arbor.textures.show_texture_review(images_json)`
3. Poll result: `arbor.textures.get_texture_review_result()`
4. Import: `arbor.textures.import_and_create_material_instance(paths, name, tiling=4.0)`

---

## Playtest

MCP: `ue5_playtest(action="start_pie"|"stop_pie"|"is_running"|"player_info"|"teleport"|...)`.

- Always call `is_running` after `start_pie` to confirm player pawn is ready
- `walk_path` and `run_playtest` are **async** — return immediately, write results via tick callbacks
- `move_to` and `navigate_path` use WASD input (goes through CharacterMovementComponent)
- `stop_pie()` cancels active walks/playtests

---

## Dependencies

- **Python Editor Script Plugin** (`PythonScriptPlugin`)
- **Editor Scripting Utilities** (`EditorScriptingUtilities`)
- **AI Module** (`AIModule`)
- **Material Editor** (`MaterialEditor`)
- **ImageWrapper** (`ImageWrapper`)
- **RenderCore** (`RenderCore`)
