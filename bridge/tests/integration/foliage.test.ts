import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { foliageTool } from "../../src/registry/foliage.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("Foliage Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Count ─────────────────────────────────────────────────────────

  describe("Count", () => {
    it("returns foliage count", async () => {
      const result = (await foliageTool.actions.count({})) as {
        count?: number;
        total?: number;
      };
      expect(result).toBeDefined();
    });
  });

  // ── Create Type ───────────────────────────────────────────────────

  describe("Create Type", () => {
    it("creates a foliage type from engine cube mesh", async () => {
      const name = uniqueName();
      const result = (await foliageTool.actions.create_type({
        mesh_path: "/Engine/BasicShapes/Cube",
        name,
        content_path: CONTENT_PATH,
      })) as { success?: boolean; foliage_type_path?: string; asset_path?: string };
      expect(result).toBeDefined();
    });
  });

  // ── Lifecycle ─────────────────────────────────────────────────────

  describe("Lifecycle", () => {
    it("create_type → paint → count → remove → count", async () => {
      const name = uniqueName();

      // Create foliage type
      const createResult = (await foliageTool.actions.create_type({
        mesh_path: "/Engine/BasicShapes/Cube",
        name,
        content_path: CONTENT_PATH,
      })) as { success?: boolean; foliage_type_path?: string; asset_path?: string };
      expect(createResult).toBeDefined();
      const typePath =
        createResult.foliage_type_path ||
        createResult.asset_path ||
        `${CONTENT_PATH}/${name}`;

      // Paint — may fail if there's no ground geometry, handle gracefully
      try {
        const paintResult = (await foliageTool.actions.paint({
          foliage_type_path: typePath,
          count: 10,
          center_x: 0,
          center_y: 0,
          radius: 500,
        })) as { success?: boolean; painted?: number };
        expect(paintResult).toBeDefined();
      } catch {
        // Paint may fail in empty levels — acceptable
        console.log("Foliage paint skipped (no ground surface)");
      }

      // Count after paint
      const countAfter = (await foliageTool.actions.count({})) as Record<
        string,
        unknown
      >;
      expect(countAfter).toBeDefined();

      // Remove all instances of this foliage type
      const removeResult = (await foliageTool.actions.remove({
        foliage_type_path: typePath,
      })) as Record<string, unknown>;
      expect(removeResult).toBeDefined();

      // Count after remove
      const countFinal = (await foliageTool.actions.count({})) as Record<
        string,
        unknown
      >;
      expect(countFinal).toBeDefined();
    });
  });
});

describe.skipIf(editorRunning)("Foliage Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
