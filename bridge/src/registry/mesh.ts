import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const meshTool: CategoryTool = {
  readOnlyActions: ["get_bounds"],

  description:
    "Static mesh utilities: fix pivot point, fix scale (e.g. Meshy 100x), setup collision, get bounds.",

  actionParams: {
    fix_pivot: {
      summary: "Fix mesh pivot to bottom or center",
      required: ["asset_path"],
      optional: ["pivot_mode"],
    },
    fix_scale: {
      summary: "Apply uniform scale factor (e.g. 100 for Meshy models)",
      required: ["asset_path"],
      optional: ["scale_factor"],
    },
    setup_collision: {
      summary: "Setup collision shape (box, sphere, capsule, convex, complex)",
      required: ["asset_path"],
      optional: ["collision_type"],
    },
    get_bounds: {
      summary: "Get mesh bounding box dimensions",
      required: ["asset_path"],
    },
  },

  schema: {
    asset_path: z.string().optional().describe("Static mesh content path"),
    // fix_pivot
    pivot_mode: z.string().optional().describe("Pivot mode: bottom (default), center"),
    // fix_scale
    scale_factor: z.number().optional().describe("Scale factor (fix_scale). E.g. 100 for Meshy"),
    // setup_collision
    collision_type: z.enum(["box", "sphere", "capsule", "convex", "complex"]).optional()
      .describe("Collision type (setup_collision)"),
  },

  actions: {
    async fix_pivot(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborMeshTools", "FixMeshPivot", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          mode: p.pivot_mode ?? "bottom",
        }),
      });
    },

    async fix_scale(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborMeshTools", "FixMeshScale", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          scale_factor: p.scale_factor ?? 100,
        }),
      });
    },

    async setup_collision(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborMeshTools", "SetupCollision", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          collision_type: p.collision_type ?? "convex",
        }),
      });
    },

    async get_bounds(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("ArborMeshTools", "GetMeshBounds", {
        AssetPath: p.asset_path,
      });
    },
  },
};
