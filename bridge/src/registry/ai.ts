import { z } from "zod";
import { callArbor, callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const aiTool: CategoryTool = {
  description:
    "AI system editing: create/query/edit Behavior Trees and EQS queries, discover available node types.",

  actionParams: {
    create_bt: {
      summary: "Create a Behavior Tree with optional blackboard",
      required: ["name", "tree"],
      optional: ["content_path", "blackboard_keys"],
    },
    query_bt: {
      summary: "Query a Behavior Tree structure and metadata",
      required: ["asset_path"],
    },
    add_bt_node: {
      summary: "Add a node to a Behavior Tree at parent path",
      required: ["asset_path", "parent_path", "node_spec"],
      optional: ["child_index"],
    },
    remove_bt_node: {
      summary: "Remove a BT node by path",
      required: ["asset_path", "node_path"],
    },
    set_bt_params: {
      summary: "Set parameters on a BT node",
      required: ["asset_path", "node_path", "params"],
    },
    layout_bt: {
      summary: "Auto-layout BT graph nodes",
      required: ["asset_path"],
    },
    create_eqs: {
      summary: "Create an EQS query with generators and tests",
      required: ["name", "generators", "tests"],
      optional: ["content_path"],
    },
    query_eqs: {
      summary: "Query an EQS query structure",
      required: ["asset_path"],
    },
    add_eqs_generator: {
      summary: "Add a generator to an EQS query",
      required: ["asset_path", "generator_spec"],
      optional: ["option_index"],
    },
    remove_eqs_generator: {
      summary: "Remove an EQS generator by option index",
      required: ["asset_path", "option_index"],
    },
    set_eqs_generator_params: {
      summary: "Set parameters on an EQS generator",
      required: ["asset_path", "option_index", "params"],
    },
    add_eqs_test: {
      summary: "Add a test to an EQS query",
      required: ["asset_path", "test_spec"],
      optional: ["option_index", "test_index"],
    },
    remove_eqs_test: {
      summary: "Remove an EQS test by index",
      required: ["asset_path", "option_index", "test_index"],
    },
    set_eqs_test_params: {
      summary: "Set parameters on an EQS test",
      required: ["asset_path", "option_index", "test_index", "params"],
    },
    list_bt_types: {
      summary: "List available BT node types",
      optional: ["bt_category", "filter"],
    },
    list_eqs_generators: {
      summary: "List available EQS generator types",
      optional: ["filter"],
    },
    list_eqs_tests: {
      summary: "List available EQS test types",
      optional: ["filter"],
    },
    get_class_params: {
      summary: "Get properties for a class",
      required: ["class_name"],
      optional: ["base_class"],
    },
  },

  readOnlyActions: ["query_bt", "query_eqs", "list_bt_types", "list_eqs_generators", "list_eqs_tests", "get_class_params"],

  schema: {
    asset_path: z.string().optional().describe("Content path to BT or EQS asset"),
    name: z.string().optional().describe("Asset name (create_bt, create_eqs)"),
    content_path: z.string().optional().describe("Content folder. Default: /Game/AI"),
    // BT creation
    blackboard_keys: z.array(z.object({
      name: z.string(), type: z.string(), base_class: z.string().optional(),
    })).optional().describe("Blackboard key definitions (create_bt)"),
    tree: z.unknown().optional().describe("BT tree structure — root composite with children (create_bt)"),
    // BT granular
    parent_path: z.string().optional().describe("Parent node path (add_bt_node), e.g. '0', '0.1'"),
    child_index: z.number().optional().describe("Child index (add_bt_node), -1 to append"),
    node_spec: z.record(z.unknown()).optional().describe("Node spec JSON (add_bt_node)"),
    node_path: z.string().optional().describe("Node path (remove_bt_node, set_bt_params)"),
    params: z.record(z.unknown()).optional().describe("Parameters (set_bt_params, set_eqs_*_params)"),
    // EQS creation
    generators: z.array(z.object({
      type: z.string(), params: z.record(z.unknown()).optional(),
    })).optional().describe("EQS generators (create_eqs)"),
    tests: z.array(z.object({
      type: z.string(), params: z.record(z.unknown()).optional(),
    })).optional().describe("EQS tests (create_eqs)"),
    // EQS granular
    option_index: z.number().optional().describe("Option index (EQS generator/test operations)"),
    test_index: z.number().optional().describe("Test index (EQS test operations)"),
    generator_spec: z.record(z.unknown()).optional().describe("Generator spec JSON (add_eqs_generator)"),
    test_spec: z.record(z.unknown()).optional().describe("Test spec JSON (add_eqs_test)"),
    // Discovery
    filter: z.string().optional().describe("Substring filter for class names (list_bt_types, list_eqs_*)"),
    bt_category: z.enum(["task", "decorator", "service", "composite"]).optional()
      .describe("BT node category (list_bt_types). Omit for all categories"),
    class_name: z.string().optional().describe("Class name for introspection (get_class_params)"),
    base_class: z.string().optional().describe("Base class constraint (get_class_params)"),
  },

  actions: {
    // --- Behavior Tree ---
    async create_bt(p) {
      if (!p.name || !p.tree) throw new Error("name, tree required");
      const assetPath = (p.content_path as string) || "/Game/AI";
      const baseName = (p.name as string).startsWith("BT_") ? (p.name as string).slice(3) : p.name as string;
      const btName = (p.name as string).startsWith("BT_") ? p.name as string : `BT_${p.name}`;
      const bbName = `BB_${baseName}`;

      const arborJson: Record<string, unknown> = { name: btName, root: p.tree };
      if (p.blackboard_keys && (p.blackboard_keys as unknown[]).length > 0) {
        arborJson.blackboard = { name: bbName, keys: p.blackboard_keys };
      }

      try {
        const result = await callArbor("BehaviorTreeBuilder", "BuildBehaviorTreeFromJSONString", {
          JsonString: JSON.stringify(arborJson), AssetPath: assetPath,
        });
        if (result.ReturnValue) {
          const r: Record<string, unknown> = { success: true, behavior_tree: `${assetPath}/${btName}` };
          if (p.blackboard_keys && (p.blackboard_keys as unknown[]).length > 0) r.blackboard = `${assetPath}/${bbName}`;
          return r;
        }
        return { success: false, behavior_tree: `${assetPath}/${btName}`, error: "BehaviorTreeBuilder returned null" };
      } catch (err) {
        return { success: false, behavior_tree: `${assetPath}/${btName}`, error: err instanceof Error ? err.message : String(err) };
      }
    },

    async query_bt(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("BehaviorTreeBuilder", "QueryBehaviorTree", { AssetPath: p.asset_path });
    },

    async add_bt_node(p) {
      if (!p.asset_path || p.parent_path === undefined || !p.node_spec)
        throw new Error("asset_path, parent_path, node_spec required");
      return callArborJson("BehaviorTreeBuilder", "AddBTNode", {
        AssetPath: p.asset_path, ParentPath: p.parent_path,
        ChildIndex: (p.child_index as number) ?? -1,
        NodeJsonString: JSON.stringify(p.node_spec),
      });
    },

    async remove_bt_node(p) {
      if (!p.asset_path || !p.node_path) throw new Error("asset_path, node_path required");
      return callArborJson("BehaviorTreeBuilder", "RemoveBTNode", {
        AssetPath: p.asset_path, NodePath: p.node_path,
      });
    },

    async set_bt_params(p) {
      if (!p.asset_path || !p.node_path || !p.params)
        throw new Error("asset_path, node_path, params required");
      return callArborJson("BehaviorTreeBuilder", "SetBTNodeParams", {
        AssetPath: p.asset_path, NodePath: p.node_path,
        ParamsJsonString: JSON.stringify(p.params),
      });
    },

    async layout_bt(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("BehaviorTreeBuilder", "LayoutBehaviorTree", { AssetPath: p.asset_path });
    },

    // --- EQS ---
    async create_eqs(p) {
      if (!p.name || !p.generators || !p.tests) throw new Error("name, generators, tests required");
      const assetPath = (p.content_path as string) || "/Game/AI";
      const eqsName = (p.name as string).startsWith("EQS_") ? p.name as string : `EQS_${p.name}`;

      try {
        const result = await callArbor("EQSBuilder", "BuildEQSFromJSONString", {
          JsonString: JSON.stringify({ name: eqsName, generators: p.generators, tests: p.tests }),
          AssetPath: assetPath,
        });
        return result.ReturnValue
          ? { success: true, eqs_query: `${assetPath}/${eqsName}` }
          : { success: false, eqs_query: `${assetPath}/${eqsName}`, error: "EQSBuilder returned null" };
      } catch (err) {
        return { success: false, eqs_query: `${assetPath}/${eqsName}`, error: err instanceof Error ? err.message : String(err) };
      }
    },

    async query_eqs(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("EQSBuilder", "QueryEQS", { AssetPath: p.asset_path });
    },

    async add_eqs_generator(p) {
      if (!p.asset_path || !p.generator_spec) throw new Error("asset_path, generator_spec required");
      return callArborJson("EQSBuilder", "AddEQSGenerator", {
        AssetPath: p.asset_path, OptionIndex: (p.option_index as number) ?? -1,
        GeneratorJsonString: JSON.stringify(p.generator_spec),
      });
    },

    async remove_eqs_generator(p) {
      if (!p.asset_path || p.option_index === undefined) throw new Error("asset_path, option_index required");
      return callArborJson("EQSBuilder", "RemoveEQSGenerator", {
        AssetPath: p.asset_path, OptionIndex: p.option_index,
      });
    },

    async set_eqs_generator_params(p) {
      if (!p.asset_path || p.option_index === undefined || !p.params)
        throw new Error("asset_path, option_index, params required");
      return callArborJson("EQSBuilder", "SetEQSGeneratorParams", {
        AssetPath: p.asset_path, OptionIndex: p.option_index,
        ParamsJsonString: JSON.stringify(p.params),
      });
    },

    async add_eqs_test(p) {
      if (!p.asset_path || !p.test_spec) throw new Error("asset_path, test_spec required");
      return callArborJson("EQSBuilder", "AddEQSTest", {
        AssetPath: p.asset_path, OptionIndex: (p.option_index as number) ?? 0,
        TestIndex: (p.test_index as number) ?? -1,
        TestJsonString: JSON.stringify(p.test_spec),
      });
    },

    async remove_eqs_test(p) {
      if (!p.asset_path || p.option_index === undefined || p.test_index === undefined)
        throw new Error("asset_path, option_index, test_index required");
      return callArborJson("EQSBuilder", "RemoveEQSTest", {
        AssetPath: p.asset_path, OptionIndex: p.option_index, TestIndex: p.test_index,
      });
    },

    async set_eqs_test_params(p) {
      if (!p.asset_path || p.option_index === undefined || p.test_index === undefined || !p.params)
        throw new Error("asset_path, option_index, test_index, params required");
      return callArborJson("EQSBuilder", "SetEQSTestParams", {
        AssetPath: p.asset_path, OptionIndex: p.option_index, TestIndex: p.test_index,
        ParamsJsonString: JSON.stringify(p.params),
      });
    },

    // --- Discovery ---
    async list_bt_types(p) {
      return callArborJson("ArborClassDiscovery", "ListBTNodeTypes", {
        Category: (p.bt_category as string) ?? "",
        Filter: (p.filter as string) ?? "",
      });
    },

    async list_eqs_generators(p) {
      return callArborJson("ArborClassDiscovery", "ListEQSGeneratorTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async list_eqs_tests(p) {
      return callArborJson("ArborClassDiscovery", "ListEQSTestTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async get_class_params(p) {
      if (!p.class_name) throw new Error("class_name required");
      return callArborJson("ArborClassDiscovery", "GetClassProperties", {
        ClassName: p.class_name,
        BaseClassName: (p.base_class as string) ?? "",
      });
    },
  },
};
