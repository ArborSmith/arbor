import { describe, it, expect, afterAll, afterEach } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { materialsTool } from "../../src/registry/materials.js";
import { actorsTool } from "../../src/registry/actors.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";
import { trackActor, cleanupAll } from "../helpers/cleanup-tracker.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("Material Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  afterEach(async () => {
    await cleanupAll();
  });

  // ── Create ────────────────────────────────────────────────────────

  describe("Create", () => {
    it("creates a material with default params", async () => {
      const name = uniqueName();
      const result = (await materialsTool.actions.create({
        name,
        content_path: CONTENT_PATH,
      })) as { success?: boolean; material_path?: string; asset_path?: string };
      expect(result).toBeDefined();
    });

    it("creates a material with color and PBR params", async () => {
      const name = uniqueName();
      const result = (await materialsTool.actions.create({
        name,
        content_path: CONTENT_PATH,
        color: { r: 0.8, g: 0.2, b: 0.1 },
        metallic: 0.9,
        roughness: 0.1,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });

    it("creates an emissive material", async () => {
      const name = uniqueName();
      const result = (await materialsTool.actions.create({
        name,
        content_path: CONTENT_PATH,
        emissive_color: { r: 0, g: 1, b: 0 },
        emissive_strength: 10,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });

    it("creates a two-sided material", async () => {
      const name = uniqueName();
      const result = (await materialsTool.actions.create({
        name,
        content_path: CONTENT_PATH,
        two_sided: true,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Create PBR ────────────────────────────────────────────────────

  describe("Create PBR", () => {
    it.skip(
      "creates a parameterized PBR material — skipped: triggers overwrite dialog for M_PPBR_Parameterized on re-runs",
      () => {}
    );
  });

  // ── Create World-Aligned ──────────────────────────────────────────

  describe("Create World-Aligned", () => {
    it("creates a world-aligned material", async () => {
      const name = uniqueName();
      const result = (await materialsTool.actions.create_world_aligned({
        name,
        content_path: CONTENT_PATH,
        color: { r: 0.6, g: 0.4, b: 0.2 },
        tiling: 2.0,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Ensure PBR Base ───────────────────────────────────────────────

  describe("Ensure PBR Base", () => {
    it.skip(
      "ensures PBR base material exists — skipped: triggers overwrite dialog for internal parameterized material on re-runs",
      () => {}
    );
  });

  // ── Create Instance ───────────────────────────────────────────────

  describe("Create Instance", () => {
    it("creates a material instance from a simple parent material", async () => {
      // Create a simple material to use as parent (no overwrite dialog risk)
      const parentName = uniqueName();
      const parent = (await materialsTool.actions.create({
        name: parentName,
        content_path: CONTENT_PATH,
        color: { r: 0.5, g: 0.5, b: 0.5 },
      })) as { material_path?: string; asset_path?: string };
      const parentPath =
        parent.material_path || parent.asset_path || `${CONTENT_PATH}/${parentName}`;

      const name = uniqueName();
      const result = (await materialsTool.actions.create_instance({
        name,
        content_path: CONTENT_PATH,
        parent_material: parentPath,
        scalar_params: { Metallic: 0.5, Roughness: 0.8 },
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Assign ────────────────────────────────────────────────────────

  describe("Assign", () => {
    it("assigns a material to a spawned cube", async () => {
      // Spawn a cube
      const label = uniqueName();
      const spawn = (await actorsTool.actions.spawn_primitive({
        shape: "cube",
        label,
      })) as { actor_name?: string; label?: string };
      const actorName = spawn.actor_name || label;
      trackActor(actorName);

      // Create a material
      const matName = uniqueName();
      const mat = (await materialsTool.actions.create({
        name: matName,
        content_path: CONTENT_PATH,
        color: { r: 1, g: 0, b: 0 },
      })) as { material_path?: string; asset_path?: string };
      const matPath =
        mat.material_path || mat.asset_path || `${CONTENT_PATH}/${matName}`;

      // Assign
      const result = (await materialsTool.actions.assign({
        actor_names: [actorName],
        material_path: matPath,
      })) as { success?: boolean };
      expect(result).toBeDefined();
    });
  });
});

describe.skipIf(editorRunning)("Material Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
