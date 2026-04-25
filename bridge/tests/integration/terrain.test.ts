import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { terrainTool } from "../../src/registry/terrain.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("Terrain Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Create ────────────────────────────────────────────────────────

  describe("Create", () => {
    it.skip(
      "creates a flat landscape — skipped: triggers LandscapeEditor modal dialog that crashes in unattended mode",
      () => {}
    );
  });

  // ── Height Queries ────────────────────────────────────────────────

  describe("Height Queries", () => {
    it("samples height at origin", async () => {
      const result = (await terrainTool.actions.sample_height({
        x: 0,
        y: 0,
      })) as { height?: number; z?: number };
      expect(result).toBeDefined();
    });

    it("finds the highest point", async () => {
      const result = (await terrainTool.actions.find_highest(
        {}
      )) as Record<string, unknown>;
      expect(result).toBeDefined();
    });

    it("finds the lowest point", async () => {
      const result = (await terrainTool.actions.find_lowest(
        {}
      )) as Record<string, unknown>;
      expect(result).toBeDefined();
    });

    it("finds a flat area", async () => {
      const result = (await terrainTool.actions.find_flat({
        min_radius: 200,
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
    });
  });

  // ── Paint ─────────────────────────────────────────────────────────

  describe("Paint", () => {
    it.skip("paints a circle — skipped: requires landscape (create triggers modal crash)", () => {});
  });

  // ── Water ─────────────────────────────────────────────────────────

  describe("Water", () => {
    it.skip("add_river — skipped: WaterEditor triggers modal dialog that crashes in unattended mode", () => {});
    it.skip("add_lake — skipped: WaterEditor triggers modal dialog that crashes in unattended mode", () => {});
  });
});

describe.skipIf(editorRunning)("Terrain Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
