import { describe, it, expect, beforeAll, afterAll, afterEach } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { actorsTool } from "../../src/registry/actors.js";
import { trackActor, cleanupAll } from "../helpers/cleanup-tracker.js";
import { clearTestActors } from "../helpers/level-isolation.js";

const editorRunning = await isConnected();

interface SpawnResult {
  actor_name?: string;
  actor_path?: string;
  shape?: string;
  position?: { x: number; y: number; z: number };
  scale?: { x: number; y: number; z: number };
  error?: string;
}

interface SceneInfoResult {
  actors?: Array<{
    name: string;
    label: string;
    class: string;
    location?: { x: number; y: number; z: number };
  }>;
  count?: number;
}

interface ModifyResult {
  success: boolean;
  error?: string;
}

interface DeleteResult {
  deleted?: string[];
  not_found?: string[];
  error?: string;
}

interface SnapResult {
  success: boolean;
  location?: { x: number; y: number; z: number };
  error?: string;
}

const PREFIX = "IntTest_";
let counter = 0;
function uniqueLabel(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

async function spawnTestCube(
  overrides: Record<string, unknown> = {}
): Promise<SpawnResult> {
  const label = uniqueLabel();
  const result = (await actorsTool.actions.spawn_primitive({
    shape: "cube",
    label,
    ...overrides,
  })) as SpawnResult;
  const actorName = result.actor_name || label;
  trackActor(actorName);
  return result;
}

describe.runIf(editorRunning)("Actor Operations", () => {
  beforeAll(async () => {
    await clearTestActors();
  });

  afterAll(async () => {
    await cleanupAll();
  });

  afterEach(async () => {
    await cleanupAll();
  });

  // ── Smoke ──────────────────────────────────────────────────────────

  describe("Smoke", () => {
    it("isConnected returns true", async () => {
      expect(await isConnected()).toBe(true);
    });

    it("list returns without error", async () => {
      const result = await actorsTool.actions.list({});
      expect(result).toBeDefined();
    });
  });

  // ── Spawn ──────────────────────────────────────────────────────────

  describe("Spawn", () => {
    it("spawns a cube at origin", async () => {
      const result = await spawnTestCube();
      expect(result.actor_name).toBeTruthy();
    });

    it("spawns a sphere at a specific position", async () => {
      const label = uniqueLabel();
      const result = (await actorsTool.actions.spawn_primitive({
        shape: "sphere",
        x: 500,
        y: 500,
        z: 200,
        label,
      })) as SpawnResult;
      trackActor(result.actor_name || label);
      expect(result.actor_name).toBeTruthy();
    });

    it("spawns with a custom label", async () => {
      const label = uniqueLabel();
      const result = (await actorsTool.actions.spawn_primitive({
        shape: "cube",
        label,
      })) as SpawnResult;
      trackActor(result.actor_name || label);
      expect(result.actor_name).toBeTruthy();
    });

    it("spawns with scale", async () => {
      const result = await spawnTestCube({
        scale_x: 2,
        scale_y: 2,
        scale_z: 3,
      });
      expect(result.actor_name).toBeTruthy();
    });

    it("spawns with rotation", async () => {
      const result = await spawnTestCube({
        pitch: 45,
        yaw: 90,
        roll: 0,
      });
      expect(result.actor_name).toBeTruthy();
    });
  });

  // ── Scene Info ─────────────────────────────────────────────────────

  describe("Scene Info", () => {
    it("returns actors list", async () => {
      const result = (await actorsTool.actions.scene_info(
        {}
      )) as SceneInfoResult;
      expect(result).toBeDefined();
    });

    it("filters by prefix", async () => {
      const spawn = await spawnTestCube();
      const name = spawn.actor_name!;

      const result = (await actorsTool.actions.scene_info({
        filter_prefix: PREFIX,
      })) as SceneInfoResult;

      expect(result.actors).toBeDefined();
      expect(result.actors!.length).toBeGreaterThan(0);

      const found = result.actors!.some(
        (a) =>
          a.name.includes(PREFIX) ||
          a.label.includes(PREFIX) ||
          a.name === name ||
          a.label === name
      );
      expect(found).toBe(true);
    });
  });

  // ── Inspect ────────────────────────────────────────────────────────

  describe("Inspect", () => {
    interface InspectResult {
      success: boolean;
      actor_name?: string;
      actor_class?: string;
      properties?: Array<{
        name: string;
        type: string;
        value: string;
        category?: string;
      }>;
      components?: Array<{
        name: string;
        class: string;
        properties: Array<{
          name: string;
          type: string;
          value: string;
          category?: string;
        }>;
      }>;
      error?: string;
    }

    it("inspects a spawned cube and returns properties", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.inspect({
        actor_name: spawn.actor_name,
      })) as InspectResult;

      expect(result.success).toBe(true);
      expect(result.actor_name).toBeTruthy();
      expect(result.actor_class).toBeTruthy();
      expect(result.properties).toBeDefined();
      expect(Array.isArray(result.properties)).toBe(true);
      expect(result.components).toBeDefined();
      expect(Array.isArray(result.components)).toBe(true);
    });

    it("returns component list with properties", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.inspect({
        actor_name: spawn.actor_name,
      })) as InspectResult;

      expect(result.success).toBe(true);
      expect(result.components!.length).toBeGreaterThan(0);

      // A spawned cube should have a StaticMeshComponent
      const meshComp = result.components!.find(
        (c) => c.class.includes("StaticMeshComponent")
      );
      expect(meshComp).toBeDefined();
      expect(meshComp!.properties.length).toBeGreaterThan(0);
    });

    it("filters properties by name substring", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.inspect({
        actor_name: spawn.actor_name,
        property_filter: "Mobility",
      })) as InspectResult;

      expect(result.success).toBe(true);
      // All returned properties should match the filter
      const allProps = [
        ...result.properties!,
        ...result.components!.flatMap((c) => c.properties),
      ];
      for (const prop of allProps) {
        expect(prop.name.toLowerCase()).toContain("mobility");
      }
    });

    it("filters components by name/class", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.inspect({
        actor_name: spawn.actor_name,
        component_filter: "StaticMesh",
      })) as InspectResult;

      expect(result.success).toBe(true);
      // All components should match the filter
      for (const comp of result.components!) {
        const matches =
          comp.name.toLowerCase().includes("staticmesh") ||
          comp.class.toLowerCase().includes("staticmesh");
        expect(matches).toBe(true);
      }
    });

    it("errors on nonexistent actor", async () => {
      const result = (await actorsTool.actions.inspect({
        actor_name: "NonExistentActor_99999",
      })) as InspectResult;
      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });

    it("each property has name, type, and value fields", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.inspect({
        actor_name: spawn.actor_name,
        property_filter: "Mobility",
      })) as InspectResult;

      expect(result.success).toBe(true);
      const allProps = [
        ...result.properties!,
        ...result.components!.flatMap((c) => c.properties),
      ];
      expect(allProps.length).toBeGreaterThan(0);

      for (const prop of allProps) {
        expect(typeof prop.name).toBe("string");
        expect(typeof prop.type).toBe("string");
        expect(typeof prop.value).toBe("string");
      }
    });
  });

  // ── Modify ─────────────────────────────────────────────────────────

  describe("Modify", () => {
    it("moves an actor", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.modify({
        actor_name: spawn.actor_name,
        position: { x: 1000, y: 2000, z: 300 },
      })) as ModifyResult;
      expect(result.success).toBe(true);
    });

    it("rotates an actor", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.modify({
        actor_name: spawn.actor_name,
        rotation: { pitch: 30, yaw: 60, roll: 0 },
      })) as ModifyResult;
      expect(result.success).toBe(true);
    });

    it("scales an actor", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.modify({
        actor_name: spawn.actor_name,
        scale: { x: 3, y: 3, z: 3 },
      })) as ModifyResult;
      expect(result.success).toBe(true);
    });

    it("toggles visibility", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.modify({
        actor_name: spawn.actor_name,
        visible: false,
      })) as ModifyResult;
      expect(result.success).toBe(true);
    });

    it("errors on nonexistent actor", async () => {
      const result = (await actorsTool.actions.modify({
        actor_name: "NonExistentActor_99999",
        position: { x: 0, y: 0, z: 0 },
      })) as ModifyResult;
      expect(result.success).toBe(false);
    });
  });

  // ── Snap to Ground ────────────────────────────────────────────────

  describe("Snap to Ground", () => {
    it("snaps an actor from height", async () => {
      const spawn = await spawnTestCube({ z: 5000 });
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.snap_to_ground({
        actor_name: spawn.actor_name,
      })) as SnapResult;
      expect(result.success).toBe(true);
    });

    it("snaps with offset", async () => {
      const spawn = await spawnTestCube({ z: 5000 });
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.snap_to_ground({
        actor_name: spawn.actor_name,
        offset: 50,
      })) as SnapResult;
      expect(result.success).toBe(true);
    });
  });

  // ── Delete ────────────────────────────────────────────────────────

  describe("Delete", () => {
    it("deletes a single actor", async () => {
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.delete({
        actor_names: [spawn.actor_name!],
      })) as DeleteResult;
      expect(result.deleted).toBeDefined();
      expect(result.deleted!.length).toBeGreaterThanOrEqual(1);
    });

    it("deletes multiple actors", async () => {
      const spawn1 = await spawnTestCube();
      const spawn2 = await spawnTestCube();
      expect(spawn1.actor_name).toBeTruthy();
      expect(spawn2.actor_name).toBeTruthy();

      const result = (await actorsTool.actions.delete({
        actor_names: [spawn1.actor_name!, spawn2.actor_name!],
      })) as DeleteResult;
      expect(result.deleted).toBeDefined();
      expect(result.deleted!.length).toBeGreaterThanOrEqual(2);
    });

    it("handles nonexistent actor gracefully", async () => {
      const result = (await actorsTool.actions.delete({
        actor_names: ["NonExistentActor_99999"],
      })) as DeleteResult;
      expect(result).toBeDefined();
    });
  });

  // ── Lifecycle ─────────────────────────────────────────────────────

  describe("Lifecycle", () => {
    it("spawn → move → verify → delete", async () => {
      // Spawn
      const spawn = await spawnTestCube();
      expect(spawn.actor_name).toBeTruthy();
      const name = spawn.actor_name!;

      // Move
      const modify = (await actorsTool.actions.modify({
        actor_name: name,
        position: { x: 999, y: 888, z: 777 },
      })) as ModifyResult;
      expect(modify.success).toBe(true);

      // Verify it exists in scene
      const scene = (await actorsTool.actions.scene_info({
        filter_prefix: PREFIX,
      })) as SceneInfoResult;
      const found = scene.actors?.some(
        (a) => a.name === name || a.label === name
      );
      expect(found).toBe(true);

      // Delete
      const del = (await actorsTool.actions.delete({
        actor_names: [name],
      })) as DeleteResult;
      expect(del.deleted).toBeDefined();
      expect(del.deleted!.length).toBeGreaterThanOrEqual(1);

      // Verify it's gone
      const sceneAfter = (await actorsTool.actions.scene_info({
        filter_prefix: PREFIX,
      })) as SceneInfoResult;
      const stillThere = sceneAfter.actors?.some(
        (a) => a.name === name || a.label === name
      );
      expect(stillThere).toBeFalsy();
    });
  });
});

describe.skipIf(editorRunning)("Actor Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
