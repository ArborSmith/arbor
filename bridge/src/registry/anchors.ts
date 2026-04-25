import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const anchorsTool: CategoryTool = {
  readOnlyActions: ["get", "list", "find_compatible"],

  description:
    "Anchor metadata extraction and management for environment building. " +
    "Anchors are snap/connection points on meshes (door frames, wall edges, road endpoints) " +
    "used by the environment graph system to place assets without explicit coordinates.",

  actionParams: {
    analyze: {
      summary: "Extract anchors from mesh bounds with optional type hint",
      required: ["asset_path"],
      optional: ["asset_type", "detect_openings"],
    },
    analyze_pack: {
      summary: "Analyze all static meshes in a content folder",
      required: ["folder_path"],
      optional: ["asset_type"],
    },
    get: {
      summary: "Read saved anchor metadata for an asset",
      required: ["asset_path"],
    },
    set: {
      summary: "Write anchor metadata for an asset",
      required: ["asset_path", "metadata"],
    },
    list: {
      summary: "List all assets with anchor metadata in registries",
      optional: ["folder_path"],
    },
    find_compatible: {
      summary: "Find compatible anchor pairs between two assets",
      required: ["from_asset", "to_asset"],
      optional: ["filter_type"],
    },
  },

  schema: {
    asset_path: z.string().optional().describe("Content path to static mesh asset"),
    asset_type: z
      .enum(["building", "road_segment", "prop", "wall", "floor"])
      .optional()
      .describe(
        "Asset type hint for type-specific anchor generation (analyze, analyze_pack). " +
        "building: adds front_door. road_segment: adds start/end/side anchors. " +
        "wall: adds left/right connection points. floor: adds surface + edge anchors"
      ),
    detect_openings: z
      .boolean()
      .optional()
      .describe(
        "Enable opening detection via raycasting (default true for wall asset_type)"
      ),
    metadata: z
      .record(z.unknown())
      .optional()
      .describe("Anchor metadata dict to save (set)"),
    folder_path: z
      .string()
      .optional()
      .describe("Content folder path (analyze_pack, list)"),
    from_asset: z
      .string()
      .optional()
      .describe("Source asset content path (find_compatible)"),
    to_asset: z
      .string()
      .optional()
      .describe("Target asset content path (find_compatible)"),
    filter_type: z
      .string()
      .optional()
      .describe("Filter to specific anchor type (find_compatible)"),
  },

  actions: {
    async analyze(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborAnchorAnalyzer", "AnalyzeMesh", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          asset_type: p.asset_type,
          detect_openings: p.detect_openings,
        }),
      });
    },

    async analyze_pack(p) {
      if (!p.folder_path) throw new Error("folder_path required");
      return callArborJson("ArborAnchorAnalyzer", "AnalyzePack", {
        ParamsJson: JSON.stringify({
          folder_path: p.folder_path,
          asset_type: p.asset_type,
        }),
      });
    },

    async get(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborAnchorAnalyzer", "GetAnchorMetadata", {
        AssetPath: p.asset_path,
      });
    },

    async set(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      if (!p.metadata) throw new Error("metadata required");
      return callArborJson("ArborAnchorAnalyzer", "SetAnchorMetadata", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          metadata: p.metadata,
        }),
      });
    },

    async list(p) {
      return callArborJson("ArborAnchorAnalyzer", "ListAnalyzedAssets", {
        FolderPath: (p.folder_path as string) || "",
      });
    },

    async find_compatible(p) {
      if (!p.from_asset) throw new Error("from_asset required");
      if (!p.to_asset) throw new Error("to_asset required");
      return callArborJson("ArborAnchorAnalyzer", "FindCompatibleAnchors", {
        ParamsJson: JSON.stringify({
          from_asset: p.from_asset,
          to_asset: p.to_asset,
          filter_type: p.filter_type,
        }),
      });
    },
  },
};
