import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { runPython } from "../../src/tools/core/run-python.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Codex Operations", () => {
  // ── Search ─────────────────────────────────────────────────────────

  describe("Search", () => {
    it("searches across all categories", async () => {
      const result = (await codexTool.actions.search({
        query: "forest",
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
    });

    it("searches with category filter", async () => {
      const result = (await codexTool.actions.search({
        query: "dark",
        category: "location",
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      for (const entry of result) {
        expect((entry as Record<string, unknown>)._category).toBe("location");
      }
    });

    it("respects limit parameter", async () => {
      const result = (await codexTool.actions.search({
        query: "a",
        limit: 3,
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      expect(result.length).toBeLessThanOrEqual(3);
    });

    it("returns empty array for no matches", async () => {
      const result = (await codexTool.actions.search({
        query: "zzzzxnonexistent99",
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      expect(result.length).toBe(0);
    });

    it("returns entries with expected metadata fields", async () => {
      const result = (await codexTool.actions.search({
        query: "a",
        limit: 1,
      })) as Record<string, unknown>[];
      if (result.length > 0) {
        const entry = result[0];
        expect(entry._category).toBeDefined();
        expect(entry._path).toBeDefined();
        expect(entry._name).toBeDefined();
        expect(entry._score).toBeDefined();
        expect(typeof entry._score).toBe("number");
      }
    });
  });

  // ── List ──────────────────────────────────────────────────────────

  describe("List", () => {
    it("lists entries for a valid category", async () => {
      const result = (await codexTool.actions.list({
        category: "location",
      })) as Record<string, unknown>[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      for (const entry of result) {
        expect(entry._path).toBeDefined();
        expect(entry._name).toBeDefined();
      }
    });

    it("returns available categories when no category given", async () => {
      const result = (await codexTool.actions.list({
        category: "",
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
    });

    it("returns empty array for unknown category", async () => {
      const result = (await codexTool.actions.list({
        category: "nonexistent",
      })) as unknown[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      expect(result.length).toBe(0);
    });
  });

  // ── Get ───────────────────────────────────────────────────────────

  describe("Get", () => {
    it("retrieves a codex entry by path", async () => {
      // First list entries to get a valid path
      const list = (await codexTool.actions.list({
        category: "location",
      })) as Record<string, unknown>[];

      if (list.length > 0) {
        const path = list[0]._path as string;
        const result = (await codexTool.actions.get({
          asset_path: path,
        })) as Record<string, unknown>;
        expect(result).toBeDefined();
        expect(result._category).toBe("location");
        expect(result._path).toBeDefined();
        expect(result._name).toBeDefined();
      }
    });

    it("returns error for non-existent path", async () => {
      const result = (await codexTool.actions.get({
        asset_path: "/Game/NonExistent/FakeAsset",
      })) as Record<string, unknown>;
      expect(result).toBeDefined();
      expect(result.success).toBe(false);
      expect(result.error).toBeDefined();
    });
  });

  // ── Context ───────────────────────────────────────────────────────

  describe("Context", () => {
    it("returns game context assets", async () => {
      const result = (await codexTool.actions.context({})) as Record<
        string,
        unknown
      >[];
      expect(result).toBeDefined();
      expect(Array.isArray(result)).toBe(true);
      for (const ctx of result) {
        expect(ctx._path).toBeDefined();
        expect(ctx._name).toBeDefined();
      }
    });
  });
});

// ── CRUD (Create / Update / Delete) ───────────────────────────────

describe.runIf(editorRunning)("Codex CRUD Operations", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let createdPath: string | undefined;
  const testName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  it("creates a location entry", async () => {
    const result = (await codexTool.actions.create({
      category: "location",
      name: testName,
      content_path: "/Game/IntTest",
      properties: {
        LocationName: testName,
        Description: "A test location created by integration tests",
        Region: "TestRegion",
        Tags: ["feature1", "feature2"],
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    expect(result._category).toBe("location");
    createdPath = result.asset_path as string;
  });

  it("retrieves the created entry via get", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("location");
    expect(result.LocationName).toBe(testName);
    expect(result.Description).toBe(
      "A test location created by integration tests"
    );
    expect(result.Region).toBe("TestRegion");
    expect(Array.isArray(result.Tags)).toBe(true);
    expect((result.Tags as string[]).length).toBe(2);
  });

  it("updates the entry with partial properties", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: {
        Description: "Updated description",
        Region: "UpdatedRegion",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.Description).toBe("Updated description");
    expect(result.Region).toBe("UpdatedRegion");
    // Unchanged fields should remain
    expect(result.LocationName).toBe(testName);
  });

  it("deletes the entry", async () => {
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

// ── Locked Field Enforcement ──────────────────────────────────────

describe.runIf(editorRunning)("Locked Field Enforcement", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let createdPath: string | undefined;
  const testName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  it("creates a test location for lock tests", async () => {
    const result = (await codexTool.actions.create({
      category: "location",
      name: testName,
      content_path: "/Game/IntTest",
      properties: {
        LocationName: testName,
        Description: "Original description",
        Region: "OriginalRegion",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    createdPath = result.asset_path as string;
  });

  it("sets LockedFields via Python", async () => {
    expect(createdPath).toBeDefined();
    const pyResult = await runPython({
      code: `
import unreal, arbor.utils
asset = unreal.EditorAssetLibrary.load_asset("${createdPath}")
locked = asset.get_editor_property("locked_fields")
locked.add("Description")
asset.set_editor_property("locked_fields", locked)
unreal.EditorAssetLibrary.save_loaded_asset(asset)
final = list(asset.get_editor_property("locked_fields"))
arbor.utils.write_result({"success": True, "locked": final})
`,
    });

    expect(pyResult.success).toBe(true);
    const data = pyResult.result as Record<string, unknown>;
    expect(data.success).toBe(true);
    expect(data.locked).toContain("Description");
  });

  it("get returns LockedFields array", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("location");
    expect(Array.isArray(result.LockedFields)).toBe(true);
    expect(result.LockedFields).toContain("Description");
  });

  it("update skips locked fields and applies unlocked ones", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: {
        Description: "Hacked description",
        Region: "NewRegion",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);

    // Verify feedback arrays
    const skipped = result._skipped_locked_fields as string[];
    const updated = result._updated_fields as string[];
    expect(skipped).toContain("Description");
    expect(updated).toContain("Region");
    expect(updated).not.toContain("Description");
  });

  it("confirms locked field was not changed", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result.Description).toBe("Original description");
    expect(result.Region).toBe("NewRegion");
  });

  it("update cannot modify LockedFields itself", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: {
        LockedFields: [],
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const skipped = result._skipped_locked_fields as string[];
    expect(skipped).toContain("LockedFields");

    // Verify LockedFields unchanged
    const getResult = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;
    expect(Array.isArray(getResult.LockedFields)).toBe(true);
    expect(getResult.LockedFields).toContain("Description");
  });
});

// ── GameContext Update (Issue #92) ───────────────────────────────

describe.runIf(editorRunning)("GameContext Update", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let contextPath: string | undefined;
  const testTitle = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  it("creates a GameContext asset via Python", async () => {
    const pyResult = await runPython({
      code: `
import unreal
import arbor.utils

gc_class = unreal.load_class(None, "/Script/Arbor.ArborGameContextAsset")
factory = unreal.DataAssetFactory()
factory.set_editor_property("data_asset_class", gc_class)

tools = unreal.AssetToolsHelpers.get_asset_tools()
asset = tools.create_asset("GC_${testTitle}", "/Game/IntTest", None, factory)
asset.set_editor_property("game_title", "${testTitle}")
asset.set_editor_property("genre", "RPG")
asset.set_editor_property("setting", "Fantasy")
asset.modify()
unreal.EditorAssetLibrary.save_loaded_asset(asset)

arbor.utils.write_result({"success": True, "path": asset.get_path_name()})
`,
    });

    expect(pyResult.success).toBe(true);
    const data = pyResult.result as Record<string, unknown>;
    expect(data.success).toBe(true);
    contextPath = data.path as string;
  });

  it("retrieves the GameContext via codex get", async () => {
    expect(contextPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: contextPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("context");
    expect(result._path).toBeDefined();
    expect(result._name).toBeDefined();
    expect(result.GameTitle).toBe(testTitle);
  });

  it("updates GameContext properties via codex update", async () => {
    expect(contextPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: contextPath!,
      properties: {
        GameTitle: "Updated Title",
        Genre: "Action-Adventure",
        Setting: "Sci-Fi",
        Tone: "Dark",
        WorldDescription: "A vast futuristic world",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result._category).toBe("context");
    expect(result._path).toBeDefined();
    expect(result._name).toBeDefined();
    expect(result._updated_fields).toBeDefined();
    expect(result._skipped_locked_fields).toBeDefined();

    const updated = result._updated_fields as string[];
    expect(updated).toContain("GameTitle");
    expect(updated).toContain("Genre");
    expect(updated).toContain("Setting");
  });

  it("confirms updated values via get", async () => {
    expect(contextPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: contextPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("context");
    expect(result.GameTitle).toBe("Updated Title");
    expect(result.Genre).toBe("Action-Adventure");
    expect(result.Setting).toBe("Sci-Fi");
    expect(result.Tone).toBe("Dark");
    expect(result.WorldDescription).toBe("A vast futuristic world");
  });

  it("respects locked fields on GameContext update", async () => {
    expect(contextPath).toBeDefined();

    // Lock the Genre field via Python
    const pyResult = await runPython({
      code: `
import unreal
import arbor.utils
asset = unreal.EditorAssetLibrary.load_asset("${contextPath}")
asset.modify()
asset.locked_fields.add("Genre")
unreal.EditorAssetLibrary.save_loaded_asset(asset)
arbor.utils.write_result({"success": True, "locked": list(asset.locked_fields)})
`,
    });
    expect(pyResult.success).toBe(true);

    // Try to update Genre (locked) and Tone (unlocked)
    const result = (await codexTool.actions.update({
      asset_path: contextPath!,
      properties: {
        Genre: "Puzzle",
        Tone: "Lighthearted",
      },
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    const skipped = result._skipped_locked_fields as string[];
    const updated = result._updated_fields as string[];
    expect(skipped).toContain("Genre");
    expect(updated).toContain("Tone");
    expect(updated).not.toContain("Genre");

    // Verify Genre unchanged, Tone updated
    const getResult = (await codexTool.actions.get({
      asset_path: contextPath!,
    })) as Record<string, unknown>;
    expect(getResult.Genre).toBe("Action-Adventure");
    expect(getResult.Tone).toBe("Lighthearted");
  });
});

describe.skipIf(editorRunning)("Codex Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
