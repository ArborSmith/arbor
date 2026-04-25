import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const foliageTool: CategoryTool = {
  readOnlyActions: ["count"],

  description:
    "Foliage type creation and instance painting: create foliage types from static meshes, " +
    "scatter/paint foliage instances with ground snapping, remove instances, get counts.",

  actionParams: {
    create_type: {
      summary: "Create foliage type from static mesh",
      required: ["mesh_path"],
      optional: ["name", "content_path", "scale_min", "scale_max", "random_yaw", "align_to_normal", "cull_distance"],
    },
    paint: {
      summary: "Paint/scatter foliage instances in circular area",
      required: ["foliage_type_path"],
      optional: ["count", "center_x", "center_y", "radius"],
    },
    remove: {
      summary: "Remove foliage instances (radius=0 removes all)",
      optional: ["foliage_type_path", "remove_radius", "remove_center_x", "remove_center_y"],
    },
    count: {
      summary: "Get total foliage instance counts per type",
    },
  },

  schema: {
    // create_type
    mesh_path: z.string().optional().describe("Static mesh path for foliage type"),
    name: z.string().optional().describe("Foliage type name"),
    content_path: z.string().optional().describe("Content folder for foliage type asset"),
    scale_min: z.number().optional().describe("Min random scale. Default 0.8"),
    scale_max: z.number().optional().describe("Max random scale. Default 1.2"),
    random_yaw: z.boolean().optional().describe("Random yaw rotation. Default true"),
    align_to_normal: z.boolean().optional().describe("Align to surface normal. Default true"),
    cull_distance: z.number().optional().describe("Max draw distance"),
    // paint
    foliage_type_path: z.string().optional().describe("Foliage type asset path (paint, remove)"),
    count: z.number().optional().describe("Number of instances to paint. Default 100"),
    center_x: z.number().optional().describe("Paint center X"),
    center_y: z.number().optional().describe("Paint center Y"),
    radius: z.number().optional().describe("Paint radius in cm. Default 1000"),
    // remove
    remove_radius: z.number().optional().describe("Remove radius (0 = all). Default 0"),
    remove_center_x: z.number().optional().describe("Remove center X"),
    remove_center_y: z.number().optional().describe("Remove center Y"),
  },

  actions: {
    async create_type(p) {
      if (!p.mesh_path) throw new Error("mesh_path required");
      return callArborJson("ArborFoliageTools", "CreateFoliageType", {
        ParamsJson: JSON.stringify({
          mesh_path: p.mesh_path, name: p.name, content_path: p.content_path,
          scale_min: p.scale_min, scale_max: p.scale_max,
          random_yaw: p.random_yaw, align_to_normal: p.align_to_normal,
          cull_distance: p.cull_distance,
        }),
      });
    },

    async paint(p) {
      if (!p.foliage_type_path) throw new Error("foliage_type_path required");
      return callArborJson("ArborFoliageTools", "PaintFoliageInstances", {
        ParamsJson: JSON.stringify({
          foliage_type_path: p.foliage_type_path,
          count: p.count ?? 100,
          center_x: p.center_x ?? 0, center_y: p.center_y ?? 0,
          radius: p.radius ?? 1000,
        }),
      });
    },

    async remove(p) {
      return callArborJson("ArborFoliageTools", "RemoveFoliageInstances", {
        ParamsJson: JSON.stringify({
          foliage_type_path: p.foliage_type_path,
          radius: p.remove_radius ?? 0,
          center_x: p.remove_center_x ?? 0, center_y: p.remove_center_y ?? 0,
        }),
      });
    },

    async count() {
      return callArborJson("ArborFoliageTools", "GetFoliageCount", {});
    },
  },
};
