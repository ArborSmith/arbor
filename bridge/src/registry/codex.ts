import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const codexTool: CategoryTool = {
  readOnlyActions: [
    "search",
    "list",
    "get",
    "context",
    "tree",
    "get_images",
    "character_query",
    "character_list",
  ],

  description:
    "Game Codex: search, retrieve, create, update, and delete game design data (locations, features, characters). " +
    "Includes structured character creation with archetype, personality profile, backstory, and sample dialogue lines.",

  actionParams: {
    search: {
      summary:
        "Full-text search across all codex entries. Returns matched entries with scores.",
      required: ["query"],
      optional: ["category", "limit", "status"],
    },
    list: {
      summary: "List all entries in a codex category (names and paths)",
      required: ["category"],
      optional: ["status"],
    },
    get: {
      summary: "Get full details of a specific codex entry by asset path",
      required: ["asset_path"],
    },
    context: {
      summary:
        "Get all Game Context assets (title, genre, setting, world description, themes, pillars)",
    },
    create: {
      summary:
        "Create a new codex entry (or update existing if same name/path)",
      required: ["category", "name", "properties"],
      optional: ["content_path", "game_context", "status"],
    },
    update: {
      summary:
        "Update fields on an existing codex entry (partial — only provided fields are changed)",
      required: ["asset_path", "properties"],
      optional: ["status"],
    },
    tree: {
      summary:
        "Get all codex entries that belong to a GameContext, grouped by category",
      required: ["game_context"],
    },
    delete: {
      summary: "Delete a codex entry",
      required: ["asset_path"],
    },
    set_concept_art: {
      summary:
        "Set primary concept art on a codex entry",
      required: ["asset_path", "texture_path"],
      optional: ["prompt"],
    },
    add_gallery_image: {
      summary: "Add an image to a codex entry's gallery",
      required: ["asset_path", "texture_path"],
    },
    remove_gallery_image: {
      summary: "Remove an image from a codex entry's gallery",
      required: ["asset_path", "texture_path"],
    },
    get_images: {
      summary: "Get concept art references for a codex entry",
      required: ["asset_path"],
    },
    character_create: {
      summary:
        "Create a character data asset with structured personality profile, archetype, backstory, and sample dialogue lines. " +
        "Use this instead of the generic `create` action when making characters — it routes through ArborCharacterBuilder for richer field handling.",
      required: ["name"],
      optional: [
        "content_path",
        "character_id",
        "archetype",
        "personality_traits",
        "backstory",
        "personality_profile",
        "dialogue_lines",
        "game_context",
      ],
    },
    character_query: {
      summary: "Query a character asset and return all fields",
      required: ["asset_path"],
    },
    character_update_section: {
      summary:
        "Update a single section of a character (backstory, personality_profile, personality_traits, dialogue_lines)",
      required: ["asset_path", "section", "data"],
    },
    character_list: {
      summary: "List character data assets under a folder",
      optional: ["folder_path"],
    },
    character_reimport: {
      summary: "Re-import a character from an .arbor.json sidecar file",
      required: ["sidecar_path"],
    },
  },

  schema: {
    query: z.string().optional().describe("Search terms (search)"),
    category: z
      .string()
      .optional()
      .describe(
        "Codex category: context, location, feature, character"
      ),
    limit: z
      .number()
      .optional()
      .describe("Max results (search). Default 20"),
    asset_path: z
      .string()
      .optional()
      .describe("Full UE5 asset path (get/update/delete)"),
    name: z
      .string()
      .optional()
      .describe("Display name for the new entry (create)"),
    properties: z
      .union([z.record(z.unknown()), z.string()])
      .optional()
      .describe("UPROPERTY field values as JSON object (create/update)"),
    content_path: z
      .string()
      .optional()
      .describe(
        "Package path prefix, e.g. /Game/WorldBuilding (create). Default per category"
      ),
    game_context: z
      .string()
      .optional()
      .describe("Soft object path to a GameContext asset. On create: auto-derives folder path to {GameContext parent}/{category subfolder} when content_path is not set. On tree: returns all entries referencing this context."),
    texture_path: z
      .string()
      .optional()
      .describe(
        "UE5 content path to a UTexture2D asset (set_concept_art/add_gallery_image)"
      ),
    prompt: z
      .string()
      .optional()
      .describe("Image generation prompt text (set_concept_art)"),
    status: z
      .enum([
        "None",
        "Ideation",
        "Pre-Production",
        "Prototype",
        "Production",
        "Polish",
        "Complete",
        "Cut",
      ])
      .optional()
      .describe(
        "Development status (create/update: sets Status field; search/list: filters by status)"
      ),

    // --- character_* action fields ---
    character_id: z.string().optional().describe("Character ID (auto-generated from name if omitted). character_create action."),
    archetype: z.string().optional().describe("Character archetype (e.g. 'reluctant_hero', 'trickster'). character_create action."),
    personality_traits: z.array(z.string()).optional().describe("List of personality traits. character_create action."),
    backstory: z.string().optional().describe("Character backstory text. character_create action."),
    personality_profile: z.object({
      core_drives: z.string().optional(),
      fears: z.string().optional(),
      contradictions: z.string().optional(),
      social_style: z.string().optional(),
      combat_mindset: z.string().optional(),
    }).optional().describe("Personality profile with core drives, fears, contradictions, social style, combat mindset. character_create action."),
    dialogue_lines: z.array(z.object({
      context: z.string(),
      line: z.string(),
    })).optional().describe("Sample dialogue lines [{context, line}]. character_create action."),
    section: z.enum(["backstory", "personality_profile", "personality_traits", "dialogue_lines"]).optional()
      .describe("Section to update (character_update_section action)"),
    data: z.unknown().optional().describe("Section data — string for backstory, object for personality_profile, array for traits/lines (character_update_section action)"),
    folder_path: z.string().optional().describe("Folder to list characters from. Default: /Game/Characters (character_list action)"),
    sidecar_path: z.string().optional().describe("Disk path to .arbor.json sidecar file (character_reimport action)"),
  },

  actions: {
    async search(p) {
      if (!p.query) throw new Error("query required");
      return callArborJson("ArborCodexSearch", "SearchCodex", {
        Query: p.query,
        Category: (p.category as string) ?? "",
        Limit: (p.limit as number) ?? 20,
        StatusFilter: (p.status as string) ?? "",
      });
    },

    async list(p) {
      if (p.category === undefined || p.category === null) throw new Error("category required");
      return callArborJson("ArborCodexSearch", "ListCodexEntries", {
        Category: p.category,
        StatusFilter: (p.status as string) ?? "",
      });
    },

    async get(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborCodexSearch", "GetCodexEntry", {
        AssetPath: p.asset_path,
      });
    },

    async context() {
      return callArborJson("ArborCodexSearch", "GetGameContext", {});
    },

    async create(p) {
      if (!p.category) throw new Error("category required");
      if (!p.name) throw new Error("name required");
      if (!p.properties) throw new Error("properties required");
      const propsObj = typeof p.properties === "string" ? JSON.parse(p.properties) : { ...p.properties };
      if (p.status) propsObj.Status = p.status;
      const props = JSON.stringify(propsObj);
      return callArborJson("ArborCodexSearch", "CreateCodexEntry", {
        Category: p.category as string,
        AssetName: p.name as string,
        ContentPath: (p.content_path as string) ?? "",
        GameContextPath: (p.game_context as string) ?? "",
        PropertiesJson: props,
      });
    },

    async update(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      if (!p.properties) throw new Error("properties required");
      const propsObj = typeof p.properties === "string" ? JSON.parse(p.properties) : { ...p.properties };
      if (p.status) propsObj.Status = p.status;
      const props = JSON.stringify(propsObj);
      return callArborJson("ArborCodexSearch", "UpdateCodexEntry", {
        AssetPath: p.asset_path as string,
        PropertiesJson: props,
      });
    },

    async tree(p) {
      if (!p.game_context) throw new Error("game_context required");
      return callArborJson("ArborCodexSearch", "GetEntriesForContext", {
        GameContextPath: p.game_context as string,
      });
    },

    async delete(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborCodexSearch", "DeleteCodexEntry", {
        AssetPath: p.asset_path as string,
      });
    },

    async set_concept_art(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      if (!p.texture_path) throw new Error("texture_path required");
      return callArborJson("ArborCodexImageTools", "SetConceptArt", {
        AssetPath: p.asset_path as string,
        TexturePath: p.texture_path as string,
        Prompt: (p.prompt as string) ?? "",
      });
    },

    async add_gallery_image(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      if (!p.texture_path) throw new Error("texture_path required");
      return callArborJson("ArborCodexImageTools", "AddGalleryImage", {
        AssetPath: p.asset_path as string,
        TexturePath: p.texture_path as string,
      });
    },

    async remove_gallery_image(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      if (!p.texture_path) throw new Error("texture_path required");
      return callArborJson("ArborCodexImageTools", "RemoveGalleryImage", {
        AssetPath: p.asset_path as string,
        TexturePath: p.texture_path as string,
      });
    },

    async get_images(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborCodexImageTools", "GetCodexImages", {
        AssetPath: p.asset_path as string,
      });
    },

    // --- character_* actions: structured character CRUD via ArborCharacterBuilder ---

    async character_create(p) {
      if (!p.name) throw new Error("name is required");
      return callArborJson("ArborCharacterBuilder", "CreateCharacterAsset", {
        ParamsJson: JSON.stringify({
          name: p.name,
          content_path: p.content_path,
          character_id: p.character_id,
          archetype: p.archetype,
          personality_traits: p.personality_traits,
          backstory: p.backstory,
          personality_profile: p.personality_profile,
          dialogue_lines: p.dialogue_lines,
          game_context: p.game_context,
        }),
      });
    },

    async character_query(p) {
      if (!p.asset_path) throw new Error("asset_path is required");
      return callArborJson("ArborCharacterBuilder", "QueryCharacterAsset", {
        AssetPath: p.asset_path as string,
      });
    },

    async character_update_section(p) {
      if (!p.asset_path) throw new Error("asset_path is required");
      if (!p.section) throw new Error("section is required");
      return callArborJson("ArborCharacterBuilder", "UpdateCharacterSection", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          section: p.section,
          data: p.data,
        }),
      });
    },

    async character_list(p) {
      return callArborJson("ArborCharacterBuilder", "ListCharacterAssets", {
        FolderPath: (p.folder_path as string) ?? "/Game/Characters",
      });
    },

    async character_reimport(p) {
      if (!p.sidecar_path) throw new Error("sidecar_path is required");
      return callArborJson("ArborCharacterBuilder", "ImportFromSidecar", {
        SidecarPath: p.sidecar_path as string,
      });
    },
  },
};
