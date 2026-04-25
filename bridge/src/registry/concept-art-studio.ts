import { z } from "zod";
import { runPython } from "../tools/core/run-python.js";
import type { CategoryTool } from "./types.js";

/**
 * Escape a string for embedding in Python string literals.
 * Uses triple-quoted strings to handle newlines and quotes safely.
 */
function pyStr(s: string): string {
  // Escape backslashes first, then triple quotes
  return s.replace(/\\/g, "\\\\").replace(/"""/g, '\\"\\"\\"');
}

export const conceptArtStudioTool: CategoryTool = {
  readOnlyActions: ["get_approval", "get_selection", "get_state"],

  description:
    "Concept Art Studio: unified pipeline for generating, reviewing, and importing concept art via a persistent UE5 editor window. " +
    "Workflow: open → set_prompt → poll get_approval → set_results → poll get_selection → import_selection.",

  actionParams: {
    open: {
      summary:
        "Open the Concept Art Studio pre-filled with a codex entry's context",
      required: ["codex_asset_path"],
    },
    set_prompt: {
      summary:
        "Send Claude's generation prompt to the studio for user review/editing",
      required: ["prompt"],
    },
    get_approval: {
      summary:
        "Poll the studio for user's prompt approval (action='approve_prompt') or regeneration request (action='regenerate')",
    },
    set_results: {
      summary: "Send generated images to the studio results grid",
      required: ["images"],
    },
    get_selection: {
      summary:
        "Poll the studio for user's image selection (action='select') or regeneration request (action='regenerate')",
    },
    get_state: {
      summary: "Read the current studio state (step, action, all fields)",
    },
    import_selection: {
      summary:
        "Import the selected image and set it as concept art on the codex entry",
      required: ["codex_asset_path", "image_path", "image_name"],
    },
  },

  schema: {
    codex_asset_path: z
      .string()
      .optional()
      .describe("UE5 asset path of the codex entry"),
    prompt: z
      .string()
      .optional()
      .describe("Image generation prompt for user review"),
    images: z
      .union([
        z.string(),
        z.array(
          z.object({
            path: z.string(),
            label: z.string(),
          })
        ),
      ])
      .optional()
      .describe("Generated images array or JSON string"),
    image_path: z
      .string()
      .optional()
      .describe("Disk path of the image to import"),
    image_name: z
      .string()
      .optional()
      .describe("Asset name for the imported texture"),
  },

  actions: {
    async open(p) {
      if (!p.codex_asset_path) throw new Error("codex_asset_path required");
      const path = pyStr(p.codex_asset_path as string);
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.open_studio("""${path}""")`,
        })
      ).result;
    },

    async set_prompt(p) {
      if (!p.prompt) throw new Error("prompt required");
      const prompt = pyStr(p.prompt as string);
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.set_prompt("""${prompt}""")`,
        })
      ).result;
    },

    async get_approval() {
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.get_approval()`,
        })
      ).result;
    },

    async set_results(p) {
      if (!p.images) throw new Error("images required");
      const imagesStr =
        typeof p.images === "string" ? p.images : JSON.stringify(p.images);
      const escaped = pyStr(imagesStr);
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.set_results("""${escaped}""")`,
        })
      ).result;
    },

    async get_selection() {
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.get_selection()`,
        })
      ).result;
    },

    async get_state() {
      return (
        await runPython({
          code: `import arbor.concept_art_studio as studio\nstudio.get_state()`,
        })
      ).result;
    },

    async import_selection(p) {
      if (!p.codex_asset_path) throw new Error("codex_asset_path required");
      if (!p.image_path) throw new Error("image_path required");
      if (!p.image_name) throw new Error("image_name required");
      const assetPath = pyStr(p.codex_asset_path as string);
      const imagePath = pyStr(p.image_path as string);
      const imageName = pyStr(p.image_name as string);
      return (
        await runPython({
          code: `import arbor.concept_art as ca\nca.import_concept_art("""${imagePath}""", """${assetPath}""", """${imageName}""")`,
        })
      ).result;
    },
  },
};
