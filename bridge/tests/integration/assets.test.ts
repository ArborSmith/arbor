import { describe, it, expect, afterAll } from "vitest";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { isConnected } from "../../src/ue5-client.js";
import { assetsTool } from "../../src/registry/assets.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const editorRunning = await isConnected();

let counter = 0;
function uniqueName(): string {
  return `IntTest_${Date.now()}_${counter++}`;
}

describe.runIf(editorRunning)("Asset Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Find ──────────────────────────────────────────────────────────

  describe("Find", () => {
    it("finds assets by query", async () => {
      const result = (await assetsTool.actions.find({
        query: "Cube",
      })) as { results?: unknown[]; assets?: unknown[] };
      expect(result).toBeDefined();
    });

    it("finds assets with type_filter", async () => {
      const result = (await assetsTool.actions.find({
        query: "Cube",
        type_filter: "mesh",
      })) as { results?: unknown[]; assets?: unknown[] };
      expect(result).toBeDefined();
    });

    it("respects limit parameter", async () => {
      const result = (await assetsTool.actions.find({
        query: "Cube",
        limit: 3,
      })) as { results?: unknown[]; assets?: unknown[] };
      expect(result).toBeDefined();
      const items = result.results ?? result.assets;
      if (Array.isArray(items)) {
        expect(items.length).toBeLessThanOrEqual(3);
      }
    });
  });

  // ── Scan ──────────────────────────────────────────────────────────

  describe("Scan", () => {
    it("scans project registry", async () => {
      const result = await assetsTool.actions.scan({});
      expect(result).toBeDefined();
    });
  });

  // ── Stats ─────────────────────────────────────────────────────────

  describe("Stats", () => {
    it("returns registry stats", async () => {
      const result = await assetsTool.actions.stats({});
      expect(result).toBeDefined();
    });
  });

  // ── Import ────────────────────────────────────────────────────────

  describe("Import", () => {
    it(
      "imports a GLB fixture into /Game/IntTest",
      async () => {
        const fixturePath = resolve(
          __dirname,
          "../fixtures/test_triangle.glb"
        );
        const result = (await assetsTool.actions.import({
          file_path: fixturePath,
          content_path: "/Game/IntTest",
          asset_name: uniqueName(),
          auto_scale_meshy: false,
        })) as {
          asset_path?: string;
          asset_name?: string;
          imported_paths?: string[];
          message?: string;
        };
        expect(result.asset_path).toBeTruthy();
        expect(result.asset_name).toBeTruthy();
      },
      60_000
    );
  });
});

describe.skipIf(editorRunning)("Asset Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
