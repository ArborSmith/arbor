import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { anchorsTool } from "../../src/registry/anchors.js";
import { environmentTool } from "../../src/registry/environment.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

let counter = 0;
function uniqueEnvId(): string {
  return `IntTest_${Date.now()}_${counter++}`;
}

describe.runIf(editorRunning)("Environment Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Resolve ───────────────────────────────────────────────────────

  describe("Resolve", () => {
    it("resolves a simple 2-node graph to transforms", async () => {
      // Ensure anchor metadata exists for engine cube
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "floor",
      });
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Sphere",
        asset_type: "prop",
      });

      const graph = {
        id: "test_resolve",
        nodes: {
          floor: {
            asset_path: "/Engine/BasicShapes/Cube",
            asset_type: "floor",
          },
          prop: {
            asset_path: "/Engine/BasicShapes/Sphere",
            asset_type: "prop",
          },
        },
        edges: [
          {
            from: { node: "floor", anchor: "edge_north" },
            to: { node: "prop", anchor: "snap_base" },
            relationship: "adjacent",
          },
        ],
      };

      const result = (await environmentTool.actions.resolve({
        graph,
      })) as {
        success?: boolean;
        transforms?: Record<string, unknown>;
        node_count?: number;
      };

      expect(result.success).toBe(true);
      expect(result.node_count).toBe(2);
      expect(result.transforms).toBeDefined();
      expect(result.transforms!.floor).toBeDefined();
      expect(result.transforms!.prop).toBeDefined();
    });

    it("resolves a graph with facing relationship and gap", async () => {
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "building",
      });

      const graph = {
        id: "test_facing",
        nodes: {
          house_a: {
            asset_path: "/Engine/BasicShapes/Cube",
            asset_type: "building",
          },
          house_b: {
            asset_path: "/Engine/BasicShapes/Cube",
            asset_type: "building",
          },
        },
        edges: [
          {
            from: { node: "house_a", anchor: "face_north" },
            to: { node: "house_b", anchor: "face_south" },
            relationship: "facing",
            params: { gap: 500 },
          },
        ],
      };

      const result = (await environmentTool.actions.resolve({
        graph,
      })) as {
        success?: boolean;
        transforms?: Record<
          string,
          { location?: { x: number; y: number; z: number } }
        >;
      };

      expect(result.success).toBe(true);
      const locA = result.transforms!.house_a?.location;
      const locB = result.transforms!.house_b?.location;
      expect(locA).toBeDefined();
      expect(locB).toBeDefined();

      // house_b should be offset from house_a in the +X direction (face_north dir)
      // with gap=500, so the distance should be > 0
      expect(locB!.x).toBeGreaterThan(locA!.x);
    });
  });

  // ── Build & Clear ─────────────────────────────────────────────────

  describe("Build & Clear", () => {
    it("builds a 2-node environment and spawns actors", async () => {
      // Ensure anchor metadata
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "floor",
      });
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Sphere",
        asset_type: "prop",
      });

      const envId = uniqueEnvId();
      const graph = {
        id: envId,
        nodes: {
          floor: {
            asset_path: "/Engine/BasicShapes/Cube",
            asset_type: "floor",
          },
          prop: {
            asset_path: "/Engine/BasicShapes/Sphere",
            asset_type: "prop",
          },
        },
        edges: [
          {
            from: { node: "floor", anchor: "edge_north" },
            to: { node: "prop", anchor: "snap_base" },
            relationship: "adjacent",
          },
        ],
      };

      const buildResult = (await environmentTool.actions.build({
        graph,
        environment_id: envId,
      })) as {
        success?: boolean;
        spawned?: Array<{ node_id: string; actor_name: string }>;
        failed?: unknown[];
        transforms?: Record<string, unknown>;
      };

      expect(buildResult.success).toBe(true);
      expect(buildResult.spawned).toBeDefined();
      expect(buildResult.spawned!.length).toBe(2);
      expect(buildResult.failed).toHaveLength(0);
      expect(buildResult.transforms).toBeDefined();

      // Verify actor labels contain the environment ID
      const actorNames = buildResult.spawned!.map((s) => s.actor_name);
      for (const name of actorNames) {
        expect(name).toContain(`Env_${envId}_`);
      }

      // Clean up
      const clearResult = (await environmentTool.actions.clear({
        environment_id: envId,
      })) as { success?: boolean; destroyed_count?: number };

      expect(clearResult.success).toBe(true);
      expect(clearResult.destroyed_count).toBe(2);
    });

    it("clear on non-existent environment returns 0 destroyed", async () => {
      const result = (await environmentTool.actions.clear({
        environment_id: "nonexistent_env_12345",
      })) as { success?: boolean; destroyed_count?: number };

      expect(result.success).toBe(true);
      expect(result.destroyed_count).toBe(0);
    });
  });

  // ── Error handling ────────────────────────────────────────────────

  describe("Error handling", () => {
    it("build returns error for empty graph", async () => {
      try {
        await environmentTool.actions.build({ graph: { nodes: {} } });
        expect.fail("Should have thrown");
      } catch (e) {
        expect((e as Error).message).toContain("at least one node");
      }
    });

    it("resolve returns error for invalid JSON", async () => {
      const result = (await environmentTool.actions.resolve({
        graph: {},
      })) as { success?: boolean; error?: string };

      expect(result.success).toBe(false);
    });
  });
}, 120_000);

describe.skipIf(editorRunning)("Environment Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
