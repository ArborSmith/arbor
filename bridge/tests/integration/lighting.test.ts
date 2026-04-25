import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { lightingTool } from "../../src/registry/lighting.js";
import { runPython } from "../../src/tools/core/run-python.js";

const editorRunning = await isConnected();

/**
 * Delete all actors spawned by the lighting tests.
 *
 * The C++ setup_outdoor/setup_indoor call RemoveExistingByClass which destroys
 * ALL actors of a given class (including original template ones). When setup_indoor
 * runs twice, the RectLight (IndoorCeilingLight) duplicates because RemoveExistingByClass
 * doesn't cover RectLight. So we clean up by class to catch all duplicates.
 */
async function cleanupLightingActors(): Promise<void> {
  try {
    await runPython({
      code: `
import unreal

classes_to_clean = [
    "DirectionalLight",
    "SkyLight",
    "RectLight",
    "PostProcessVolume",
]
# Dynamic classes that may not exist in all projects
optional_classes = [
    "/Script/Engine.SkyAtmosphere",
    "/Script/Engine.ExponentialHeightFog",
    "/Script/Engine.VolumetricCloud",
]

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
deleted = []

for cls_name in classes_to_clean:
    cls = getattr(unreal, cls_name, None)
    if cls:
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
            name = actor.get_actor_label()
            actor.destroy_actor()
            deleted.append(name)

for cls_path in optional_classes:
    cls = unreal.find_object(None, cls_path)
    if cls:
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
            name = actor.get_actor_label()
            actor.destroy_actor()
            deleted.append(name)

# Also clean up the SM_SkySphere static mesh actor that setup_outdoor might leave
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
    label = actor.get_actor_label()
    if label == "SM_SkySphere":
        actor.destroy_actor()
        deleted.append(label)

_write_result({"success": True, "deleted": deleted})
`,
    });
  } catch {
    /* best-effort */
  }
}

describe.runIf(editorRunning)("Lighting Operations", () => {
  afterAll(async () => {
    await cleanupLightingActors();
  });

  // ── Outdoor ───────────────────────────────────────────────────────

  describe("Outdoor", () => {
    it("sets up outdoor lighting with defaults", async () => {
      const result = (await lightingTool.actions.setup_outdoor(
        {}
      )) as Record<string, unknown>;
      expect(result).toBeDefined();
    });

    it("sets up outdoor lighting with custom time and clouds", async () => {
      const result = (await lightingTool.actions.setup_outdoor({
        time_of_day: 17.5,
        cloud_coverage: 0.7,
        fog_density: 0.02,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Indoor ────────────────────────────────────────────────────────

  describe("Indoor", () => {
    it("sets up indoor lighting with custom intensities", async () => {
      const result = (await lightingTool.actions.setup_indoor({
        sky_light_intensity: 2.0,
        rect_light_intensity: 5000,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Post-Process ──────────────────────────────────────────────────

  describe("Post-Process", () => {
    it("adds a bounded post-process volume", async () => {
      const result = (await lightingTool.actions.add_post_process({
        infinite: false,
        x: 0,
        y: 0,
        z: 200,
        extent_x: 500,
        extent_y: 500,
        extent_z: 500,
        bloom_intensity: 2.0,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });
});

describe.skipIf(editorRunning)("Lighting Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
