/**
 * Reusable validator primitives. Each returns a RequirementResult with both a
 * boolean pass and the raw observed value, so the report can show what was
 * actually found when a requirement fails.
 *
 * Validators only call read-only MCP actions + Python introspection — they
 * never mutate the level. If a validator needs to look at an asset on disk
 * (screenshots), it does so without touching the editor.
 */

import { promises as fs } from "node:fs";
import { dirname, join } from "node:path";
import { actorsTool } from "../../../src/registry/actors.js";
import { aiTool } from "../../../src/registry/ai.js";
import { blueprintTool } from "../../../src/registry/blueprint.js";
import { runPython } from "../../../src/tools/core/run-python.js";
import type { RequirementResult } from "./requirement.js";

interface SceneActor {
  name: string;
  label: string;
  class: string;
  location?: { x: number; y: number; z: number };
}

interface SceneInfo {
  actors?: SceneActor[];
}

async function sceneActors(prefix?: string, klass?: string): Promise<SceneActor[]> {
  const result = (await actorsTool.actions.scene_info({
    filter_prefix: prefix,
    filter_class: klass,
  })) as SceneInfo;
  return result.actors ?? [];
}

// ─── Actor / scene validators ──────────────────────────────────────────

export async function actorWithLabelExists(
  labelStartsWith: string
): Promise<RequirementResult> {
  const actors = await sceneActors(labelStartsWith);
  const matches = actors.filter((a) =>
    (a.label || a.name).startsWith(labelStartsWith)
  );
  return {
    passed: matches.length > 0,
    detail: `expected ≥1 actor with label starting "${labelStartsWith}", found ${matches.length}`,
    observed: matches.map((a) => ({
      label: a.label || a.name,
      class: a.class,
    })),
  };
}

export async function countActorsWithLabel(
  labelStartsWith: string,
  expectedAtLeast: number
): Promise<RequirementResult> {
  const actors = await sceneActors(labelStartsWith);
  const matches = actors.filter((a) =>
    (a.label || a.name).startsWith(labelStartsWith)
  );
  return {
    passed: matches.length >= expectedAtLeast,
    detail: `expected ≥${expectedAtLeast} actors with label starting "${labelStartsWith}", found ${matches.length}`,
    observed: matches.map((a) => a.label || a.name),
  };
}

export async function actorOfClassExists(
  className: string,
  prefix?: string
): Promise<RequirementResult> {
  const actors = await sceneActors(prefix, className);
  return {
    passed: actors.length > 0,
    detail: `expected ≥1 actor of class "${className}"${prefix ? ` with prefix "${prefix}"` : ""}, found ${actors.length}`,
    observed: actors.map((a) => ({ label: a.label || a.name, class: a.class })),
  };
}

/**
 * Outdoor lighting check — UE adds a Directional Light + sky atmosphere/skylight
 * when `arbor.lighting.setup_outdoor_scene()` runs. We only require that ≥1
 * directional light is present (the sky setup varies with engine version).
 */
export async function outdoorLightingPresent(): Promise<RequirementResult> {
  const directional = await sceneActors(undefined, "DirectionalLight");
  const skyAtmo = await sceneActors(undefined, "SkyAtmosphere");
  const skyLight = await sceneActors(undefined, "SkyLight");
  const passed =
    directional.length > 0 && (skyAtmo.length > 0 || skyLight.length > 0);
  return {
    passed,
    detail: `expected DirectionalLight + (SkyAtmosphere or SkyLight); got dir=${directional.length} skyAtmo=${skyAtmo.length} skyLight=${skyLight.length}`,
    observed: { directional: directional.length, skyAtmo: skyAtmo.length, skyLight: skyLight.length },
  };
}

// ─── Material distinctness ─────────────────────────────────────────────

/**
 * Compare the assigned override material on a wall actor to the floor's. If
 * either has no override (engine default), or both share the same material
 * path, fail.
 */
export async function wallAndFloorMaterialsDiffer(
  floorLabelPrefix: string,
  wallLabelPrefix: string
): Promise<RequirementResult> {
  const data = await pyJson(`
import unreal

def primary_material_path(actor):
    """Return the path of the first material on the first SMC, override or asset."""
    smc = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not smc:
        return None
    # Override first
    overrides = smc.get_editor_property("override_materials") or []
    for m in overrides:
        if m:
            return m.get_path_name()
    # Fall back to material set on the mesh asset
    mesh = smc.get_editor_property("static_mesh")
    if mesh:
        mats = mesh.get_editor_property("static_materials") or []
        for entry in mats:
            mi = entry.material_interface if hasattr(entry, "material_interface") else None
            if mi:
                return mi.get_path_name()
    return None

ess = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ess.get_editor_world() if ess else None
all_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if world else []

floor_mat = None
wall_mats = []
for a in all_actors:
    label = a.get_actor_label()
    if label.startswith(${JSON.stringify(floorLabelPrefix)}):
        floor_mat = primary_material_path(a)
    elif label.startswith(${JSON.stringify(wallLabelPrefix)}):
        m = primary_material_path(a)
        if m:
            wall_mats.append(m)

distinct = floor_mat is not None and any(m != floor_mat for m in wall_mats)
_write_result({
    "floor_material": floor_mat,
    "wall_materials": wall_mats,
    "distinct": distinct,
})
`);
  return {
    passed: data.distinct === true,
    detail: data.distinct
      ? `wall material differs from floor material`
      : `walls and floor share the same material (or one has no material)`,
    observed: data,
  };
}

// ─── Asset / Blueprint validators ──────────────────────────────────────

export async function blueprintAtPath(
  assetPath: string
): Promise<RequirementResult> {
  try {
    const result = (await blueprintTool.actions.query({
      asset_path: assetPath,
    })) as { success?: boolean; asset_path?: string; error?: string };
    const passed = result.success !== false && !result.error;
    return {
      passed,
      detail: passed
        ? `Blueprint found at ${assetPath}`
        : `Blueprint not found at ${assetPath}: ${result.error ?? "unknown error"}`,
      observed: result,
    };
  } catch (e) {
    return {
      passed: false,
      detail: `query() threw for ${assetPath}: ${(e as Error).message}`,
      observed: { error: (e as Error).message },
    };
  }
}

/**
 * The character BP CDO should have an AIControllerClass set, which transitively
 * carries the BehaviorTree (set on the AIController BP CDO). We accept either
 * an AIController-only chain or a directly-set DefaultBehaviorTree.
 */
export async function characterBpHasAiControllerWired(
  charPath: string
): Promise<RequirementResult> {
  const data = await pyJson(`
import unreal

bp = unreal.EditorAssetLibrary.load_asset(${JSON.stringify(charPath)})
if not bp or not bp.generated_class():
    _write_result({"loaded": False})
else:
    cdo = unreal.get_default_object(bp.generated_class())
    aic = cdo.get_editor_property("ai_controller_class")
    aic_path = aic.get_path_name() if aic else None
    bt_path = None
    if aic:
        try:
            aic_cdo = unreal.get_default_object(aic)
            bt = aic_cdo.get_editor_property("default_behavior_tree")
            bt_path = bt.get_path_name() if bt else None
        except Exception:
            pass
    _write_result({
        "loaded": True,
        "ai_controller_class": aic_path,
        "default_behavior_tree": bt_path,
    })
`);
  const passed = data.loaded === true && !!data.ai_controller_class;
  return {
    passed,
    detail: passed
      ? `Character BP has AIControllerClass=${data.ai_controller_class}, BT=${data.default_behavior_tree ?? "(none on AIC)"}`
      : `Character BP missing AIControllerClass`,
    observed: data,
  };
}

/** Walk a behavior tree's structure (via aiTool.query_bt) looking for a node class match. */
export async function behaviorTreeContainsNodeClass(
  btPath: string,
  nodeClassSubstring: string
): Promise<RequirementResult> {
  try {
    const result = (await aiTool.actions.query_bt({
      asset_path: btPath,
    })) as Record<string, unknown>;
    const json = JSON.stringify(result);
    const passed = json.includes(nodeClassSubstring);
    return {
      passed,
      detail: passed
        ? `BT at ${btPath} contains node matching "${nodeClassSubstring}"`
        : `BT at ${btPath} has no node matching "${nodeClassSubstring}"`,
      observed: result,
    };
  } catch (e) {
    return {
      passed: false,
      detail: `query_bt() threw for ${btPath}: ${(e as Error).message}`,
      observed: { error: (e as Error).message },
    };
  }
}

/**
 * Resolve the BehaviorTree associated with a Character BP. Tries three paths
 * in order, since Arbor's canonical "wire BT on AIController" pattern uses
 * an event-graph `RunBehaviorTree` call rather than a CDO property
 * (stock `AAIController` doesn't expose `default_behavior_tree`):
 *   1. `default_behavior_tree` on the AIController CDO (custom subclasses).
 *   2. Walk the AIController BP's event graphs for a `RunBehaviorTree` call
 *      and read the `BTAsset` pin literal.
 *   3. If `fallbackSearchRoot` is given, scan that folder for BehaviorTree
 *      assets; if exactly one exists, take it.
 *
 * Returns the BT asset path or null if no path resolves.
 */
export async function behaviorTreePathFromCharacter(
  charPath: string,
  fallbackSearchRoot?: string
): Promise<string | null> {
  const data = await pyJson(`
import unreal

CHAR_PATH = ${JSON.stringify(charPath)}
FALLBACK_ROOT = ${JSON.stringify(fallbackSearchRoot ?? "")}

bt_path = None
resolved_via = None
debug = {"checked_aic_path": None, "ubergraph_count": 0, "callfunc_count": 0,
         "found_run_bt_call": False, "fallback_candidates": []}

bp = unreal.EditorAssetLibrary.load_asset(CHAR_PATH)
if bp and bp.generated_class():
    cdo = unreal.get_default_object(bp.generated_class())
    aic = cdo.get_editor_property("ai_controller_class")
    if aic:
        debug["checked_aic_path"] = aic.get_path_name()
        # 1. CDO property (only present on custom AIController subclasses)
        try:
            aic_cdo = unreal.get_default_object(aic)
            bt = aic_cdo.get_editor_property("default_behavior_tree")
            if bt:
                bt_path = bt.get_path_name()
                resolved_via = "cdo_default_behavior_tree"
        except Exception:
            pass

        # 2. AIController BP event-graph: K2Node_CallFunction "RunBehaviorTree"
        if not bt_path:
            # generated_class has a "_C" suffix — strip to load the BP asset
            aic_class_path = aic.get_path_name()
            if aic_class_path.endswith("_C"):
                aic_bp_path = aic_class_path[:-2]
            else:
                aic_bp_path = aic_class_path
            aic_bp = unreal.EditorAssetLibrary.load_asset(aic_bp_path)
            if aic_bp:
                try:
                    pages = aic_bp.get_editor_property("ubergraph_pages") or []
                    debug["ubergraph_count"] = len(pages)
                    for graph in pages:
                        nodes = graph.get_editor_property("nodes") or []
                        for node in nodes:
                            if node.get_class().get_name() != "K2Node_CallFunction":
                                continue
                            debug["callfunc_count"] += 1
                            ref = node.get_editor_property("function_reference")
                            mname = str(ref.get_member_name())
                            if mname != "RunBehaviorTree":
                                continue
                            debug["found_run_bt_call"] = True
                            for pin in (node.get_editor_property("pins") or []):
                                if str(pin.get_editor_property("pin_name")) != "BTAsset":
                                    continue
                                # Object literals are stored on default_object;
                                # if the pin is wired, follow the link to a literal
                                d = pin.get_editor_property("default_object")
                                if d:
                                    bt_path = d.get_path_name()
                                    resolved_via = "event_graph_default_object"
                                    break
                                links = pin.get_editor_property("linked_to") or []
                                for linked in links:
                                    owner = linked.get_editor_property("owning_node")
                                    if owner and owner.get_class().get_name() in (
                                        "K2Node_Literal", "K2Node_Self"
                                    ):
                                        try:
                                            lit_obj = owner.get_editor_property("object_ref")
                                            if lit_obj:
                                                bt_path = lit_obj.get_path_name()
                                                resolved_via = "event_graph_literal_node"
                                                break
                                        except Exception:
                                            pass
                            if bt_path:
                                break
                        if bt_path:
                            break
                except Exception as e:
                    debug["walker_error"] = str(e)

# 3. Folder scan fallback — only applied when caller passed a root
if not bt_path and FALLBACK_ROOT:
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = ar.get_assets_by_path(FALLBACK_ROOT, recursive=True)
        for a in assets:
            cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
            if cls == "BehaviorTree":
                debug["fallback_candidates"].append(str(a.package_name))
        if len(debug["fallback_candidates"]) == 1:
            bt_path = debug["fallback_candidates"][0]
            resolved_via = "fallback_single_bt_in_folder"
    except Exception as e:
        debug["fallback_error"] = str(e)

_write_result({"bt_path": bt_path, "resolved_via": resolved_via, "debug": debug})
`);
  return (data.bt_path as string | null) ?? null;
}

// ─── Screenshot validation ─────────────────────────────────────────────

/**
 * Find the most recently modified PNG/JPG inside the project's Saved/Screenshots
 * tree that's newer than `sinceMs`. Walks the tree shallowly (UE puts shots in
 * Saved/Screenshots/<Platform>/).
 */
export async function findLatestScreenshotSince(
  sinceMs: number
): Promise<string | null> {
  const data = await pyJson(`
import unreal
import os

# Search both the engine default (Saved/Screenshots/) and arbor.capture's
# subdir (Saved/Arbor/Screenshots/) — different tools write to different roots.
saved = unreal.Paths.project_saved_dir()
roots = [
    os.path.normpath(os.path.join(saved, "Screenshots")),
    os.path.normpath(os.path.join(saved, "Arbor", "Screenshots")),
]
latest = None
latest_mtime = 0
for root_dir in roots:
    if not os.path.isdir(root_dir):
        continue
    for root, _dirs, files in os.walk(root_dir):
        for f in files:
            if not (f.lower().endswith(".png") or f.lower().endswith(".jpg") or f.lower().endswith(".jpeg")):
                continue
            full = os.path.join(root, f)
            try:
                mt = os.path.getmtime(full)
            except OSError:
                continue
            if mt > latest_mtime and mt * 1000 >= ${sinceMs - 2000}:
                latest_mtime = mt
                latest = full

_write_result({"path": latest, "mtime_ms": int(latest_mtime * 1000)})
`);
  return (data.path as string | null) ?? null;
}

/**
 * Return *all* PNG/JPG files in the project's Saved/Screenshots tree that are
 * newer than `sinceMs`. Used by the test to collect every screenshot Claude
 * took during the run for the HTML report.
 */
export async function findAllScreenshotsSince(
  sinceMs: number
): Promise<string[]> {
  const data = await pyJson(`
import unreal
import os

saved = unreal.Paths.project_saved_dir()
roots = [
    os.path.normpath(os.path.join(saved, "Screenshots")),
    os.path.normpath(os.path.join(saved, "Arbor", "Screenshots")),
]
matches = []
for root_dir in roots:
    if not os.path.isdir(root_dir):
        continue
    for root, _dirs, files in os.walk(root_dir):
        for f in files:
            if not (f.lower().endswith(".png") or f.lower().endswith(".jpg") or f.lower().endswith(".jpeg")):
                continue
            full = os.path.join(root, f)
            try:
                mt = os.path.getmtime(full)
            except OSError:
                continue
            if mt * 1000 >= ${sinceMs - 2000}:
                matches.append({"path": full, "mtime_ms": int(mt * 1000)})

matches.sort(key=lambda m: m["mtime_ms"])
_write_result({"shots": matches})
`);
  const shots = (data.shots as Array<{ path: string }>) ?? [];
  return shots.map((s) => s.path);
}

/**
 * Validate that a screenshot exists on disk and has reasonable mean pixel
 * brightness (i.e. it's not pitch black, which is what we get if the orbit
 * camera ended up inside a wall or PIE failed). No image-decoding deps —
 * we sample the file's raw bytes instead, which is a coarse but adequate
 * "did the editor render anything" signal for PNG/JPG.
 */
export async function screenshotIsNotBlank(
  path: string | null
): Promise<RequirementResult> {
  if (!path) {
    return { passed: false, detail: "no screenshot path provided", observed: null };
  }
  try {
    const stat = await fs.stat(path);
    if (stat.size < 1024) {
      return {
        passed: false,
        detail: `screenshot file is suspiciously small (${stat.size} bytes)`,
        observed: { path, size: stat.size },
      };
    }
    // Sample raw bytes — files of pure black render to large runs of zero/0xFF
    // padded chunks once compressed, but real renders have wide byte variance.
    // Read up to 256 KB and compute byte-value variance as a cheap proxy.
    const buf = await fs.readFile(path);
    const sample = buf.subarray(0, Math.min(buf.length, 262144));
    let sum = 0;
    let sumSq = 0;
    for (let i = 0; i < sample.length; i++) {
      sum += sample[i]!;
      sumSq += sample[i]! * sample[i]!;
    }
    const mean = sum / sample.length;
    const variance = sumSq / sample.length - mean * mean;
    // Empirically, a black PNG has variance < 200; a real render has > 2000.
    const passed = variance > 1000;
    return {
      passed,
      detail: passed
        ? `screenshot at ${path} (${stat.size} bytes, byte-variance ${variance.toFixed(0)})`
        : `screenshot looks blank — byte-variance ${variance.toFixed(0)} below threshold`,
      observed: { path, size: stat.size, byte_variance: Math.round(variance) },
    };
  } catch (e) {
    return {
      passed: false,
      detail: `screenshot path unreadable: ${(e as Error).message}`,
      observed: { path, error: (e as Error).message },
    };
  }
}

/** Resolve an asset path on disk (under <ProjectContent>/) to confirm the .uasset exists. */
export async function uassetExists(
  assetPath: string
): Promise<RequirementResult> {
  const data = await pyJson(`
import unreal
import os
content_dir = unreal.Paths.project_content_dir()
rel = ${JSON.stringify(assetPath)}.replace("/Game/", "").split(".")[0]
disk = os.path.normpath(os.path.join(content_dir, rel + ".uasset"))
_write_result({"disk": disk, "exists": os.path.isfile(disk)})
`);
  return {
    passed: data.exists === true,
    detail: data.exists
      ? `.uasset on disk: ${data.disk}`
      : `.uasset missing on disk: ${data.disk}`,
    observed: data,
  };
}

// ─── Internal: pyQuery wrapper ─────────────────────────────────────────

async function pyJson(code: string): Promise<Record<string, unknown>> {
  const result = await runPython({ code });
  if (!result.success) {
    throw new Error(
      `Python failed: ${result.error}\n${result.traceback ?? ""}`
    );
  }
  return (result.result ?? {}) as Record<string, unknown>;
}
