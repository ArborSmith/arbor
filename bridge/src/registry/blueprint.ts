import { z } from "zod";
import { callArbor, callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const blueprintTool: CategoryTool = {
  description:
    "Blueprint editing: query, add/remove event graph nodes, set pin defaults, connect/disconnect pins, compile, " +
    "add/remove/set components, create Character/GameMode/AIController blueprints, setup AnimGraph, " +
    "discover available node/component/function types.",

  actionParams: {
    query: {
      summary: "Query blueprint structure and event graph",
      required: ["asset_path"],
    },
    add_node: {
      summary: "Add an event graph node to a blueprint",
      required: ["asset_path", "node_spec"],
    },
    remove_node: {
      summary: "Remove an event graph node by GUID",
      required: ["asset_path", "node_guid"],
    },
    set_pin: {
      summary: "Set default value for a node pin",
      required: ["asset_path", "node_guid", "pin_name", "pin_value"],
    },
    connect: {
      summary: "Connect two pins between nodes",
      required: ["asset_path", "from_guid", "from_pin", "to_guid", "to_pin"],
    },
    disconnect: {
      summary: "Disconnect a pin from a node",
      required: ["asset_path", "node_guid", "pin_name"],
    },
    compile: {
      summary: "Compile and save blueprint",
      required: ["asset_path"],
    },
    get_node_pins: {
      summary: "Get pins for a node by GUID or by spec",
      optional: ["node_guid", "asset_path", "node_spec"],
    },
    add_component: {
      summary: "Add component to blueprint SCS",
      required: ["asset_path", "component_spec"],
    },
    remove_component: {
      summary: "Remove component from blueprint SCS",
      required: ["asset_path", "component_name"],
    },
    set_component_property: {
      summary: "Set properties on a blueprint component",
      required: ["asset_path", "component_name", "property_spec"],
    },
    set_component_transform: {
      summary: "Set relative transform on a component",
      required: ["asset_path", "component_name"],
      optional: ["location", "rotation", "scale"],
    },
    create_character: {
      summary: "Create Character blueprint with optional mesh and anim BP",
      required: ["name"],
      optional: ["content_path", "mesh_path", "anim_bp_path", "ai_controller_path", "auto_possess", "variables"],
    },
    create_game_mode: {
      summary: "Create GameMode blueprint with optional pawn and controller classes",
      required: ["name"],
      optional: ["content_path", "default_pawn_class", "player_controller_class"],
    },
    create_ai_controller: {
      summary: "Create AIController blueprint with optional BT and perception",
      required: ["name"],
      optional: ["content_path", "behavior_tree_path", "blackboard_path", "perception_config"],
    },
    setup_anim: {
      summary: "Setup locomotion AnimGraph with blendspace",
      required: ["asset_path", "blendspace_path"],
      optional: ["variable_name", "variable_axis", "variable_bindings"],
    },
    query_anim: {
      summary: "Query AnimGraph structure",
      required: ["asset_path"],
    },
    list_node_types: {
      summary: "List available blueprint node types",
      optional: ["filter"],
    },
    list_functions: {
      summary: "List functions for a class",
      required: ["class_name"],
    },
    list_component_types: {
      summary: "List available component types",
      optional: ["filter"],
    },
  },

  readOnlyActions: ["query", "get_node_pins", "query_anim", "list_node_types", "list_functions", "list_component_types"],

  schema: {
    asset_path: z.string().optional().describe("Content path to the Blueprint asset"),
    // Event graph node operations
    node_spec: z.record(z.unknown()).optional().describe("Node specification JSON (add_node, get_node_pins)"),
    node_guid: z.string().optional().describe("Node GUID (remove_node, set_pin, disconnect, get_node_pins)"),
    pin_name: z.string().optional().describe("Pin name (set_pin, disconnect)"),
    pin_value: z.string().optional().describe("Default value string (set_pin)"),
    from_guid: z.string().optional().describe("Source node GUID (connect)"),
    from_pin: z.string().optional().describe("Source pin name (connect)"),
    to_guid: z.string().optional().describe("Target node GUID (connect)"),
    to_pin: z.string().optional().describe("Target pin name (connect)"),
    // Component operations
    component_spec: z.record(z.unknown()).optional().describe("Component JSON (add_component): {name, type, properties}"),
    component_name: z.string().optional().describe("Component name (remove_component, set_component_property, set_component_transform)"),
    property_spec: z.record(z.unknown()).optional().describe("Property JSON (set_component_property)"),
    // Transform operations
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional()
      .describe("Relative location {x,y,z} in cm (set_component_transform)"),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional()
      .describe("Relative rotation {pitch,yaw,roll} in degrees (set_component_transform)"),
    scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional()
      .describe("Relative scale {x,y,z} (set_component_transform)"),
    // Create character/game_mode/ai_controller
    name: z.string().optional().describe("Blueprint name (create_character/game_mode/ai_controller)"),
    content_path: z.string().optional().describe("Content folder. Default: /Game/Blueprints"),
    mesh_path: z.string().optional().describe("SkeletalMesh path (create_character)"),
    anim_bp_path: z.string().optional().describe("AnimBlueprint path (create_character)"),
    ai_controller_path: z.string().optional().describe("AI Controller path (create_character)"),
    auto_possess: z.string().optional().describe("Auto-possess mode (create_character)"),
    variables: z.array(z.object({
      name: z.string(), type: z.string(), default: z.unknown().optional(),
    })).optional().describe("Blueprint variables (create_character)"),
    default_pawn_class: z.string().optional().describe("Default pawn class (create_game_mode)"),
    player_controller_class: z.string().optional().describe("Player controller class (create_game_mode)"),
    behavior_tree_path: z.string().optional().describe("BT path (create_ai_controller)"),
    blackboard_path: z.string().optional().describe("BB path (create_ai_controller)"),
    perception_config: z.array(z.object({
      sense: z.string(), dominant: z.boolean().optional(), params: z.record(z.unknown()).optional(),
    })).optional().describe("Perception senses (create_ai_controller)"),
    // Setup anim
    blendspace_path: z.string().optional().describe("BlendSpace path (setup_anim)"),
    variable_name: z.string().optional().describe("Variable name for axis binding (setup_anim)"),
    variable_axis: z.string().optional().describe("BlendSpace axis to bind (setup_anim). Default 'X'"),
    variable_bindings: z.array(z.object({
      variable_name: z.string(),
      variable_axis: z.string().optional(),
    })).optional().describe(
      "Array of {variable_name, variable_axis} bindings for 2D BlendSpaces (setup_anim). " +
      "Use instead of variable_name/variable_axis for multi-axis binding."
    ),
    // Discovery
    filter: z.string().optional().describe("Substring filter for class names (list_node_types, list_component_types)"),
    class_name: z.string().optional().describe("Class name for introspection (list_functions)"),
  },

  actions: {
    async query(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("BlueprintBuilder", "QueryBlueprint", { AssetPath: p.asset_path });
    },

    async add_node(p) {
      if (!p.asset_path || !p.node_spec) throw new Error("asset_path, node_spec required");
      return callArborJson("BlueprintBuilder", "AddEventGraphNode", {
        AssetPath: p.asset_path,
        NodeJsonString: JSON.stringify(p.node_spec),
      });
    },

    async remove_node(p) {
      if (!p.asset_path || !p.node_guid) throw new Error("asset_path, node_guid required");
      return callArborJson("BlueprintBuilder", "RemoveEventGraphNode", {
        AssetPath: p.asset_path, NodeGuidString: p.node_guid,
      });
    },

    async set_pin(p) {
      if (!p.asset_path || !p.node_guid || !p.pin_name || p.pin_value === undefined)
        throw new Error("asset_path, node_guid, pin_name, pin_value required");
      return callArborJson("BlueprintBuilder", "SetPinDefault", {
        AssetPath: p.asset_path, NodeGuidString: p.node_guid,
        PinName: p.pin_name, DefaultValue: p.pin_value,
      });
    },

    async connect(p) {
      if (!p.asset_path || !p.from_guid || !p.from_pin || !p.to_guid || !p.to_pin)
        throw new Error("asset_path, from_guid, from_pin, to_guid, to_pin required");
      return callArborJson("BlueprintBuilder", "ConnectPins", {
        AssetPath: p.asset_path,
        FromNodeGuid: p.from_guid, FromPinName: p.from_pin,
        ToNodeGuid: p.to_guid, ToPinName: p.to_pin,
      });
    },

    async disconnect(p) {
      if (!p.asset_path || !p.node_guid || !p.pin_name)
        throw new Error("asset_path, node_guid, pin_name required");
      return callArborJson("BlueprintBuilder", "DisconnectPin", {
        AssetPath: p.asset_path, NodeGuidString: p.node_guid, PinName: p.pin_name,
      });
    },

    async compile(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("BlueprintBuilder", "CompileAndSaveBlueprint", {
        AssetPath: p.asset_path,
      });
    },

    async get_node_pins(p) {
      // Support lookup by node_guid on an existing blueprint
      if (p.node_guid) {
        if (!p.asset_path) throw new Error("asset_path required when using node_guid");
        // Search event graph first
        const bp = await callArborJson("BlueprintBuilder", "QueryBlueprint", { AssetPath: p.asset_path }) as {
          success?: boolean; event_graph?: { nodes?: Array<{ guid?: string; pins?: unknown[] }> };
        };
        if (!bp?.success) throw new Error("Failed to query blueprint");
        let node = bp.event_graph?.nodes?.find((n) => n.guid === p.node_guid);
        // If not found, search AnimGraph (for AnimBlueprints)
        if (!node) {
          try {
            const animBp = await callArborJson("AnimBlueprintBuilder", "QueryAnimGraph", {
              AssetPath: p.asset_path,
            }) as { success?: boolean; anim_graph?: { nodes?: Array<{ guid?: string; pins?: unknown[] }> } };
            if (animBp?.success) {
              node = animBp.anim_graph?.nodes?.find((n) => n.guid === p.node_guid);
            }
          } catch { /* not an AnimBP, ignore */ }
        }
        if (!node) throw new Error(`Node ${p.node_guid} not found in blueprint`);
        return { success: true, node_guid: p.node_guid, pins: node.pins ?? [] };
      }
      if (!p.node_spec) throw new Error("node_spec or node_guid required");
      return callArborJson("BlueprintBuilder", "GetNodePins", {
        NodeJsonString: JSON.stringify(p.node_spec),
        ContextAssetPath: (p.asset_path as string) ?? "",
      });
    },

    async add_component(p) {
      if (!p.asset_path || !p.component_spec) throw new Error("asset_path, component_spec required");
      return callArborJson("BlueprintBuilder", "AddSCSComponent", {
        AssetPath: p.asset_path,
        ComponentJsonString: JSON.stringify(p.component_spec),
      });
    },

    async remove_component(p) {
      if (!p.asset_path || !p.component_name) throw new Error("asset_path, component_name required");
      return callArborJson("BlueprintBuilder", "RemoveSCSComponent", {
        AssetPath: p.asset_path, ComponentName: p.component_name,
      });
    },

    async set_component_property(p) {
      if (!p.asset_path || !p.component_name || !p.property_spec)
        throw new Error("asset_path, component_name, property_spec required");
      return callArborJson("BlueprintBuilder", "SetComponentProperty", {
        AssetPath: p.asset_path, ComponentName: p.component_name,
        PropertyJsonString: JSON.stringify(p.property_spec),
      });
    },

    async set_component_transform(p) {
      if (!p.asset_path || !p.component_name)
        throw new Error("asset_path, component_name required");
      const loc = p.location as { x: number; y: number; z: number } | undefined;
      const rot = p.rotation as { pitch: number; yaw: number; roll: number } | undefined;
      const sc = p.scale as { x: number; y: number; z: number } | undefined;
      if (!loc && !rot && !sc)
        throw new Error("At least one of location, rotation, or scale required");
      const props: Record<string, unknown> = {};
      if (loc) props.RelativeLocation = { X: loc.x, Y: loc.y, Z: loc.z };
      if (rot) props.RelativeRotation = { Pitch: rot.pitch, Yaw: rot.yaw, Roll: rot.roll };
      if (sc) props.RelativeScale3D = { X: sc.x, Y: sc.y, Z: sc.z };
      return callArborJson("BlueprintBuilder", "SetComponentProperty", {
        AssetPath: p.asset_path, ComponentName: p.component_name,
        PropertyJsonString: JSON.stringify(props),
      });
    },

    async create_character(p) {
      if (!p.name) throw new Error("name required");
      const contentPath = (p.content_path as string) || "/Game/Blueprints";
      const bpJson: Record<string, unknown> = {
        name: p.name,
        parent_class: "Character",
      };
      if (p.mesh_path) {
        const meshProps: Record<string, unknown> = {
          SkeletalMesh: p.mesh_path,
          RelativeLocation: { X: 0, Y: 0, Z: -88 },
          RelativeRotation: { Pitch: 0, Yaw: -90, Roll: 0 },
        };
        if (p.anim_bp_path) meshProps.AnimClass = p.anim_bp_path;
        bpJson.components = [{ name: "Mesh", type: "SkeletalMeshComponent", properties: meshProps }];
      }
      if (p.variables && (p.variables as unknown[]).length > 0) bpJson.variables = p.variables;
      const defaults: Record<string, unknown> = {};
      if (p.ai_controller_path) defaults.AIControllerClass = p.ai_controller_path;
      if (p.auto_possess) defaults.AutoPossessAI = p.auto_possess;
      if (Object.keys(defaults).length > 0) bpJson.defaults = defaults;

      const result = await callArbor("BlueprintBuilder", "BuildBlueprintFromJSONString", {
        JsonString: JSON.stringify(bpJson), AssetPath: contentPath,
      });
      const assetPath = `${contentPath}/${p.name}`;
      return result.ReturnValue
        ? { success: true, asset_path: assetPath }
        : { success: false, asset_path: assetPath, error: "BlueprintBuilder returned null — check UE5 Output Log" };
    },

    async create_game_mode(p) {
      if (!p.name) throw new Error("name required");
      const contentPath = (p.content_path as string) || "/Game/Blueprints";
      const bpJson: Record<string, unknown> = {
        name: p.name,
        parent_class: "GameModeBase",
      };
      const defaults: Record<string, unknown> = {};
      if (p.default_pawn_class) defaults.DefaultPawnClass = p.default_pawn_class;
      if (p.player_controller_class) defaults.PlayerControllerClass = p.player_controller_class;
      if (Object.keys(defaults).length > 0) bpJson.defaults = defaults;

      const result = await callArbor("BlueprintBuilder", "BuildBlueprintFromJSONString", {
        JsonString: JSON.stringify(bpJson), AssetPath: contentPath,
      });
      const assetPath = `${contentPath}/${p.name}`;
      return result.ReturnValue
        ? { success: true, asset_path: assetPath }
        : { success: false, asset_path: assetPath, error: "BlueprintBuilder returned null" };
    },

    async create_ai_controller(p) {
      if (!p.name) throw new Error("name required");
      const contentPath = (p.content_path as string) || "/Game/Blueprints";
      const bpJson: Record<string, unknown> = {
        name: p.name,
        parent_class: "AIController",
      };

      // Wire BT auto-run via the event graph: Event OnPossess -> RunBehaviorTree(BTAsset).
      // Base AAIController has no DefaultBehaviorTree CDO property, so the data-driven
      // way to run a BT on possess is to override the BP event.
      // blackboard_path is intentionally not used here — RunBehaviorTree initializes the
      // blackboard from the BT asset's BlackboardAsset reference.
      if (p.behavior_tree_path) {
        bpJson.event_graph = {
          nodes: [
            { id: "OnPossess", type: "Event", event: "OnPossess" },
            {
              id: "RunBT",
              type: "CallFunction",
              function: "RunBehaviorTree",
              owner_class: "AIController",
              defaults: { BTAsset: p.behavior_tree_path },
            },
          ],
          connections: [
            { from: "OnPossess", from_pin: "then", to: "RunBT", to_pin: "execute" },
          ],
        };
      }

      if (p.perception_config && (p.perception_config as unknown[]).length > 0) {
        const sensesConfig = (p.perception_config as Array<{sense: string; dominant?: boolean; params?: Record<string, unknown>}>)
          .map(s => ({
            type: s.sense,
            ...(s.dominant != null && { dominant: s.dominant }),
            ...s.params,
          }));
        bpJson.components = [{
          name: "AIPerception",
          type: "AIPerceptionComponent",
          properties: { SensesConfig: sensesConfig },
        }];
      }

      const result = await callArbor("BlueprintBuilder", "BuildBlueprintFromJSONString", {
        JsonString: JSON.stringify(bpJson), AssetPath: contentPath,
      });
      const assetPath = `${contentPath}/${p.name}`;
      return result.ReturnValue
        ? { success: true, asset_path: assetPath }
        : { success: false, asset_path: assetPath, error: "BlueprintBuilder returned null" };
    },

    async setup_anim(p) {
      if (!p.asset_path || !p.blendspace_path)
        throw new Error("asset_path, blendspace_path required");
      const params: Record<string, unknown> = {
        blendspace_path: p.blendspace_path,
      };
      if (p.variable_bindings) {
        params.variable_bindings = p.variable_bindings;
      } else {
        params.variable_name = p.variable_name ?? "Speed";
        params.variable_axis = p.variable_axis ?? "X";
      }
      return callArborJson("AnimBlueprintBuilder", "SetupLocomotionGraph", {
        AssetPath: p.asset_path,
        ParamsJsonString: JSON.stringify(params),
      });
    },

    async query_anim(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("AnimBlueprintBuilder", "QueryAnimGraph", {
        AssetPath: p.asset_path,
      });
    },

    // --- Discovery ---
    async list_node_types(p) {
      return callArborJson("ArborClassDiscovery", "ListBlueprintNodeTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async list_functions(p) {
      if (!p.class_name) throw new Error("class_name required");
      return callArborJson("ArborClassDiscovery", "ListClassFunctions", {
        ClassName: p.class_name,
      });
    },

    async list_component_types(p) {
      return callArborJson("ArborClassDiscovery", "ListComponentTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },
  },
};
