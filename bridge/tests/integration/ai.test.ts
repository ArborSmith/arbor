import { describe, it, expect, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { aiTool } from "../../src/registry/ai.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("AI — Behavior Trees & EQS", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── BT Discovery ──────────────────────────────────────────────────

  describe("BT Discovery", () => {
    it("list_bt_types returns an array", async () => {
      const result = (await aiTool.actions.list_bt_types({})) as {
        types?: unknown[];
      };
      expect(result).toBeDefined();
      expect(result.types ?? result).toBeDefined();
    });

    it("list_bt_types filters by category", async () => {
      const result = (await aiTool.actions.list_bt_types({
        bt_category: "task",
      })) as { types?: unknown[] };
      expect(result).toBeDefined();
    });

    it("get_class_params returns properties for BTTask_Wait", async () => {
      const result = (await aiTool.actions.get_class_params({
        class_name: "BTTask_Wait",
      })) as { properties?: unknown[] };
      expect(result).toBeDefined();
    });
  });

  // ── EQS Discovery ─────────────────────────────────────────────────

  describe("EQS Discovery", () => {
    it("list_eqs_generators returns an array", async () => {
      const result = (await aiTool.actions.list_eqs_generators({})) as {
        types?: unknown[];
      };
      expect(result).toBeDefined();
    });

    it("list_eqs_tests returns an array", async () => {
      const result = (await aiTool.actions.list_eqs_tests({})) as {
        types?: unknown[];
      };
      expect(result).toBeDefined();
    });
  });

  // ── BT Create ─────────────────────────────────────────────────────

  describe("BT Create", () => {
    it("creates a BT with Selector root and Wait child", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait" }],
        },
      })) as { success: boolean; behavior_tree?: string };
      expect(result.success).toBe(true);
      expect(result.behavior_tree).toContain(CONTENT_PATH);
    });

    it("creates a BT with blackboard keys", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        blackboard_keys: [
          { name: "TargetActor", type: "Object", base_class: "Actor" },
          { name: "PatrolIndex", type: "Int" },
        ],
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait" }],
        },
      })) as { success: boolean; behavior_tree?: string; blackboard?: string };
      expect(result.success).toBe(true);
      expect(result.blackboard).toBeDefined();
      expect(result.blackboard).toContain(CONTENT_PATH);
    });
  });

  // ── BT Update In Place (Issue #75) ──────────────────────────────

  describe("BT Update In Place", () => {
    it("updates blackboard reference when rebuilding an existing BT", async () => {
      const name = uniqueName();

      // 1. Create BT with initial blackboard keys
      const create1 = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        blackboard_keys: [
          { name: "KeyA", type: "Bool" },
          { name: "KeyB", type: "Int" },
        ],
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait" }],
        },
      })) as { success: boolean; behavior_tree: string; blackboard?: string };
      expect(create1.success).toBe(true);
      expect(create1.blackboard).toBeDefined();

      // 2. Query to confirm initial state
      // Note: UE5 auto-adds a "SelfActor" key to every blackboard
      const query1 = (await aiTool.actions.query_bt({
        asset_path: create1.behavior_tree,
      })) as { blackboard?: { name: string; keys: { name: string; type: string }[] } };
      expect(query1.blackboard).toBeDefined();
      const keyNames1 = query1.blackboard!.keys.map((k) => k.name);
      expect(keyNames1).toContain("KeyA");
      expect(keyNames1).toContain("KeyB");

      // 3. Recreate the SAME BT with DIFFERENT blackboard keys
      const create2 = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        blackboard_keys: [
          { name: "KeyX", type: "Vector" },
          { name: "KeyY", type: "Float" },
          { name: "KeyZ", type: "Object", base_class: "Actor" },
        ],
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait", params: { WaitTime: 2.0 } }],
        },
      })) as { success: boolean; behavior_tree: string; blackboard?: string };
      expect(create2.success).toBe(true);

      // 4. Query and verify blackboard was updated
      const query2 = (await aiTool.actions.query_bt({
        asset_path: create2.behavior_tree,
      })) as { blackboard?: { name: string; keys: { name: string; type: string }[] } };
      expect(query2.blackboard).toBeDefined();

      const keyNames2 = query2.blackboard!.keys.map((k) => k.name);
      expect(keyNames2).toContain("KeyX");
      expect(keyNames2).toContain("KeyY");
      expect(keyNames2).toContain("KeyZ");

      // Old keys must be gone
      expect(keyNames2).not.toContain("KeyA");
      expect(keyNames2).not.toContain("KeyB");

      // Verify key types (filter out auto-added SelfActor)
      const userKeys = query2.blackboard!.keys.filter((k) => k.name !== "SelfActor");
      expect(userKeys).toHaveLength(3);
      const keyMap = Object.fromEntries(userKeys.map((k) => [k.name, k.type]));
      expect(keyMap.KeyX).toBe("Vector");
      expect(keyMap.KeyY).toBe("Float");
      expect(keyMap.KeyZ).toBe("Object");
    });
  });

  // ── BT Query ──────────────────────────────────────────────────────

  describe("BT Query", () => {
    it("queries a created BT and returns structure", async () => {
      const name = uniqueName();
      const create = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait" }],
        },
      })) as { success: boolean; behavior_tree: string };
      expect(create.success).toBe(true);

      const query = (await aiTool.actions.query_bt({
        asset_path: create.behavior_tree,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── BT Granular ───────────────────────────────────────────────────

  describe("BT Granular Editing", () => {
    it("add → set_params → remove → layout", async () => {
      // Create a minimal BT (root only), then add nodes granularly
      const name = uniqueName();
      const create = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        tree: { type: "Selector" },
      })) as { success: boolean; behavior_tree: string };
      expect(create.success).toBe(true);
      const btPath = create.behavior_tree;

      // Add a Wait task under root (parent_path "" = root)
      const addWait = (await aiTool.actions.add_bt_node({
        asset_path: btPath,
        parent_path: "",
        child_index: -1,
        node_spec: { type: "Task", class: "BTTask_Wait" },
      })) as { success: boolean; node_path?: string };
      expect(addWait.success).toBe(true);

      // Add a Sequence child under root
      const add = (await aiTool.actions.add_bt_node({
        asset_path: btPath,
        parent_path: "",
        child_index: -1,
        node_spec: { type: "Sequence" },
      })) as { success: boolean };
      expect(add.success).toBe(true);

      // Set params on the Wait node (path "0")
      const setP = (await aiTool.actions.set_bt_params({
        asset_path: btPath,
        node_path: "0",
        params: { WaitTime: 5.0 },
      })) as { success: boolean };
      expect(setP.success).toBe(true);

      // Remove the Sequence we added (path "1")
      const remove = (await aiTool.actions.remove_bt_node({
        asset_path: btPath,
        node_path: "1",
      })) as { success: boolean };
      expect(remove.success).toBe(true);

      // Layout
      const layout = (await aiTool.actions.layout_bt({
        asset_path: btPath,
      })) as { success: boolean };
      expect(layout.success).toBe(true);

      // Query to confirm
      const query = (await aiTool.actions.query_bt({
        asset_path: btPath,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── BT Decorators & Services ──────────────────────────────────────

  describe("BT Decorators & Services", () => {
    it("adds a decorator and a service to a node", async () => {
      // Create BT with root only, then add a task, decorator, and service
      const name = uniqueName();
      const create = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        tree: { type: "Selector" },
      })) as { success: boolean; behavior_tree: string };
      expect(create.success).toBe(true);
      const btPath = create.behavior_tree;

      // Add a task child first (so we have a child to attach decorator to)
      const addTask = (await aiTool.actions.add_bt_node({
        asset_path: btPath,
        parent_path: "",
        child_index: -1,
        node_spec: { type: "Task", class: "BTTask_Wait" },
      })) as { success: boolean };
      expect(addTask.success).toBe(true);

      // Add decorator to child "0" (the Wait task)
      const addDec = (await aiTool.actions.add_bt_node({
        asset_path: btPath,
        parent_path: "0",
        child_index: -1,
        node_spec: { class: "BTDecorator_Blackboard", role: "decorator" },
      })) as { success: boolean };
      expect(addDec.success).toBe(true);

      // Add service to root Selector (parent_path "" = root)
      const addSvc = (await aiTool.actions.add_bt_node({
        asset_path: btPath,
        parent_path: "",
        child_index: -1,
        node_spec: { class: "BTService_DefaultFocus", role: "service" },
      })) as { success: boolean };
      expect(addSvc.success).toBe(true);
    });
  });

  // ── EQS Create ────────────────────────────────────────────────────

  describe("EQS Create", () => {
    it("creates an EQS with SimpleGrid generator and Distance test", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "SimpleGrid", params: { GridSize: 500 } }],
        tests: [{ type: "Distance", params: { TestPurpose: "FilterAndScore" } }],
      })) as { success: boolean; eqs_query?: string };
      expect(result.success).toBe(true);
      expect(result.eqs_query).toContain(CONTENT_PATH);
    });
  });

  // ── EQS Query ─────────────────────────────────────────────────────

  describe("EQS Query", () => {
    it("queries a created EQS and returns structure", async () => {
      const name = uniqueName();
      const create = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "SimpleGrid" }],
        tests: [{ type: "Distance" }],
      })) as { success: boolean; eqs_query: string };
      expect(create.success).toBe(true);

      const query = (await aiTool.actions.query_eqs({
        asset_path: create.eqs_query,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── EQS Granular ──────────────────────────────────────────────────

  describe("EQS Granular Editing", () => {
    it("add generator → set params → add test → set test params → remove test → remove generator", async () => {
      const name = uniqueName();
      const create = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "SimpleGrid" }],
        tests: [{ type: "Distance" }],
      })) as { success: boolean; eqs_query: string };
      expect(create.success).toBe(true);
      const eqsPath = create.eqs_query;

      // Add a second generator
      const addGen = (await aiTool.actions.add_eqs_generator({
        asset_path: eqsPath,
        option_index: -1,
        generator_spec: { type: "SimpleGrid", params: { GridSize: 1000 } },
      })) as { success: boolean };
      expect(addGen.success).toBe(true);

      // Set params on first generator (option_index 0)
      const setGenP = (await aiTool.actions.set_eqs_generator_params({
        asset_path: eqsPath,
        option_index: 0,
        params: { GridSize: 250 },
      })) as { success: boolean };
      expect(setGenP.success).toBe(true);

      // Add a test to option 0
      const addTest = (await aiTool.actions.add_eqs_test({
        asset_path: eqsPath,
        option_index: 0,
        test_index: -1,
        test_spec: { type: "Trace" },
      })) as { success: boolean };
      expect(addTest.success).toBe(true);

      // Set test params (option_index 0, test_index 1 — the newly added one)
      const setTestP = (await aiTool.actions.set_eqs_test_params({
        asset_path: eqsPath,
        option_index: 0,
        test_index: 1,
        params: { TestPurpose: "FilterOnly" },
      })) as { success: boolean };
      expect(setTestP.success).toBe(true);

      // Remove the test we added (index 1)
      const removeTest = (await aiTool.actions.remove_eqs_test({
        asset_path: eqsPath,
        option_index: 0,
        test_index: 1,
      })) as { success: boolean };
      expect(removeTest.success).toBe(true);

      // Remove the second generator (option_index 1)
      const removeGen = (await aiTool.actions.remove_eqs_generator({
        asset_path: eqsPath,
        option_index: 1,
      })) as { success: boolean };
      expect(removeGen.success).toBe(true);
    });
  });

  // ── EQS Full Class Name Support (Issue #73) ─────────────────────

  describe("EQS Full Class Name Support", () => {
    it("create_eqs accepts full class names for generators and tests", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "EnvQueryGenerator_SimpleGrid", params: { GridSize: 500 } }],
        tests: [{ type: "EnvQueryTest_Distance", params: { TestPurpose: "FilterAndScore" } }],
      })) as { success: boolean; eqs_query?: string };
      expect(result.success).toBe(true);
      expect(result.eqs_query).toContain(CONTENT_PATH);

      // Verify the query actually has content (not silently empty)
      const query = (await aiTool.actions.query_eqs({
        asset_path: result.eqs_query!,
      })) as { success: boolean; generators?: unknown[]; tests?: unknown[] };
      expect(query.success).toBe(true);
      expect(query.generators).toBeDefined();
      expect((query.generators as unknown[]).length).toBeGreaterThan(0);
    });

    it("add_eqs_generator accepts full class name", async () => {
      const name = uniqueName();
      const create = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "SimpleGrid" }],
        tests: [{ type: "Distance" }],
      })) as { success: boolean; eqs_query: string };
      expect(create.success).toBe(true);

      const addGen = (await aiTool.actions.add_eqs_generator({
        asset_path: create.eqs_query,
        option_index: -1,
        generator_spec: { type: "EnvQueryGenerator_OnCircle" },
      })) as { success: boolean };
      expect(addGen.success).toBe(true);
    });

    it("add_eqs_test accepts full class name", async () => {
      const name = uniqueName();
      const create = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "SimpleGrid" }],
        tests: [{ type: "Distance" }],
      })) as { success: boolean; eqs_query: string };
      expect(create.success).toBe(true);

      const addTest = (await aiTool.actions.add_eqs_test({
        asset_path: create.eqs_query,
        option_index: 0,
        test_index: -1,
        test_spec: { type: "EnvQueryTest_Trace" },
      })) as { success: boolean };
      expect(addTest.success).toBe(true);
    });

    it("create_eqs with invalid type returns failure", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_eqs({
        name,
        content_path: CONTENT_PATH,
        generators: [{ type: "NonExistentGenerator" }],
        tests: [{ type: "Distance" }],
      })) as { success: boolean };
      expect(result.success).toBe(false);
    });
  });
});

describe.skipIf(editorRunning)("AI Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
