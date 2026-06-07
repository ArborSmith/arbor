import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const materialsTool: CategoryTool = {
  description:
    "Material creation, assignment, and graph editing: create simple/textured/PBR/world-aligned " +
    "materials, create material instances with parameter overrides, assign materials to actors, " +
    "and edit material graphs via granular primitives (add/remove/connect expressions) or the " +
    "build_material orchestrator that takes a full JSON spec.",

  readOnlyActions: ["query", "list_expression_types", "get_expression_class_params", "query_function"],

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
    query: {
      summary: "Dump a material's expression graph as JSON (expressions, connections, flags)",
      required: ["material_path"],
    },
    list_expression_types: {
      summary: "List UMaterialExpression subclasses, optional substring filter",
      optional: ["filter"],
    },
    get_expression_class_params: {
      summary: "Reflect on an expression class for its editable properties + defaults",
      required: ["expression_class"],
    },
    add_expression: {
      summary: "Add an expression to a material's graph; returns the sentinel ID",
      required: ["material_path", "expression_class"],
      optional: ["expression_id", "properties", "node_x", "node_y"],
    },
    remove_expression: {
      summary: "Remove an expression by sentinel ID",
      required: ["material_path", "expression_id"],
    },
    set_expression_property: {
      summary: "Set a single property by name on an existing expression",
      required: ["material_path", "expression_id", "property_name", "value"],
    },
    connect_nodes: {
      summary: "Wire one expression's output to another expression's input",
      required: ["material_path", "from_id", "to_id"],
      optional: ["from_output", "to_input"],
    },
    connect_output: {
      summary: "Wire an expression's output to a material output channel (BaseColor, Normal, ..., or FrontMaterial on 5.7+)",
      required: ["material_path", "expression_id", "property"],
      optional: ["from_output"],
    },
    recompile: {
      summary: "Explicit terminal recompile + save after batched edits",
      required: ["material_path"],
    },
    build: {
      summary: "Build or update a complete material from a JSON spec (idempotent). See build_material docs for schema.",
      required: ["spec"],
    },
    query_function: {
      summary: "Dump a Material Function's graph as JSON (expressions, connections, inputs, outputs)",
      required: ["function_path"],
    },
    build_function: {
      summary: "Build or update a complete Material Function from a JSON spec (idempotent). No outputs/flags block; inputs/outputs are FunctionInput/FunctionOutput nodes. See BuildMaterialFunction docs for schema.",
      required: ["spec"],
    },
    layout: {
      summary: "Auto-arrange a material or material function's nodes into a readable left-to-right layout (UE built-in). build/build_function already do this unless the spec sets auto_layout:false.",
      required: ["path"],
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
    // graph editing
    filter: z.string().optional().describe("Substring filter for list_expression_types"),
    expression_class: z.string().optional().describe("UMaterialExpression subclass name (U-prefix optional)"),
    expression_id: z.string().optional().describe("Stable Arbor ID stamped into the expression's Desc field"),
    properties: z.record(z.any()).optional().describe("Property name -> value dict; FLinearColor as [r,g,b,a], textures as asset path string, enums as the enum value name"),
    node_x: z.number().optional().describe("Node X position in graph"),
    node_y: z.number().optional().describe("Node Y position in graph"),
    property_name: z.string().optional().describe("Property name for set_expression_property"),
    value: z.any().optional().describe("Property value for set_expression_property"),
    from_id: z.string().optional().describe("Source expression ID for connect_nodes"),
    to_id: z.string().optional().describe("Target expression ID for connect_nodes"),
    from_output: z.string().optional().describe("Source output pin name (empty = first)"),
    to_input: z.string().optional().describe("Target input pin name (empty = first; warning if target has >1 inputs)"),
    property: z.string().optional().describe("Material output property: BaseColor/Normal/Roughness/Metallic/EmissiveColor/Opacity/AmbientOcclusion/WorldPositionOffset"),
    spec: z.record(z.any()).optional().describe("Full BuildMaterial / BuildMaterialFunction spec — see ArborMaterialGraphTools.h for schema"),
    function_path: z.string().optional().describe("Material Function asset path for query_function (e.g. /Game/Materials/Functions/MF_SDF_Circle)"),
    path: z.string().optional().describe("Material or Material Function asset path for layout (auto-detects type)"),
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

    // ---- Graph editing primitives (Phase 1) ----

    async query(p) {
      if (!p.material_path) throw new Error("material_path required");
      return callArborJson("ArborMaterialGraphTools", "QueryMaterial", {
        MaterialPath: p.material_path as string,
      });
    },

    async list_expression_types(p) {
      return callArborJson("ArborMaterialGraphTools", "ListMaterialExpressionTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async get_expression_class_params(p) {
      if (!p.expression_class) throw new Error("expression_class required");
      return callArborJson("ArborMaterialGraphTools", "GetMaterialExpressionClassParams", {
        ClassName: p.expression_class as string,
      });
    },

    async add_expression(p) {
      if (!p.material_path || !p.expression_class) throw new Error("material_path, expression_class required");
      return callArborJson("ArborMaterialGraphTools", "AddMaterialExpression", {
        ParamsJson: JSON.stringify({
          material_path: p.material_path,
          expression_class: p.expression_class,
          expression_id: p.expression_id,
          properties: p.properties,
          node_x: p.node_x ?? 0,
          node_y: p.node_y ?? 0,
        }),
      });
    },

    async remove_expression(p) {
      if (!p.material_path || !p.expression_id) throw new Error("material_path, expression_id required");
      return callArborJson("ArborMaterialGraphTools", "RemoveMaterialExpressionById", {
        MaterialPath: p.material_path as string,
        ExpressionId: p.expression_id as string,
      });
    },

    async set_expression_property(p) {
      if (!p.material_path || !p.expression_id || !p.property_name)
        throw new Error("material_path, expression_id, property_name required");
      return callArborJson("ArborMaterialGraphTools", "SetMaterialExpressionProperty", {
        ParamsJson: JSON.stringify({
          material_path: p.material_path,
          expression_id: p.expression_id,
          property_name: p.property_name,
          value: p.value,
        }),
      });
    },

    async connect_nodes(p) {
      if (!p.material_path || !p.from_id || !p.to_id)
        throw new Error("material_path, from_id, to_id required");
      return callArborJson("ArborMaterialGraphTools", "ConnectMaterialNodes", {
        ParamsJson: JSON.stringify({
          material_path: p.material_path,
          from_id: p.from_id, to_id: p.to_id,
          from_output: p.from_output ?? "",
          to_input: p.to_input ?? "",
        }),
      });
    },

    async connect_output(p) {
      if (!p.material_path || !p.expression_id || !p.property)
        throw new Error("material_path, expression_id, property required");
      return callArborJson("ArborMaterialGraphTools", "ConnectMaterialOutput", {
        ParamsJson: JSON.stringify({
          material_path: p.material_path,
          expression_id: p.expression_id,
          property: p.property,
          from_output: p.from_output ?? "",
        }),
      });
    },

    async recompile(p) {
      if (!p.material_path) throw new Error("material_path required");
      return callArborJson("ArborMaterialGraphTools", "RecompileMaterialAsset", {
        MaterialPath: p.material_path as string,
      });
    },

    // ---- BuildMaterial orchestrator (Phase 2) ----

    async build(p) {
      if (!p.spec) throw new Error("spec required");
      return callArborJson("ArborMaterialGraphTools", "BuildMaterial", {
        JsonSpec: JSON.stringify(p.spec),
      });
    },

    // ---- Material Function authoring (Phase 7) ----

    async query_function(p) {
      if (!p.function_path) throw new Error("function_path required");
      return callArborJson("ArborMaterialGraphTools", "QueryMaterialFunction", {
        FunctionPath: p.function_path as string,
      });
    },

    async build_function(p) {
      if (!p.spec) throw new Error("spec required");
      return callArborJson("ArborMaterialGraphTools", "BuildMaterialFunction", {
        JsonSpec: JSON.stringify(p.spec),
      });
    },

    // ---- Graph layout ----

    async layout(p) {
      const path = (p.path ?? p.material_path ?? p.function_path) as string | undefined;
      if (!path) throw new Error("path required");
      return callArborJson("ArborMaterialGraphTools", "LayoutMaterial", {
        JsonParams: JSON.stringify({ path }),
      });
    },
  },
};
