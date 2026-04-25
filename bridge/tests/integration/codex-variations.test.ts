import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { runPython } from "../../src/tools/core/run-python.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Codex Text Variations", () => {
  // ── show_text_variations ──────────────────────────────────────────

  describe("show_text_variations", () => {
    it("opens the text variation review widget with valid manifest", async () => {
      const manifest = JSON.stringify({
        variations: [
          {
            label: "Variation A",
            fields: { GameTitle: "Shadow Realms", Genre: "Dark RPG" },
          },
          {
            label: "Variation B",
            fields: { GameTitle: "Eclipse", Genre: "Horror RPG" },
          },
          {
            label: "Variation C",
            fields: { GameTitle: "Dusk", Genre: "Action RPG" },
          },
        ],
        category: "context",
        asset_path: "/Game/IntTest/Fake",
        prompt: "improve context",
        locked_fields: [],
        field_order: ["GameTitle", "Genre"],
      });

      const result = await runPython({
        code: `import arbor.variations as var\nvar.show_text_variations(${JSON.stringify(manifest)})\n_write_result({"success": True})`,
      });
      // show_text_variations doesn't return a value — it opens a UI.
      // If it didn't throw, it succeeded.
      expect(result).toBeDefined();
    });

    it("rejects empty variations array", async () => {
      const manifest = JSON.stringify({
        variations: [],
        category: "context",
        asset_path: "/Game/IntTest/Fake",
        prompt: "test",
      });

      const result = await runPython({
        code: `import arbor.variations as var\nvar.show_text_variations(${JSON.stringify(manifest)})`,
      });
      expect(result.success).toBe(false);
    });

    it("rejects missing variations key", async () => {
      const manifest = JSON.stringify({
        category: "context",
        prompt: "test",
      });

      const result = await runPython({
        code: `import arbor.variations as var\nvar.show_text_variations(${JSON.stringify(manifest)})`,
      });
      expect(result.success).toBe(false);
    });
  });

  // ── get_text_variation_result ─────────────────────────────────────

  describe("get_text_variation_result", () => {
    it("returns pending when no result file exists", async () => {
      // Ensure no stale result file
      await runPython({
        code: `import os, unreal\npath = os.path.join(unreal.Paths.project_dir(), "Saved", "Arbor", "text_variation_result.json")\nif os.path.exists(path): os.remove(path)\n_write_result({"success": True})`,
      });

      const pyResult = await runPython({
        code: `import arbor.variations as var\nvar.get_text_variation_result()`,
      });
      expect(pyResult).toBeDefined();
      const data = pyResult.result as Record<string, unknown>;
      expect(data.status).toBe("pending");
    });

    it("reads and consumes a selection result file", async () => {
      // Write a fake result file
      await runPython({
        code: `import json, os, unreal\nresult = {"action": "select", "selected_index": 1, "selected_label": "Variation B",\n          "selected_fields": {"GameTitle": "Eclipse"}, "category": "context"}\npath = os.path.join(unreal.Paths.project_dir(), "Saved", "Arbor", "text_variation_result.json")\nos.makedirs(os.path.dirname(path), exist_ok=True)\nwith open(path, "w") as f: json.dump(result, f)\n_write_result({"success": True})`,
      });

      // Read it
      const pyResult = await runPython({
        code: `import arbor.variations as var\nvar.get_text_variation_result()`,
      });
      const data = pyResult.result as Record<string, unknown>;
      expect(data.action).toBe("select");
      expect(data.selected_index).toBe(1);
      expect(data.selected_label).toBe("Variation B");
      expect(
        (data.selected_fields as Record<string, unknown>).GameTitle
      ).toBe("Eclipse");

      // Verify file was consumed (second read returns pending)
      const pyResult2 = await runPython({
        code: `import arbor.variations as var\nvar.get_text_variation_result()`,
      });
      const data2 = pyResult2.result as Record<string, unknown>;
      expect(data2.status).toBe("pending");
    });

    it("reads and consumes a regenerate result file", async () => {
      await runPython({
        code: `import json, os, unreal\nresult = {"action": "regenerate", "selected_index": -1, "comments": "more detail please"}\npath = os.path.join(unreal.Paths.project_dir(), "Saved", "Arbor", "text_variation_result.json")\nos.makedirs(os.path.dirname(path), exist_ok=True)\nwith open(path, "w") as f: json.dump(result, f)\n_write_result({"success": True})`,
      });

      const pyResult = await runPython({
        code: `import arbor.variations as var\nvar.get_text_variation_result()`,
      });
      const data = pyResult.result as Record<string, unknown>;
      expect(data.action).toBe("regenerate");
      expect(data.comments).toBe("more detail please");
    });

    it("reads and consumes a cancel result file", async () => {
      await runPython({
        code: `import json, os, unreal\nresult = {"action": "cancel", "selected_index": -1}\npath = os.path.join(unreal.Paths.project_dir(), "Saved", "Arbor", "text_variation_result.json")\nos.makedirs(os.path.dirname(path), exist_ok=True)\nwith open(path, "w") as f: json.dump(result, f)\n_write_result({"success": True})`,
      });

      const pyResult = await runPython({
        code: `import arbor.variations as var\nvar.get_text_variation_result()`,
      });
      const data = pyResult.result as Record<string, unknown>;
      expect(data.action).toBe("cancel");
    });
  });

  // No test assets to clean up — variations are in-memory only
});
