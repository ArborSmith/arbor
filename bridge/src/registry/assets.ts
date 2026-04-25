import { z } from "zod";
import { resolve, basename, extname } from "node:path";
import { access } from "node:fs/promises";
import { callArborJson, executePythonWithResult } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

const TYPE_SHORTCUTS: Record<string, string> = {
  mesh: "StaticMesh,SkeletalMesh",
  material: "Material,MaterialInstance",
  blueprint: "Blueprint",
  vfx: "NiagaraSystem,NiagaraEmitter",
  sound: "SoundWave,SoundCue",
  texture: "Texture2D",
};

export const assetsTool: CategoryTool = {
  readOnlyActions: ["find", "scan", "stats"],

  description:
    "Asset management: search project assets, scan/rescan registry, get stats, import GLB/FBX files.",

  actionParams: {
    find: {
      summary: "Search project assets by query and type",
      required: ["query"],
      optional: ["type_filter", "limit"],
    },
    scan: {
      summary: "Scan and rescan asset registry",
    },
    stats: {
      summary: "Get asset registry statistics",
    },
    import: {
      summary: "Import GLB/GLTF/FBX file into project",
      required: ["file_path"],
      optional: ["content_path", "asset_name", "auto_fix_pivot", "scale_factor", "auto_scale_meshy"],
    },
  },

  schema: {
    // find
    query: z.string().optional().describe("Search terms (find)"),
    type_filter: z.string().optional().describe(
      "Type filter: 'mesh', 'material', 'blueprint', 'vfx', 'sound', 'texture', or exact UE5 types"
    ),
    limit: z.number().optional().describe("Max results (find). Default 10"),
    // import
    file_path: z.string().optional().describe("Absolute path to GLB/GLTF/FBX file (import)"),
    content_path: z.string().optional().describe("UE5 content destination. Default: /Game/GeneratedAssets"),
    asset_name: z.string().optional().describe("Imported asset name (import)"),
    auto_fix_pivot: z.boolean().optional().describe("Fix mesh pivot to bottom (import)"),
    scale_factor: z.number().optional().describe("Uniform scale factor (import)"),
    auto_scale_meshy: z.boolean().optional().describe("Auto 100x scale for Meshy models (import). Default true"),
  },

  actions: {
    async find(p) {
      if (!p.query) throw new Error("query required");
      let typeFilter = (p.type_filter as string) ?? "";
      if (typeFilter) {
        const lower = typeFilter.toLowerCase().trim();
        if (TYPE_SHORTCUTS[lower]) typeFilter = TYPE_SHORTCUTS[lower];
      }
      return callArborJson("ArborAssetSearch", "FindAsset", {
        Query: p.query, TypeFilter: typeFilter, Limit: (p.limit as number) ?? 10,
      });
    },

    async scan() {
      return callArborJson("ArborAssetSearch", "ScanProject", {});
    },

    async stats() {
      return callArborJson("ArborAssetSearch", "GetRegistryStats", {});
    },

    async import(p) {
      if (!p.file_path) throw new Error("file_path required");
      const filePath = resolve(p.file_path as string);
      const contentPath = (p.content_path as string) || "/Game/GeneratedAssets";
      const ext = extname(filePath);
      const assetName = (p.asset_name as string) || basename(filePath, ext).replace(/[^a-zA-Z0-9_]/g, "_");

      try { await access(filePath); } catch { throw new Error(`File not found: ${filePath}`); }

      const supported = [".glb", ".gltf", ".fbx"];
      if (!supported.includes(ext.toLowerCase())) {
        throw new Error(`Unsupported format: ${ext}. Supported: ${supported.join(", ")}`);
      }

      const autoScale = (p.auto_scale_meshy as boolean) !== false;
      const effectiveScale = (p.scale_factor as number) ?? (autoScale ? 100.0 : 1.0);

      const args = [filePath, contentPath, assetName];
      if (p.auto_fix_pivot) args.push("--auto-fix-pivot");
      if (effectiveScale !== 1.0) args.push("--scale-factor", String(effectiveScale));

      const result = await executePythonWithResult<{
        success: boolean; imported_paths?: string[]; asset_path?: string;
        asset_name?: string; error?: string;
        pivot_fix?: Record<string, unknown>; pivot_fix_warning?: string;
        scale_fix?: Record<string, unknown>; scale_fix_warning?: string;
      }>("import_asset.py", args);

      if (!result.success) throw new Error(result.error || "Import failed");

      const finalName = result.asset_name || assetName;
      const response: Record<string, unknown> = {
        asset_path: result.asset_path || `${contentPath}/${finalName}`,
        asset_name: finalName,
        imported_paths: result.imported_paths || [],
        message: `Imported to ${contentPath}/${finalName}`,
      };
      if (result.pivot_fix) { response.pivot_fix = result.pivot_fix; response.message += " (pivot fixed)"; }
      if (result.scale_fix) { response.scale_fix = result.scale_fix; response.message += ` (scaled ${effectiveScale}x)`; }
      return response;
    },
  },
};
