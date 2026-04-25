import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { codexTool } from "../../src/registry/codex.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Codex Status field", () => {
  const timestamp = Date.now();
  let counter = 0;
  const uniqueName = () => `IntTest_${timestamp}_${++counter}`;

  let createdPath: string | undefined;
  const testName = uniqueName();

  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Create with status ────────────────────────────────────────────

  it("creates an entry with a status", async () => {
    const result = (await codexTool.actions.create({
      category: "location",
      name: testName,
      content_path: "/Game/IntTest",
      properties: { LocationName: testName, Description: "A test location" },
      status: "Ideation",
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);
    expect(result.asset_path).toBeDefined();
    createdPath = result.asset_path as string;
  });

  // ── Get returns status ────────────────────────────────────────────

  it("get returns the status field", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;

    expect(result._category).toBe("location");
    expect(result.Status).toBe("Ideation");
  });

  // ── Update status ─────────────────────────────────────────────────

  it("updates the status via update action", async () => {
    expect(createdPath).toBeDefined();
    const result = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: {},
      status: "Prototype",
    })) as Record<string, unknown>;

    expect(result.success).toBe(true);

    // Verify with get
    const entry = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;
    expect(entry.Status).toBe("Prototype");
  });

  // ── List with status filter ───────────────────────────────────────

  it("list filters by status", async () => {
    // Create a second entry with a different status
    const name2 = uniqueName();
    const created2 = (await codexTool.actions.create({
      category: "location",
      name: name2,
      content_path: "/Game/IntTest",
      properties: { LocationName: name2, Description: "Another test location" },
      status: "Production",
    })) as Record<string, unknown>;
    expect(created2.success).toBe(true);

    // List with Prototype filter — should include entry 1 but not entry 2
    const filtered = (await codexTool.actions.list({
      category: "location",
      status: "Prototype",
    })) as Record<string, unknown>[];
    expect(Array.isArray(filtered)).toBe(true);

    const paths = filtered.map((e) => e._path as string);
    expect(paths.some((p) => p.startsWith(createdPath!))).toBe(true);
    expect(paths.some((p) => p.startsWith(created2.asset_path as string))).toBe(false);

    // List with Production filter — should include entry 2 but not entry 1
    const filtered2 = (await codexTool.actions.list({
      category: "location",
      status: "Production",
    })) as Record<string, unknown>[];
    expect(Array.isArray(filtered2)).toBe(true);

    const paths2 = filtered2.map((e) => e._path as string);
    expect(paths2.some((p) => p.startsWith(created2.asset_path as string))).toBe(true);
    expect(paths2.some((p) => p.startsWith(createdPath!))).toBe(false);
  });

  // ── Search with status filter ─────────────────────────────────────

  it("search filters by status", async () => {
    // Search for our test entries with Prototype filter
    const results = (await codexTool.actions.search({
      query: `IntTest_${timestamp}`,
      category: "location",
      status: "Prototype",
    })) as Record<string, unknown>[];
    expect(Array.isArray(results)).toBe(true);

    // All results should have Prototype status
    for (const r of results) {
      expect(r.Status).toBe("Prototype");
    }

    // Should find our first entry (Prototype) but not the second (Production)
    const resultPaths = results.map((r) => r._path as string);
    expect(resultPaths.some((p) => p.startsWith(createdPath!))).toBe(true);
  });

  // ── Status as a lockable field ────────────────────────────────────

  it("status is lockable via LockedFields", async () => {
    expect(createdPath).toBeDefined();

    // Lock the Status field
    const lockResult = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: { LockedFields: ["Status"] },
    })) as Record<string, unknown>;
    expect(lockResult.success).toBe(true);

    // Try to update status — should be skipped
    const updateResult = (await codexTool.actions.update({
      asset_path: createdPath!,
      properties: { Status: "Complete" },
    })) as Record<string, unknown>;
    expect(updateResult.success).toBe(true);

    const skipped = updateResult._skipped_locked_fields as string[];
    expect(skipped).toContain("Status");

    // Verify status is unchanged
    const entry = (await codexTool.actions.get({
      asset_path: createdPath!,
    })) as Record<string, unknown>;
    expect(entry.Status).toBe("Prototype");
  });

  // ── Default status is None ────────────────────────────────────────

  it("default status is None for newly created entries", async () => {
    const name3 = uniqueName();
    const created = (await codexTool.actions.create({
      category: "character",
      name: name3,
      content_path: "/Game/IntTest",
      properties: { CharacterName: name3 },
    })) as Record<string, unknown>;
    expect(created.success).toBe(true);

    const entry = (await codexTool.actions.get({
      asset_path: created.asset_path as string,
    })) as Record<string, unknown>;
    expect(entry.Status).toBe("None");
  });
});

describe.skipIf(editorRunning)("Codex Status field (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
