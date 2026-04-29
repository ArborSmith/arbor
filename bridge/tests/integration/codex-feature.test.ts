import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

// ── Feature CRUD ──────────────────────────────────────────────────

describe.runIf(editorRunning)("Codex Feature CRUD", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let createdPath: string | undefined;
  const testName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  it("creates a feature entry", async () => {
    const result = (await codexTool.actions.create({
      category: "feature",
      name: testName,
      content_path: "/Game/IntTest",
      properties: {
        FeatureName: testName,
        Category: "Combat",
        DesignIntent: "Make combat feel visceral and rewarding",
        Description: "A test feature created by integration tests",
        Rules: "Combo system with timing windows",
        RelatedStats: ["Strength", "Dexterity"],
        Examples: "Light-light-heavy combo deals 2x damage",
        Tags: ["Core Loop", "Combat"],
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    expect(result._category).toBe("feature");
    createdPath = result.asset_path as string;
  });

  it("lists feature entries", async () => {
    const result = (await codexTool.actions.list({
      category: "feature",
    })) as Record<string, unknown>[];
    expect(result).toBeDefined();
    expect(Array.isArray(result)).toBe(true);
    const found = result.some(
      (e) => (e._path as string).startsWith(createdPath!)
    );
    expect(found).toBe(true);
  });

  // Trimmed: DesignIntent, Rules, RelatedStats, Examples fields are not implemented on
  // UArborFeatureAsset (see Source/Arbor/Public/ArborGameContextTypes.h:131-165 — only
  // FeatureName, Category, Description, Tags, ConceptArt*, Status, LockedFields exist).
  it("retrieves the created feature via get", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("feature");
    expect(result.FeatureName).toBe(testName);
    expect(result.Category).toBe("Combat");
    expect(result.Description).toBe(
      "A test feature created by integration tests"
    );
  });

  it("updates the feature with partial properties", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: {
        Category: "Core Loop",
        Description: "Updated feature description",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.Category).toBe("Core Loop");
    expect(result.Description).toBe("Updated feature description");
    // Unchanged fields should remain
    expect(result.FeatureName).toBe(testName);
  });

  it("finds the feature via search", async () => {
    const result = (await codexTool.actions.search({
      query: testName,
      category: "feature",
    })) as Record<string, unknown>[];
    expect(result).toBeDefined();
    expect(result.length).toBeGreaterThan(0);
    expect(result[0]._category).toBe("feature");
  });

  it("deletes the feature entry", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.delete({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.deleted_path).toBe(createdPath);
  });

  it("confirms deletion — get returns not-found", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result.success).toBe(false);
    expect(result.error).toBeDefined();
  });
});

describe.skipIf(editorRunning)("Codex Feature (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
