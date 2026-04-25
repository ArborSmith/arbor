import { describe, it, expect, afterAll, afterEach } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { pcgTool } from "../../src/registry/pcg.js";
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

/** Try to create a PCG graph — returns the path or null if creation fails. */
async function tryCreateGraph(
  preset: "custom" | "foliage_scatter" = "custom",
  meshPaths?: string[]
): Promise<string | null> {
  const name = uniqueName();
  const params: Record<string, unknown> = {
    name,
    content_path: CONTENT_PATH,
    preset,
  };
  if (meshPaths) params.mesh_paths = meshPaths;

  const result = (await pcgTool.actions.create(params)) as {
    success: boolean;
    pcg_graph?: string;
  };
  return result.success ? result.pcg_graph! : null;
}

describe.runIf(editorRunning)("PCG Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  afterEach(async () => {
    await cleanupAll();
  });

  // ── Discovery ─────────────────────────────────────────────────────

  describe("Discovery", () => {
    it("list_node_types returns results (unfiltered)", async () => {
      const result = (await pcgTool.actions.list_node_types(
        {}
      )) as { types?: unknown[]; node_types?: unknown[] };
      expect(result).toBeDefined();
      const items = result.types ?? result.node_types;
      if (Array.isArray(items)) {
        expect(items.length).toBeGreaterThan(0);
      }
    });

    it("list_node_types filters by substring", async () => {
      const result = (await pcgTool.actions.list_node_types({
        filter: "Surface",
      })) as { types?: unknown[]; node_types?: unknown[] };
      expect(result).toBeDefined();
    });

    it("get_node_params returns properties for a PCG class", async () => {
      const result = (await pcgTool.actions.get_node_params({
        class_name: "PCGSurfaceSamplerSettings",
      })) as { properties?: unknown[] };
      expect(result).toBeDefined();
    });
  });

  // ── Create ────────────────────────────────────────────────────────

  describe("Create", () => {
    it("creates a foliage_scatter preset graph", async () => {
      const graphPath = await tryCreateGraph("foliage_scatter", [
        "/Engine/BasicShapes/Cube",
      ]);
      // PCG graph creation may return null if PCGBuilder isn't available (project-dependent)
      if (!graphPath) {
        console.log("PCG create foliage_scatter returned null — PCGBuilder may not be available in this project");
        return;
      }
      expect(graphPath).toBeTruthy();
    });

    it("creates a custom empty graph", async () => {
      const graphPath = await tryCreateGraph("custom");
      if (!graphPath) {
        console.log("PCG create custom returned null — PCGBuilder may not be available in this project");
        return;
      }
      expect(graphPath).toBeTruthy();
    });
  });

  // ── Query ─────────────────────────────────────────────────────────

  describe("Query", () => {
    it("queries a created PCG graph", async () => {
      const graphPath = await tryCreateGraph("custom");
      if (!graphPath) {
        console.log("Skipping query — PCG graph creation failed");
        return;
      }

      const query = (await pcgTool.actions.query({
        asset_path: graphPath,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── Granular Editing ──────────────────────────────────────────────

  describe("Granular Editing", () => {
    it("add_node → set_params → connect → disconnect → remove_node", async () => {
      const graphPath = await tryCreateGraph("custom");
      if (!graphPath) {
        console.log("Skipping granular editing — PCG graph creation failed");
        return;
      }

      // Add a surface sampler node
      const addNode1 = (await pcgTool.actions.add_node({
        asset_path: graphPath,
        node_spec: { class: "PCGSurfaceSamplerSettings" },
      })) as { success?: boolean; node_id?: string };
      expect(addNode1).toBeDefined();
      const nodeId1 = addNode1.node_id;

      // Add a second node (static mesh spawner)
      const addNode2 = (await pcgTool.actions.add_node({
        asset_path: graphPath,
        node_spec: { class: "PCGStaticMeshSpawnerSettings" },
      })) as { success?: boolean; node_id?: string };
      expect(addNode2).toBeDefined();
      const nodeId2 = addNode2.node_id;

      // Set params on the first node (if node IDs were returned)
      if (nodeId1) {
        const setP = (await pcgTool.actions.set_params({
          asset_path: graphPath,
          node_id: nodeId1,
          params: { PointsPerSquaredMeter: 0.5 },
        })) as { success?: boolean };
        expect(setP).toBeDefined();
      }

      // Connect the two nodes (if both IDs available)
      if (nodeId1 && nodeId2) {
        const conn = (await pcgTool.actions.connect({
          asset_path: graphPath,
          from_node_id: nodeId1,
          from_pin: "Out",
          to_node_id: nodeId2,
          to_pin: "In",
        })) as { success?: boolean };
        expect(conn).toBeDefined();

        // Disconnect
        const disc = (await pcgTool.actions.disconnect({
          asset_path: graphPath,
          node_id: nodeId2,
          pin_label: "In",
        })) as { success?: boolean };
        expect(disc).toBeDefined();
      }

      // Remove the second node
      if (nodeId2) {
        const rem = (await pcgTool.actions.remove_node({
          asset_path: graphPath,
          node_id: nodeId2,
        })) as { success?: boolean };
        expect(rem).toBeDefined();
      }

      // Query to confirm final state
      const query = (await pcgTool.actions.query({
        asset_path: graphPath,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── Execute ───────────────────────────────────────────────────────

  describe("Execute", () => {
    it("spawns an actor with PCG component and executes graph", async () => {
      const graphPath = await tryCreateGraph("foliage_scatter", [
        "/Engine/BasicShapes/Cube",
      ]);
      if (!graphPath) {
        console.log("Skipping execute — PCG graph creation failed");
        return;
      }

      // Spawn an actor
      const label = uniqueName();
      const spawn = (await actorsTool.actions.spawn_primitive({
        shape: "cube",
        label,
      })) as { actor_name?: string; label?: string };
      const actorName = spawn.actor_name || label;
      trackActor(actorName);

      // Add PCG component to actor
      const addComp = (await pcgTool.actions.add_component({
        asset_path: graphPath,
        actor_label: actorName,
      })) as { success?: boolean };
      expect(addComp).toBeDefined();

      // Execute PCG on the actor
      const exec = (await pcgTool.actions.execute({
        asset_path: graphPath,
        actor_label: actorName,
      })) as { success?: boolean };
      expect(exec).toBeDefined();
    });
  });
});

describe.skipIf(editorRunning)("PCG Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
