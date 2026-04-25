import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const materialsTool: CategoryTool = {
  description:
    "Material creation and assignment: create simple/textured/PBR/world-aligned materials, " +
    "create material instances with parameter overrides, assign materials to actors.",

  actionParams: {
    create: {
      summary: "Create simple PBR material with color and properties",
      optional: ["name", "content_path", "color", "metallic", "roughness", "opacity", "emissive_color", "emissive_strength", "two_sided"],
    },
    create_from_textures: {
      summary: "Create material from texture maps (albedo, normal, roughness, etc.)",
      optional: ["name", "content_path", "albedo", "normal", "roughness_map", "metallic_map", "ao", "height_map", "tiling"],
    },
    create_pbr: {
      summary: "Create parameterized PBR material with adjustable properties",
      optional: ["name", "content_path", "color", "metallic", "roughness", "opacity", "emissive_color", "emissive_strength", "two_sided", "tiling"],
    },
    create_instance: {
      summary: "Create material instance with parameter overrides",
      required: ["parent_material"],
      optional: ["name", "content_path", "scalar_params", "vector_params", "texture_params"],
    },
    create_world_aligned: {
      summary: "Create world-aligned material for seamless tiling across surfaces",
      optional: ["name", "content_path", "color", "metallic", "roughness", "tiling"],
    },
    assign: {
      summary: "Assign material to actors",
      required: ["actor_names", "material_path"],
      optional: ["slot_index"],
    },
    ensure_pbr_base: {
      summary: "Ensure PBR base material exists in content path",
      optional: ["content_path"],
    },
  },

  schema: {
    name: z.string().optional().describe("Material name"),
    content_path: z.string().optional().describe("Content folder. Default: /Game/Materials"),
    // create (simple)
    color: z.object({ r: z.number(), g: z.number(), b: z.number() }).optional().describe("Base color {r,g,b} 0-1"),
    metallic: z.number().optional().describe("Metallic value 0-1"),
    roughness: z.number().optional().describe("Roughness value 0-1"),
    opacity: z.number().optional().describe("Opacity 0-1 (enables translucency)"),
    emissive_color: z.object({ r: z.number(), g: z.number(), b: z.number() }).optional().describe("Emissive color"),
    emissive_strength: z.number().optional().describe("Emissive multiplier"),
    two_sided: z.boolean().optional().describe("Two-sided material"),
    // create_from_textures
    albedo: z.string().optional().describe("Albedo texture path"),
    normal: z.string().optional().describe("Normal map texture path"),
    roughness_map: z.string().optional().describe("Roughness texture path"),
    metallic_map: z.string().optional().describe("Metallic texture path"),
    ao: z.string().optional().describe("AO texture path"),
    height_map: z.string().optional().describe("Height map texture path"),
    tiling: z.number().optional().describe("UV tiling factor"),
    // create_instance
    parent_material: z.string().optional().describe("Parent material path (create_instance)"),
    scalar_params: z.record(z.number()).optional().describe("Scalar param overrides"),
    vector_params: z.record(z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() })).optional()
      .describe("Vector param overrides"),
    texture_params: z.record(z.string()).optional().describe("Texture param overrides (name → texture path)"),
    // assign
    actor_names: z.array(z.string()).optional().describe("Actor names to assign material to"),
    material_path: z.string().optional().describe("Material content path to assign"),
    slot_index: z.number().optional().describe("Material slot index. Default 0"),
  },

  actions: {
    async create(p) {
      return callArborJson("ArborMaterialTools", "CreateMaterial", {
        ParamsJson: JSON.stringify({
          name: p.name, content_path: p.content_path,
          color: p.color, metallic: p.metallic, roughness: p.roughness,
          opacity: p.opacity, emissive_color: p.emissive_color,
          emissive_strength: p.emissive_strength, two_sided: p.two_sided,
        }),
      });
    },

    async create_from_textures(p) {
      return callArborJson("ArborMaterialTools", "CreateMaterialFromTextures", {
        ParamsJson: JSON.stringify({
          name: p.name, content_path: p.content_path,
          albedo: p.albedo, normal: p.normal,
          roughness_map: p.roughness_map, metallic_map: p.metallic_map,
          ao: p.ao, height_map: p.height_map, tiling: p.tiling,
        }),
      });
    },

    async create_pbr(p) {
      return callArborJson("ArborMaterialTools", "CreateParameterizedPBRMaterial", {
        ParamsJson: JSON.stringify({
          name: p.name, content_path: p.content_path,
          color: p.color, metallic: p.metallic, roughness: p.roughness,
          opacity: p.opacity, emissive_color: p.emissive_color,
          emissive_strength: p.emissive_strength, two_sided: p.two_sided,
          tiling: p.tiling,
        }),
      });
    },

    async create_instance(p) {
      if (!p.parent_material) throw new Error("parent_material required");
      return callArborJson("ArborMaterialTools", "CreateMaterialInstance", {
        ParamsJson: JSON.stringify({
          name: p.name, content_path: p.content_path,
          parent_material: p.parent_material,
          scalar_params: p.scalar_params, vector_params: p.vector_params,
          texture_params: p.texture_params,
        }),
      });
    },

    async create_world_aligned(p) {
      return callArborJson("ArborMaterialTools", "CreateWorldAlignedMaterial", {
        ParamsJson: JSON.stringify({
          name: p.name, content_path: p.content_path,
          color: p.color, metallic: p.metallic, roughness: p.roughness,
          tiling: p.tiling,
        }),
      });
    },

    async assign(p) {
      if (!p.actor_names || !p.material_path) throw new Error("actor_names, material_path required");
      return callArborJson("ArborMaterialTools", "AssignMaterial", {
        ParamsJson: JSON.stringify({
          actor_names: p.actor_names, material_path: p.material_path,
          slot_index: p.slot_index ?? 0,
        }),
      });
    },

    async ensure_pbr_base(p) {
      return callArborJson("ArborMaterialTools", "EnsurePBRBaseMaterial", {
        ContentPath: (p.content_path as string) ?? "/Game/Materials",
      });
    },
  },
};
