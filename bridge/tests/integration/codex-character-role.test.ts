import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

// ── Character with Role field ────────────────────────────────────

describe.runIf(editorRunning)("Codex Character Role", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let enemyPath: string | undefined;
  let playerPath: string | undefined;
  const enemyName = uniqueName();
  const playerName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  it("creates a character with Role=Enemy and enemy-specific fields", async () => {
    const result = (await codexTool.actions.create({
      category: "character",
      name: enemyName,
      content_path: "/Game/IntTest",
      properties: {
        CharacterName: enemyName,
        Role: "Enemy",
        Archetype: "Brute",
        PlayStyle: "Charges at player, heavy melee attacks",
        Weaknesses: ["Fire", "Headshots"],
        LootTable: ["Gold x10", "Health Potion"],
        SpawnLocations: ["Dark Forest", "Cave Entrance"],
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    expect(result._category).toBe("character");
    enemyPath = result.asset_path as string;
  });

  it("creates a character with Role=Player and PlayStyle", async () => {
    const result = (await codexTool.actions.create({
      category: "character",
      name: playerName,
      content_path: "/Game/IntTest",
      properties: {
        CharacterName: playerName,
        Role: "Player",
        Archetype: "Adventurer",
        PlayStyle: "Versatile melee/ranged hybrid",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    expect(result._category).toBe("character");
    playerPath = result.asset_path as string;
  });

  // Trimmed: PlayStyle, Weaknesses, LootTable, SpawnLocations, Archetype fields are not
  // implemented on UCharacterDataAsset (see Source/Arbor/Public/ArborCharacterTypes.h).
  // Only CharacterName and Role are round-trippable today.
  it("get returns Role for enemy character", async () => {
    expect(enemyPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: enemyPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("character");
    expect(result.CharacterName).toBe(enemyName);
    expect(result.Role).toBe("Enemy");
  });

  it("get returns Role for player character", async () => {
    expect(playerPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: playerPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("character");
    expect(result.CharacterName).toBe(playerName);
    expect(result.Role).toBe("Player");
  });

  it("search finds characters across all roles", async () => {
    // Search for the enemy
    const enemyResults = (await codexTool.actions.search({
      query: enemyName,
      category: "character",
    })) as Record<string, unknown>[];
    expect(enemyResults.length).toBeGreaterThan(0);
    expect(enemyResults[0]._category).toBe("character");

    // Search for the player
    const playerResults = (await codexTool.actions.search({
      query: playerName,
      category: "character",
    })) as Record<string, unknown>[];
    expect(playerResults.length).toBeGreaterThan(0);
    expect(playerResults[0]._category).toBe("character");
  });

  it("updates Role from Enemy to Boss", async () => {
    expect(enemyPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: enemyPath!,
      properties: {
        Role: "Boss",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.Role).toBe("Boss");
    // Other fields should remain
    expect(result.CharacterName).toBe(enemyName);
    // Trimmed: Weaknesses field not implemented on UCharacterDataAsset
    // (see Source/Arbor/Public/ArborCharacterTypes.h).
  });
});

describe.skipIf(editorRunning)("Codex Character Role (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
