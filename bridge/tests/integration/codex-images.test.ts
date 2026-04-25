import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

// ── Concept Art Image Operations ─────────────────────────────────

describe.runIf(editorRunning)("Codex Image Operations", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let locationPath: string | undefined;
  let characterPath: string | undefined;
  let enemyPath: string | undefined;

  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Setup: create test entries ────────────────────────────────

  it("creates test location for image tests", async () => {
    const name = uniqueName();
    const result = (await codexTool.actions.create({
      category: "location",
      name,
      content_path: "/Game/IntTest",
      properties: {
        LocationName: name,
        Description: "A dark enchanted forest with ancient oaks",
        Region: "Northlands",
        Atmosphere: "Misty and foreboding",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    locationPath = result.asset_path as string;
  });

  it("creates test character for image tests", async () => {
    const name = uniqueName();
    const result = (await codexTool.actions.create({
      category: "character",
      name,
      content_path: "/Game/IntTest",
      properties: {
        CharacterName: name,
        Archetype: "Warrior",
        Backstory: "A battle-hardened knight seeking redemption",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    characterPath = result.asset_path as string;
  });

  it("creates test character (enemy) for image tests", async () => {
    const name = uniqueName();
    const result = (await codexTool.actions.create({
      category: "character",
      name,
      content_path: "/Game/IntTest",
      properties: {
        CharacterName: name,
        Role: "Enemy",
        Description: "A skeletal warrior risen from ancient battlefields",
        Tags: ["Undead", "Melee", "Aggressive"],
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    enemyPath = result.asset_path as string;
  });

  // ── get_images ────────────────────────────────────────────────

  describe("get_images", () => {
    it("returns empty image data for new entry", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.get_images({
        asset_path: locationPath!,
      })) as Record<string, unknown>;

      expect(result.concept_art).toBeDefined();
      expect(result.gallery).toBeDefined();
      expect(Array.isArray(result.gallery)).toBe(true);
      expect((result.gallery as unknown[]).length).toBe(0);
      expect(result.prompt).toBeDefined();
      expect(result.asset_path).toBe(locationPath);
    });

    it("returns error for non-existent asset", async () => {
      const result = (await codexTool.actions.get_images({
        asset_path: "/Game/NonExistent/FakeAsset",
      })) as Record<string, unknown>;

      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });
  });

  // ── set_concept_art ───────────────────────────────────────────

  describe("set_concept_art", () => {
    it("sets concept art with texture path and prompt", async () => {
      expect(locationPath).toBeDefined();
      // Use a fake texture path — we're testing the property setting, not UE5 texture loading
      const result = (await codexTool.actions.set_concept_art({
        asset_path: locationPath!,
        texture_path: "/Game/IntTest/T_TestConceptArt",
        prompt: "A dark enchanted forest with ancient oaks and mist",
      })) as Record<string, unknown>;

      expect(result.success).toBe(true);
      expect(result.asset_path).toBe(locationPath);
      expect(result.texture_path).toBe("/Game/IntTest/T_TestConceptArt");
    });

    it("verifies concept art was set via get_images", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.get_images({
        asset_path: locationPath!,
      })) as Record<string, unknown>;

      // The concept_art field should now contain the texture path
      expect(result.concept_art).toBeDefined();
      expect(typeof result.concept_art).toBe("string");
      expect((result.concept_art as string)).toContain("T_TestConceptArt");
      expect(result.prompt).toBe(
        "A dark enchanted forest with ancient oaks and mist"
      );
    });

    it("returns error for non-existent asset", async () => {
      const result = (await codexTool.actions.set_concept_art({
        asset_path: "/Game/NonExistent/FakeAsset",
        texture_path: "/Game/Textures/T_Test",
      })) as Record<string, unknown>;

      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });
  });

  // ── add_gallery_image ─────────────────────────────────────────

  describe("add_gallery_image", () => {
    it("adds image to gallery", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.add_gallery_image({
        asset_path: locationPath!,
        texture_path: "/Game/IntTest/T_Gallery_01",
      })) as Record<string, unknown>;

      expect(result.success).toBe(true);
      expect(result.gallery_count).toBe(1);
    });

    it("adds second image to gallery", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.add_gallery_image({
        asset_path: locationPath!,
        texture_path: "/Game/IntTest/T_Gallery_02",
      })) as Record<string, unknown>;

      expect(result.success).toBe(true);
      expect(result.gallery_count).toBe(2);
    });

    it("verifies gallery contents via get_images", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.get_images({
        asset_path: locationPath!,
      })) as Record<string, unknown>;

      const gallery = result.gallery as string[];
      expect(gallery.length).toBe(2);
      expect(gallery[0]).toContain("T_Gallery_01");
      expect(gallery[1]).toContain("T_Gallery_02");
    });

    it("returns error for non-existent asset", async () => {
      const result = (await codexTool.actions.add_gallery_image({
        asset_path: "/Game/NonExistent/FakeAsset",
        texture_path: "/Game/Textures/T_Test",
      })) as Record<string, unknown>;

      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });
  });

  // ── Concept art fields visible in codex get ────────────────────

  describe("Concept art in codex get", () => {
    it("codex get includes ConceptArt and ConceptArtPrompt fields", async () => {
      expect(locationPath).toBeDefined();
      const result = (await codexTool.actions.get({
        asset_path: locationPath!,
      })) as Record<string, unknown>;

      expect(result._category).toBe("location");
      // The serializer should include the new fields
      expect(result.ConceptArtPrompt).toBeDefined();
      expect(result.ConceptArtPrompt).toBe(
        "A dark enchanted forest with ancient oaks and mist"
      );
    });
  });
});

describe.skipIf(editorRunning)("Codex Image Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
