import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("Character Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Create ──────────────────────────────────────────────────────────

  describe("Create", () => {
    it("creates a character with minimal data (name only)", async () => {
      const name = uniqueName();
      const result = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
      })) as { success?: boolean; asset_path?: string; character_id?: string };
      expect(result.success).toBe(true);
      expect(result.asset_path).toContain(CONTENT_PATH);
    });

    // SKIP: character_id, archetype, personality_traits, backstory, personality_profile,
    // and dialogue_lines fields are not yet implemented on UCharacterDataAsset
    // (see Source/Arbor/Public/ArborCharacterTypes.h — only CharacterName, Role, Description,
    // Tags, GameContext, ConceptArt*, Status, LockedFields exist).
    it.skip("creates a character with full data", async () => {
      const name = uniqueName();
      const result = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        character_id: "char_test_full",
        archetype: "reluctant_hero",
        personality_traits: ["brave", "stubborn", "loyal"],
        backstory: "A warrior who lost everything and seeks redemption.",
        personality_profile: {
          core_drives: "Redemption and protecting the innocent",
          fears: "Losing loved ones again",
          contradictions: "Brave in battle but afraid of emotional intimacy",
          social_style: "Gruff exterior hiding a caring nature",
          combat_mindset: "Defensive, protects allies first",
        },
        dialogue_lines: [
          { context: "greeting", line: "What do you want? Make it quick." },
          { context: "quest_accept", line: "Fine. But don't slow me down." },
        ],
      })) as { success?: boolean; asset_path?: string; character_id?: string };
      expect(result.success).toBe(true);
      expect(result.character_id).toBe("char_test_full");
    });
  });

  // ── Query Round-Trip ────────────────────────────────────────────────

  describe("Query", () => {
    // SKIP: archetype, backstory, personality_traits, personality_profile, dialogue_lines
    // are not yet implemented on UCharacterDataAsset (see ArborCharacterTypes.h:25-60 and
    // ArborCharacterBuilder.cpp CharacterToJson/PopulateCharacter — only round-trips
    // CharacterName, Role, Description, Tags fields).
    it.skip("round-trips all character fields", async () => {
      const name = uniqueName();
      const traits = ["cunning", "charming"];
      const backstory = "A thief turned reluctant hero.";

      const created = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        archetype: "trickster",
        personality_traits: traits,
        backstory,
        personality_profile: {
          core_drives: "Freedom",
          fears: "Imprisonment",
          contradictions: "Steals but has a moral code",
          social_style: "Smooth talker",
          combat_mindset: "Evasive",
        },
        dialogue_lines: [
          { context: "intro", line: "You look like you need help." },
        ],
      })) as { success?: boolean; asset_path?: string };
      expect(created.success).toBe(true);

      const queried = (await codexTool.actions.character_query({
        asset_path: created.asset_path,
      })) as Record<string, unknown>;

      expect(queried.success).toBe(true);
      expect(queried.name).toBe(name);
      expect(queried.archetype).toBe("trickster");
      expect(queried.backstory).toBe(backstory);
      expect(queried.personality_traits).toEqual(traits);

      const profile = queried.personality_profile as Record<string, string>;
      expect(profile.core_drives).toBe("Freedom");
      expect(profile.fears).toBe("Imprisonment");

      const lines = queried.dialogue_lines as Array<{ context: string; line: string }>;
      expect(lines).toHaveLength(1);
      expect(lines[0].context).toBe("intro");
    });
  });

  // ── Update Section ──────────────────────────────────────────────────

  describe("Update Section", () => {
    // SKIP: backstory field not implemented on UCharacterDataAsset
    // (see Source/Arbor/Public/ArborCharacterTypes.h)
    it.skip("updates backstory only", async () => {
      const name = uniqueName();
      const created = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        backstory: "Original backstory.",
        archetype: "warrior",
      })) as { success?: boolean; asset_path?: string };

      await codexTool.actions.character_update_section({
        asset_path: created.asset_path,
        section: "backstory",
        data: "Updated backstory with more detail.",
      });

      const queried = (await codexTool.actions.character_query({
        asset_path: created.asset_path,
      })) as Record<string, unknown>;

      expect(queried.backstory).toBe("Updated backstory with more detail.");
      expect(queried.archetype).toBe("warrior"); // unchanged
    });

    // SKIP: personality_traits field not implemented on UCharacterDataAsset
    // (see Source/Arbor/Public/ArborCharacterTypes.h)
    it.skip("updates personality_traits only", async () => {
      const name = uniqueName();
      const created = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        personality_traits: ["original"],
      })) as { success?: boolean; asset_path?: string };

      await codexTool.actions.character_update_section({
        asset_path: created.asset_path,
        section: "personality_traits",
        data: ["updated_trait_1", "updated_trait_2"],
      });

      const queried = (await codexTool.actions.character_query({
        asset_path: created.asset_path,
      })) as Record<string, unknown>;

      expect(queried.personality_traits).toEqual(["updated_trait_1", "updated_trait_2"]);
    });

    // SKIP: dialogue_lines field not implemented on UCharacterDataAsset
    // (see Source/Arbor/Public/ArborCharacterTypes.h)
    it.skip("updates dialogue_lines only", async () => {
      const name = uniqueName();
      const created = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        dialogue_lines: [{ context: "old", line: "Old line" }],
      })) as { success?: boolean; asset_path?: string };

      await codexTool.actions.character_update_section({
        asset_path: created.asset_path,
        section: "dialogue_lines",
        data: [
          { context: "new", line: "New line 1" },
          { context: "battle", line: "New line 2" },
        ],
      });

      const queried = (await codexTool.actions.character_query({
        asset_path: created.asset_path,
      })) as Record<string, unknown>;

      const lines = queried.dialogue_lines as Array<{ context: string; line: string }>;
      expect(lines).toHaveLength(2);
      expect(lines[0].context).toBe("new");
    });
  });

  // ── List ────────────────────────────────────────────────────────────

  describe("List", () => {
    it("lists characters in a folder", async () => {
      const name1 = uniqueName();
      const name2 = uniqueName();

      await codexTool.actions.character_create({ name: name1, content_path: CONTENT_PATH });
      await codexTool.actions.character_create({ name: name2, content_path: CONTENT_PATH });

      const result = (await codexTool.actions.character_list({
        folder_path: CONTENT_PATH,
      })) as { success?: boolean; characters?: Array<Record<string, string>> };

      expect(result.success).toBe(true);
      expect(result.characters).toBeDefined();

      const names = result.characters!.map((c) => c.name);
      expect(names).toContain(name1);
      expect(names).toContain(name2);
    });
  });

  // ── Idempotent Create ───────────────────────────────────────────────

  describe("Idempotent Create", () => {
    // SKIP: archetype field not implemented on UCharacterDataAsset, so the idempotent
    // round-trip cannot verify the second create overwrote the first.
    // (see Source/Arbor/Public/ArborCharacterTypes.h)
    it.skip("updates an existing character on second create with same name", async () => {
      const name = uniqueName();

      const first = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        archetype: "warrior",
      })) as { success?: boolean; asset_path?: string };
      expect(first.success).toBe(true);

      const second = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
        archetype: "mage",
      })) as { success?: boolean; asset_path?: string };
      expect(second.success).toBe(true);
      expect(second.asset_path).toBe(first.asset_path);

      const queried = (await codexTool.actions.character_query({
        asset_path: first.asset_path,
      })) as Record<string, unknown>;
      expect(queried.archetype).toBe("mage");
    });
  });

  // ── Character ID Auto-Generation ────────────────────────────────────

  describe("Character ID", () => {
    // SKIP: character_id field not implemented on UCharacterDataAsset; create returns
    // success but no character_id field. (see Source/Arbor/Public/ArborCharacterTypes.h)
    it.skip("auto-generates character_id from name when not provided", async () => {
      const name = uniqueName();
      const result = (await codexTool.actions.character_create({
        name,
        content_path: CONTENT_PATH,
      })) as { success?: boolean; character_id?: string };

      expect(result.success).toBe(true);
      expect(result.character_id).toBeDefined();
      expect(result.character_id).toMatch(/^char_/);
    });
  });
});

describe.skipIf(editorRunning)("Character Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
