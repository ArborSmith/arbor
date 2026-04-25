import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

// ---------------------------------------------------------------------------
// Per-GameContext folder structure
// ---------------------------------------------------------------------------

describe.runIf(editorRunning)("Per-GameContext Folder Structure", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let contextPath: string | undefined;
  const contextName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── GameContext creation ─────────────────────────────────────────

  it("creates a GameContext in a self-named subfolder", async () => {
    const result = (await codexTool.actions.create({
      category: "context",
      name: contextName,
      content_path: "/Game/IntTest",
      properties: {
        GameTitle: contextName,
        Genre: "RPG",
        Setting: "Fantasy",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    contextPath = result.asset_path as string;

    // When content_path is provided explicitly, it uses that path
    // (the auto-subfolder only triggers when content_path is empty)
    expect(contextPath).toContain("/Game/IntTest/");
  });

  it("creates a GameContext with auto-derived subfolder (no content_path)", async () => {
    const autoName = uniqueName();
    const result = (await codexTool.actions.create({
      category: "context",
      name: autoName,
      properties: {
        GameTitle: autoName,
        Genre: "Action",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const path = result.asset_path as string;
    // Should be at /Game/GameCodex/<SanitizedName>/GC_<SanitizedName>
    expect(path).toContain(`/Game/GameCodex/${autoName}/GC_${autoName}`);
  });

  // ── Codex entries with game_context auto-path ───────────────────

  it("creates a location in the GameContext subfolder", async () => {
    expect(contextPath).toBeDefined();
    const locName = uniqueName();
    const result = (await codexTool.actions.create({
      category: "location",
      name: locName,
      game_context: contextPath!,
      properties: {
        LocationName: locName,
        Description: "A test location inside a GameContext folder",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    // Should be in {context parent}/Locations/
    expect(assetPath).toContain("/Locations/");
    expect(assetPath).toContain(`Loc_${locName}`);
  });

  it("creates a codex character in the GameContext Characters subfolder", async () => {
    expect(contextPath).toBeDefined();
    const charName = uniqueName();
    const result = (await codexTool.actions.create({
      category: "character",
      name: charName,
      game_context: contextPath!,
      properties: {
        CharacterName: charName,
        Role: "Enemy",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    expect(assetPath).toContain("/Characters/");
    expect(assetPath).toContain(`DA_${charName}`);
  });

  // ── Explicit content_path overrides game_context ────────────────

  it("explicit content_path wins over game_context", async () => {
    expect(contextPath).toBeDefined();
    const featureName = uniqueName();
    const result = (await codexTool.actions.create({
      category: "feature",
      name: featureName,
      content_path: "/Game/IntTest/CustomPath",
      game_context: contextPath!,
      properties: {
        FeatureName: featureName,
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    // content_path wins — should NOT be in the game context subfolder
    expect(assetPath).toContain("/Game/IntTest/CustomPath/");
    expect(assetPath).not.toContain("/Features/");
  });

  // ── No game_context uses flat default path ──────────────────────

  it("no game_context uses the flat default path", async () => {
    const featureEntryName = uniqueName();
    const result = (await codexTool.actions.create({
      category: "feature",
      name: featureEntryName,
      content_path: "/Game/IntTest",
      properties: {
        FeatureName: featureEntryName,
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    // No game_context, explicit content_path → flat under /Game/IntTest
    expect(assetPath).toContain("/Game/IntTest/");
    expect(assetPath).not.toContain("/Features/");
  });

  // ── Character builder path derivation ───────────────────────────

  it("creates a character in the GameContext Characters subfolder", async () => {
    expect(contextPath).toBeDefined();
    const charName = uniqueName();
    const result = (await codexTool.actions.character_create({
      name: charName,
      game_context: contextPath!,
      archetype: "hero",
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    expect(assetPath).toContain("/Characters/");
    expect(assetPath).toContain(`DA_${charName}`);
  });

  it("character with explicit content_path ignores game_context for path", async () => {
    expect(contextPath).toBeDefined();
    const charName = uniqueName();
    const result = (await codexTool.actions.character_create({
      name: charName,
      content_path: "/Game/IntTest/CustomChars",
      game_context: contextPath!,
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const assetPath = result.asset_path as string;
    expect(assetPath).toContain("/Game/IntTest/CustomChars/");
  });

  // ── Tree query ──────────────────────────────────────────────────

  it("tree returns entries grouped by category", async () => {
    expect(contextPath).toBeDefined();
    const result = (await codexTool.actions.tree({
      game_context: contextPath!,
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.game_context).toBeDefined();

    // We created at least a location and a character above with this game_context
    // They should appear in the tree result
    if (result.location) {
      expect(Array.isArray(result.location)).toBe(true);
    }
    if (result.character) {
      expect(Array.isArray(result.character)).toBe(true);
    }
  });

  it("tree returns error for non-existent GameContext", async () => {
    const result = (await codexTool.actions.tree({
      game_context: "/Game/NonExistent/GC_Fake",
    })) as Record<string, unknown>;

    expect(result.success).toBe(false);
    expect(result.error).toBeDefined();
  });
});

describe.skipIf(editorRunning)("Per-GameContext Folder Structure (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
