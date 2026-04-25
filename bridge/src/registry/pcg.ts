import { z } from "zod";
import { callArbor, callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const pcgTool: CategoryTool = {
  readOnlyActions: ["query", "list_node_types", "get_node_params"],

  description:
    "PCG (Procedural Content Generation) graph creation and editing: create from presets or custom nodes, " +
    "query, add/remove nodes, set params, connect/disconnect pins, execute on actors, discover node types.",

  actionParams: {
    create: {
      summary: "Create PCG graph from preset or custom nodes/connections",
      required: ["name"],
      optional: ["content_path", "preset", "mesh_paths", "density", "scale_min", "scale_max", "random_yaw", "nodes", "connections"],
    },
    query: {
      summary: "Query PCG graph structure and nodes",
      required: ["asset_path"],
    },
    add_node: {
      summary: "Add node to PCG graph",
      required: ["asset_path", "node_spec"],
    },
    remove_node: {
      summary: "Remove PCG node by ID",
      required: ["asset_path", "node_id"],
    },
    set_params: {
      summary: "Set parameters on a PCG node",
      required: ["asset_path", "node_id", "params"],
    },
    connect: {
      summary: "Connect two PCG node pins",
      required: ["asset_path", "from_node_id", "from_pin", "to_node_id", "to_pin"],
    },
    disconnect: {
      summary: "Disconnect a PCG node pin",
      required: ["asset_path", "node_id", "pin_label"],
    },
    execute: {
      summary: "Execute PCG graph on an actor",
      required: ["asset_path", "actor_label"],
    },
    add_component: {
      summary: "Add PCG component to an actor",
      required: ["asset_path", "actor_label"],
    },
    list_node_types: {
      summary: "List available PCG node types",
      optional: ["filter"],
    },
    get_node_params: {
      summary: "Get properties for a PCG node class",
      required: ["class_name"],
    },
  },

  schema: {
    asset_path: z.string().optional().describe("Content path to PCG graph asset"),
    // create
    name: z.string().optional().describe("PCG graph name (create)"),
    content_path: z.string().optional().describe("Content folder. Default: /Game/PCG"),
    preset: z.enum(["foliage_scatter", "rock_scatter", "debris_scatter", "custom"]).optional()
      .describe("Preset type (create). Use 'custom' for manual node/connection spec"),
    mesh_paths: z.array(z.string()).optional().describe("Mesh paths for preset scattering"),
    density: z.number().optional().describe("Scatter density (preset)"),
    scale_min: z.number().optional().describe("Min random scale"),
    scale_max: z.number().optional().describe("Max random scale"),
    random_yaw: z.boolean().optional().describe("Random Y rotation"),
    nodes: z.array(z.record(z.unknown())).optional().describe("Custom nodes array (create with preset='custom')"),
    connections: z.array(z.record(z.unknown())).optional().describe("Custom connections array"),
    // granular
    node_spec: z.record(z.unknown()).optional().describe("Node spec JSON (add_node)"),
    node_id: z.string().optional().describe("Node ID (remove_node, set_params, disconnect)"),
    params: z.record(z.unknown()).optional().describe("Parameters (set_params)"),
    from_node_id: z.string().optional().describe("Source node ID (connect)"),
    from_pin: z.string().optional().describe("Source pin label (connect)"),
    to_node_id: z.string().optional().describe("Target node ID (connect)"),
    to_pin: z.string().optional().describe("Target pin label (connect)"),
    pin_label: z.string().optional().describe("Pin label (disconnect)"),
    // execute
    actor_label: z.string().optional().describe("Actor label (execute, add_component)"),
    // Discovery
    filter: z.string().optional().describe("Substring filter for class names (list_node_types)"),
    class_name: z.string().optional().describe("Class name for introspection (get_node_params)"),
  },

  actions: {
    async create(p) {
      if (!p.name) throw new Error("name required");
      const assetPath = (p.content_path as string) || "/Game/PCG";
      const arborJson: Record<string, unknown> = {
        name: p.name,
        preset: p.preset,
        mesh_paths: p.mesh_paths,
        density: p.density,
        scale_min: p.scale_min,
        scale_max: p.scale_max,
        random_yaw: p.random_yaw,
        nodes: p.nodes,
        connections: p.connections,
      };

      try {
        const result = await callArbor("PCGBuilder", "BuildPCGGraphFromJSONString", {
          JsonString: JSON.stringify(arborJson), AssetPath: assetPath,
        });
        return result.ReturnValue
          ? { success: true, pcg_graph: `${assetPath}/${p.name}` }
          : { success: false, error: "PCGBuilder returned null" };
      } catch (err) {
        return { success: false, error: err instanceof Error ? err.message : String(err) };
      }
    },

    async query(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("PCGBuilder", "QueryPCGGraph", { AssetPath: p.asset_path });
    },

    async add_node(p) {
      if (!p.asset_path || !p.node_spec) throw new Error("asset_path, node_spec required");
      return callArborJson("PCGBuilder", "AddPCGNode", {
        AssetPath: p.asset_path, NodeJsonString: JSON.stringify(p.node_spec),
      });
    },

    async remove_node(p) {
      if (!p.asset_path || !p.node_id) throw new Error("asset_path, node_id required");
      return callArborJson("PCGBuilder", "RemovePCGNode", {
        AssetPath: p.asset_path, NodeIdString: String(p.node_id),
      });
    },

    async set_params(p) {
      if (!p.asset_path || !p.node_id || !p.params)
        throw new Error("asset_path, node_id, params required");
      return callArborJson("PCGBuilder", "SetPCGNodeParams", {
        AssetPath: p.asset_path, NodeIdString: String(p.node_id),
        ParamsJsonString: JSON.stringify(p.params),
      });
    },

    async connect(p) {
      if (!p.asset_path || !p.from_node_id || !p.from_pin || !p.to_node_id || !p.to_pin)
        throw new Error("asset_path, from_node_id, from_pin, to_node_id, to_pin required");
      return callArborJson("PCGBuilder", "ConnectPCGPins", {
        AssetPath: p.asset_path,
        FromNodeId: String(p.from_node_id), FromPinLabel: p.from_pin,
        ToNodeId: String(p.to_node_id), ToPinLabel: p.to_pin,
      });
    },

    async disconnect(p) {
      if (!p.asset_path || !p.node_id || !p.pin_label)
        throw new Error("asset_path, node_id, pin_label required");
      return callArborJson("PCGBuilder", "DisconnectPCGPin", {
        AssetPath: p.asset_path, NodeIdString: String(p.node_id), PinLabel: p.pin_label,
      });
    },

    async execute(p) {
      if (!p.asset_path || !p.actor_label) throw new Error("asset_path, actor_label required");
      return callArborJson("PCGBuilder", "ExecutePCGOnActor", {
        GraphAssetPath: p.asset_path, ActorLabel: p.actor_label,
      });
    },

    async add_component(p) {
      if (!p.asset_path || !p.actor_label) throw new Error("asset_path, actor_label required");
      return callArborJson("PCGBuilder", "AddPCGComponentToActor", {
        ActorLabel: p.actor_label, GraphAssetPath: p.asset_path,
      });
    },

    // --- Discovery ---
    async list_node_types(p) {
      return callArborJson("ArborClassDiscovery", "ListPCGNodeTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async get_node_params(p) {
      if (!p.class_name) throw new Error("class_name required");
      return callArborJson("ArborClassDiscovery", "GetClassProperties", {
        ClassName: p.class_name,
        BaseClassName: "UPCGSettings",
      });
    },
  },
};
