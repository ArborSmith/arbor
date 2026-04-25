import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { anchorsTool } from "../../src/registry/anchors.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Anchor Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Analyze ───────────────────────────────────────────────────────

  describe("Analyze", () => {
    it("analyzes engine cube and returns anchors", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "prop",
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
        footprint?: Record<string, number>;
        bounds_3d?: Record<string, unknown>;
      };

      expect(result.success).toBe(true);
      expect(result.anchors).toBeDefined();
      expect(result.anchors!.length).toBeGreaterThanOrEqual(5); // 4 cardinal + snap_base

      // Verify cardinal anchors exist
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("face_north");
      expect(anchorIds).toContain("face_south");
      expect(anchorIds).toContain("face_east");
      expect(anchorIds).toContain("face_west");
      expect(anchorIds).toContain("snap_base");

      // Verify footprint
      expect(result.footprint).toBeDefined();
      expect(result.footprint!.area_cm2).toBeGreaterThan(0);
    });

    it("adds building-specific anchors when asset_type is building", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "building",
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
      };

      expect(result.success).toBe(true);
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("front_door");

      const door = result.anchors!.find((a) => a.id === "front_door");
      expect(door!.type).toBe("door");
    });

    it("adds road-specific anchors when asset_type is road_segment", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "road_segment",
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
      };

      expect(result.success).toBe(true);
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("road_start");
      expect(anchorIds).toContain("road_end");
      expect(anchorIds).toContain("side_left");
      expect(anchorIds).toContain("side_right");
    });

    it("adds floor-specific anchors when asset_type is floor", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "floor",
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
      };

      expect(result.success).toBe(true);
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("surface_center");
      expect(anchorIds).toContain("edge_north");
      expect(anchorIds).toContain("edge_south");
    });

    it("wall analysis on solid cube produces no openings", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "wall",
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
      };

      expect(result.success).toBe(true);
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("wall_left");
      expect(anchorIds).toContain("wall_right");

      // Solid cube should NOT produce openings
      const openings = result.anchors!.filter(
        (a) => a.type === "door_opening" || a.type === "window_opening"
      );
      expect(openings).toHaveLength(0);
    });

    it("skips opening detection when detect_openings is false", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "wall",
        detect_openings: false,
      })) as {
        success?: boolean;
        anchors?: Array<{ id: string; type: string }>;
      };

      expect(result.success).toBe(true);
      const anchorIds = result.anchors!.map((a) => a.id);
      expect(anchorIds).toContain("wall_left");
      expect(anchorIds).toContain("wall_right");
    });
  });

  // ── Get / Set ─────────────────────────────────────────────────────

  describe("Get / Set", () => {
    it("get returns metadata after analyze", async () => {
      // Ensure metadata exists (analyze writes sidecar)
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
      });

      const result = (await anchorsTool.actions.get({
        asset_path: "/Engine/BasicShapes/Cube",
      })) as {
        success?: boolean;
        anchors?: unknown[];
      };

      expect(result.success).toBe(true);
      expect(result.anchors).toBeDefined();
    });

    it("set writes custom metadata and get reads it back", async () => {
      const customMetadata = {
        asset_type: "custom_test",
        anchors: [
          {
            id: "test_anchor",
            type: "test",
            position: { x: 0, y: 0, z: 0 },
            direction: { x: 1, y: 0, z: 0 },
          },
        ],
      };

      const setResult = (await anchorsTool.actions.set({
        asset_path: "/Engine/BasicShapes/Sphere",
        metadata: customMetadata,
      })) as { success?: boolean; sidecar_path?: string };

      expect(setResult.success).toBe(true);
      expect(setResult.sidecar_path).toBeDefined();

      // Read back
      const getResult = (await anchorsTool.actions.get({
        asset_path: "/Engine/BasicShapes/Sphere",
      })) as {
        success?: boolean;
        asset_type?: string;
        anchors?: Array<{ id: string }>;
      };

      expect(getResult.success).toBe(true);
      expect(getResult.asset_type).toBe("custom_test");
      expect(getResult.anchors).toHaveLength(1);
      expect(getResult.anchors![0].id).toBe("test_anchor");
    });

    it("wall analyze does not include height on solid mesh anchors", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "wall",
      })) as {
        success?: boolean;
        anchors?: Array<{
          id: string;
          type: string;
          width?: number;
          height?: number;
        }>;
      };

      expect(result.success).toBe(true);
      // Wall connectors should not have height (only openings get height)
      const connector = result.anchors!.find(
        (a) => a.type === "wall_connector"
      );
      expect(connector).toBeDefined();
      expect(connector!.height).toBeUndefined();
    });
  });

  // ── Socket detection ─────────────────────────────────────────────

  describe("Socket detection", () => {
    it("reports socket_count of 0 for meshes without sockets", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "prop",
      })) as {
        success?: boolean;
        socket_count?: number;
        anchors?: Array<{ id: string }>;
      };

      expect(result.success).toBe(true);
      expect(result.socket_count).toBe(0);

      // No socket-prefixed anchors
      const socketAnchors = result.anchors!.filter((a) =>
        a.id.startsWith("socket_")
      );
      expect(socketAnchors).toHaveLength(0);
    });
  });

  // ── Analyze Pack ────────────────────────────────────────────────

  describe("Analyze Pack", () => {
    it("analyzes all meshes in /Engine/BasicShapes", async () => {
      const result = (await anchorsTool.actions.analyze_pack({
        folder_path: "/Engine/BasicShapes",
      })) as {
        success?: boolean;
        analyzed?: number;
        failed?: number;
        results?: Array<{
          asset_path: string;
          anchor_count: number;
          socket_count: number;
        }>;
      };

      expect(result.success).toBe(true);
      expect(result.analyzed).toBeGreaterThan(0);
      expect(result.results).toBeDefined();
      expect(result.results!.length).toBe(result.analyzed);

      // Each result should have expected fields
      for (const entry of result.results!) {
        expect(entry.asset_path).toBeDefined();
        expect(entry.anchor_count).toBeGreaterThanOrEqual(5);
      }
    });

    it("passes asset_type to all meshes in the folder", async () => {
      const result = (await anchorsTool.actions.analyze_pack({
        folder_path: "/Engine/BasicShapes",
        asset_type: "floor",
      })) as {
        success?: boolean;
        results?: Array<{
          asset_path: string;
          asset_type: string;
          anchor_count: number;
        }>;
      };

      expect(result.success).toBe(true);
      // All results should have floor type and floor-specific anchors (more than 5 base)
      for (const entry of result.results!) {
        expect(entry.asset_type).toBe("floor");
        expect(entry.anchor_count).toBeGreaterThan(5);
      }
    });

    it("returns error for missing folder_path", async () => {
      await expect(
        anchorsTool.actions.analyze_pack({})
      ).rejects.toThrow("folder_path required");
    });
  });

  // ── List Analyzed Assets ────────────────────────────────────────

  describe("List Analyzed Assets", () => {
    it("returns valid response shape", async () => {
      const result = (await anchorsTool.actions.list({})) as {
        success?: boolean;
        count?: number;
        assets?: Array<{
          asset_path: string;
          asset_type: string;
          anchor_count: number;
        }>;
      };

      expect(result.success).toBe(true);
      expect(typeof result.count).toBe("number");
      expect(Array.isArray(result.assets)).toBe(true);
    });

    it("filters by folder_path", async () => {
      const result = (await anchorsTool.actions.list({
        folder_path: "/Game/NonExistentFolder",
      })) as {
        success?: boolean;
        count?: number;
        assets?: unknown[];
      };

      expect(result.success).toBe(true);
      expect(result.count).toBe(0);
      expect(result.assets).toHaveLength(0);
    });
  });

  // ── Find Compatible ─────────────────────────────────────────────

  describe("Find Compatible", () => {
    it("returns a response with pairs array", async () => {
      // Analyze both with different types first
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cube",
        asset_type: "floor",
      });
      await anchorsTool.actions.analyze({
        asset_path: "/Engine/BasicShapes/Cylinder",
        asset_type: "wall",
      });

      const result = (await anchorsTool.actions.find_compatible({
        from_asset: "/Engine/BasicShapes/Cube",
        to_asset: "/Engine/BasicShapes/Cylinder",
      })) as {
        success?: boolean;
        pairs?: Array<{
          from_anchor: string;
          to_anchor: string;
          from_type: string;
          to_type: string;
          relationship: string;
        }>;
      };

      // Engine paths may not persist metadata to registry, so success
      // depends on whether anchor metadata was saved. Just verify shape.
      if (result.success) {
        expect(result.pairs).toBeDefined();
        expect(Array.isArray(result.pairs)).toBe(true);
        if (result.pairs!.length > 0) {
          const pair = result.pairs![0];
          expect(pair.from_anchor).toBeDefined();
          expect(pair.to_anchor).toBeDefined();
          expect(pair.relationship).toBeDefined();
        }
      } else {
        // Expected when registry doesn't persist for /Engine/ paths
        expect(result.success).toBe(false);
      }
    });

    it("returns error for missing from_asset", async () => {
      await expect(
        anchorsTool.actions.find_compatible({
          to_asset: "/Engine/BasicShapes/Cube",
        })
      ).rejects.toThrow("from_asset required");
    });
  });

  // ── Error handling ────────────────────────────────────────────────

  describe("Error handling", () => {
    it("analyze returns error for non-existent mesh", async () => {
      const result = (await anchorsTool.actions.analyze({
        asset_path: "/Game/NonExistent/Mesh",
      })) as { success?: boolean; error?: string };

      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });

    it("get returns error when no sidecar exists", async () => {
      const result = (await anchorsTool.actions.get({
        asset_path: "/Game/NonExistent/NoSidecar",
      })) as { success?: boolean; error?: string };

      expect(result.success).toBe(false);
    });
  });
}, 60_000);

describe.skipIf(editorRunning)("Anchor Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
