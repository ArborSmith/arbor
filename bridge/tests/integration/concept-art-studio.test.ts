import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { conceptArtStudioTool } from "../../src/registry/concept-art-studio.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

// ── Concept Art Studio ──────────────────────────────────────────

describe.runIf(editorRunning)("Concept Art Studio", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let locationPath: string | undefined;

  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Setup: create a test codex entry ──────────────────────────

  it("creates test location for studio tests", async () => {
    const name = uniqueName();
    const result = (await codexTool.actions.create({
      category: "location",
      name,
      content_path: "/Game/IntTest",
      properties: {
        LocationName: name,
        Description: "A mysterious dark forest with ancient twisted oaks",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    locationPath = result.asset_path as string;
  });

  // ── open ──────────────────────────────────────────────────────

  it("opens studio with codex context", async () => {
    expect(locationPath).toBeDefined();
    const result = (await conceptArtStudioTool.actions.open({
      codex_asset_path: locationPath!,
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.step).toBe("context");
    expect(result.codex_asset_path).toBe(locationPath);
  });

  // ── set_prompt ────────────────────────────────────────────────

  it("sets prompt for review", async () => {
    const result = (await conceptArtStudioTool.actions.set_prompt({
      prompt:
        "A mysterious dark forest with ancient twisted oaks, ethereal mist, moonlight filtering through the canopy",
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.step).toBe("prompt_review");
  });

  // ── get_state ─────────────────────────────────────────────────

  it("reads studio state after set_prompt", async () => {
    const result = (await conceptArtStudioTool.actions.get_state(
      {}
    )) as Record<string, unknown>;

    expect(result.step).toBe("prompt_review");
    expect(result.prompt).toContain("dark forest");
    expect(result.codex_asset_path).toBe(locationPath);
  });

  // ── get_approval (before user acts) ───────────────────────────

  it("get_approval returns pending state", async () => {
    const result = (await conceptArtStudioTool.actions.get_approval(
      {}
    )) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.action).toBe("");
    expect(result.prompt).toContain("dark forest");
    expect(result.num_images).toBe(4);
  });

  // ── set_results ───────────────────────────────────────────────

  it("sends results to studio", async () => {
    const testImages = [
      { path: "D:/test/concept_1.png", label: "Variant 1" },
      { path: "D:/test/concept_2.png", label: "Variant 2" },
    ];

    const result = (await conceptArtStudioTool.actions.set_results({
      images: testImages,
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.step).toBe("results");
    expect(result.image_count).toBe(2);
  });

  // ── get_selection (before user acts) ──────────────────────────

  it("get_selection returns pending state", async () => {
    const result = (await conceptArtStudioTool.actions.get_selection(
      {}
    )) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.action).toBe("");
    expect(result.selected_index).toBe(-1);
  });

  // ── get_state shows results step ──────────────────────────────

  it("state shows results with images", async () => {
    const result = (await conceptArtStudioTool.actions.get_state(
      {}
    )) as Record<string, unknown>;

    expect(result.step).toBe("results");
    const images = result.images as Array<Record<string, unknown>>;
    expect(images).toBeDefined();
    expect(images.length).toBe(2);
    expect(images[0].label).toBe("Variant 1");
  });
});

describe.skipIf(editorRunning)("Concept Art Studio (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
