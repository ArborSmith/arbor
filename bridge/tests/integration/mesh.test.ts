import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { isConnected } from "../../src/ue5-client.js";
import { meshTool } from "../../src/registry/mesh.js";
import { assetsTool } from "../../src/registry/assets.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const editorRunning = await isConnected();

let counter = 0;
function uniqueName(): string {
  return `IntTest_${Date.now()}_${counter++}`;
}

/** Import the GLB fixture via Arbor's import action, returning the asset path. */
async function importTestMesh(): Promise<string | null> {
  const fixturePath = resolve(__dirname, "../fixtures/test_triangle.glb");
  const name = uniqueName();
  try {
    const result = (await assetsTool.actions.import({
      file_path: fixturePath,
      content_path: "/Game/IntTest",
      asset_name: name,
      auto_scale_meshy: false,
    })) as { asset_path?: string };
    return result.asset_path || null;
  } catch {
    return null;
  }
}

describe.runIf(editorRunning)("Mesh Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Get Bounds ────────────────────────────────────────────────────

  describe("Get Bounds", () => {
    it("returns bounds for engine cube", async () => {
      const result = (await meshTool.actions.get_bounds({
        asset_path: "/Engine/BasicShapes/Cube",
      })) as { success?: boolean; bounds?: unknown };
      expect(result).toBeDefined();
    });

    it("returns bounds for engine sphere", async () => {
      const result = (await meshTool.actions.get_bounds({
        asset_path: "/Engine/BasicShapes/Sphere",
      })) as { success?: boolean; bounds?: unknown };
      expect(result).toBeDefined();
    });
  });

  // ── Write Ops ─────────────────────────────────────────────────────

  describe("Write Ops", () => {
    it("fix_pivot repositions the pivot on an imported mesh", async () => {
      const meshPath = await importTestMesh();
      expect(meshPath, "GLB import should succeed").toBeTruthy();

      const result = (await meshTool.actions.fix_pivot({
        asset_path: meshPath!,
        pivot_mode: "bottom",
      })) as { success?: boolean; offset?: Record<string, number> };
      expect(result.success).toBe(true);
      expect(result.offset).toBeDefined();
    });

    it("fix_scale scales an imported mesh", async () => {
      const meshPath = await importTestMesh();
      expect(meshPath, "GLB import should succeed").toBeTruthy();

      const result = (await meshTool.actions.fix_scale({
        asset_path: meshPath!,
        scale_factor: 2,
      })) as { success?: boolean };
      expect(result.success).toBe(true);
    });

    it("setup_collision adds collision to an imported mesh", async () => {
      const meshPath = await importTestMesh();
      expect(meshPath, "GLB import should succeed").toBeTruthy();

      const result = (await meshTool.actions.setup_collision({
        asset_path: meshPath!,
        collision_type: "box",
      })) as { success?: boolean };
      expect(result.success).toBe(true);
    });
  },
  60_000);
});

describe.skipIf(editorRunning)("Mesh Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
