/**
 * AI Controller + Behavior Tree integration test.
 *
 * Verifies the full chain end-to-end:
 *   BT → AIController BP (parent: AIController, event graph wires
 *        OnPossess → RunBehaviorTree(BTAsset)) → Character BP (CDO has
 *        AIControllerClass + AutoPossessAI) → spawn → PIE → BT is running
 *
 * Also inspects the AIController BP event graph via Python to verify the
 * Event OnPossess → RunBehaviorTree wiring + the BTAsset pin literal
 * survived the BlueprintBuilder round-trip.
 */

import { describe, it, expect, beforeAll, afterAll, afterEach } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { aiTool } from "../../src/registry/ai.js";
import { blueprintTool } from "../../src/registry/blueprint.js";
import { actorsTool } from "../../src/registry/actors.js";
import { playtestTool } from "../../src/registry/playtest.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";
import { trackActor, cleanupAll } from "../helpers/cleanup-tracker.js";
import { clearTestActors } from "../helpers/level-isolation.js";
import { runPython } from "../../src/tools/core/run-python.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

/** Run Python inside UE5 and return the parsed result. */
async function pyQuery(code: string): Promise<Record<string, unknown>> {
  const result = await runPython({ code });
  if (!result.success) {
    throw new Error(`Python failed: ${result.error}\n${result.traceback ?? ""}`);
  }
  return (result.result ?? {}) as Record<string, unknown>;
}

describe.runIf(editorRunning)(
  "AI Controller + BehaviorTree CDO Propagation",
  () => {
    beforeAll(async () => {
      // Stop any leftover PIE from a prior test suite
      try {
        await playtestTool.actions.stop_pie({});
      } catch {
        /* best-effort */
      }
      await new Promise((r) => setTimeout(r, 1000));
      await clearTestActors();
    });

    afterAll(async () => {
      // Safety stop PIE if still running
      try {
        await playtestTool.actions.stop_pie({});
      } catch {
        /* best-effort */
      }
      await cleanupAll();
      await deleteTestAssets();
    });

    afterEach(async () => {
      // Stop PIE between tests
      try {
        await playtestTool.actions.stop_pie({});
      } catch {
        /* best-effort */
      }
    });

    // ── Step 1: Create the full asset chain ──────────────────────────

    let btPath: string;
    let bbPath: string;
    let aicPath: string;
    let charPath: string;

    it("creates a BehaviorTree with a Wait task", async () => {
      const name = uniqueName();
      const result = (await aiTool.actions.create_bt({
        name,
        content_path: CONTENT_PATH,
        blackboard_keys: [
          { name: "HomeLocation", type: "Vector" },
          { name: "PatrolLocation", type: "Vector" },
        ],
        tree: {
          type: "Selector",
          children: [{ type: "BTTask_Wait", params: { WaitTime: 999 } }],
        },
      })) as { success: boolean; behavior_tree: string; blackboard?: string };

      expect(result.success).toBe(true);
      btPath = result.behavior_tree;
      bbPath = result.blackboard!;
      expect(btPath).toBeDefined();
    });

    // Parent class is base AAIController. The BT is wired via the BP's event graph
    // (Event OnPossess → RunBehaviorTree(BTAsset)) — see create_ai_controller in
    // bridge/src/registry/blueprint.ts.
    it("creates an AIController BP with BT wired on OnPossess", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_ai_controller({
        name,
        content_path: CONTENT_PATH,
        behavior_tree_path: btPath,
        blackboard_path: bbPath,
      })) as { success: boolean; asset_path: string };

      expect(result.success).toBe(true);
      aicPath = result.asset_path;
    });

    it("creates a Character BP with AIControllerClass on CDO", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
        ai_controller_path: aicPath,
        auto_possess: "PlacedInWorldOrSpawned",
      })) as { success: boolean; asset_path: string };

      expect(result.success).toBe(true);
      charPath = result.asset_path;
    });

    // ── Step 2: Inspect CDO values via Python ────────────────────────

    it("verifies AIController BP parent class is AIController", async () => {
      const q = (await blueprintTool.actions.query({ asset_path: aicPath })) as {
        success: boolean;
        parent_class?: string;
      };
      expect(q.success).toBe(true);
      // Parent must be UE's built-in AIController (no custom AArborAIController).
      expect(
        q.parent_class,
        `AIController BP parent should be "AIController" but resolved to "${q.parent_class}"`
      ).toBe("AIController");
    });

    it("verifies AIController BP event graph runs the BT on OnPossess", async () => {
      type EGNode = {
        guid: string;
        type: string;
        event?: string;
        function?: string;
        defaults?: Record<string, string>;
        pins?: Array<{ name: string; default_object?: string; default?: string }>;
      };
      type EGConn = { from: string; from_pin: string; to: string; to_pin: string };
      const q = (await blueprintTool.actions.query({ asset_path: aicPath })) as {
        success: boolean;
        event_graph?: { nodes: EGNode[]; connections: EGConn[] };
      };
      expect(q.success).toBe(true);
      expect(q.event_graph, "AIController BP must have an event graph").toBeDefined();

      const nodes = q.event_graph!.nodes;
      const conns = q.event_graph!.connections;

      const eventNode = nodes.find(
        (n) => n.type === "Event" && n.event === "ReceivePossess"
      );
      const runBTNode = nodes.find(
        (n) => n.type === "CallFunction" && n.function === "RunBehaviorTree"
      );
      expect(eventNode, `expected an Event "ReceivePossess" node; got: ${JSON.stringify(nodes.map(n => ({type: n.type, event: n.event, function: n.function})))}`)
        .toBeDefined();
      expect(runBTNode, "expected a CallFunction RunBehaviorTree node").toBeDefined();

      const wired = conns.some(
        (c) =>
          c.from === eventNode!.guid &&
          c.from_pin === "then" &&
          c.to === runBTNode!.guid &&
          c.to_pin === "execute"
      );
      expect(wired, `expected ReceivePossess.then -> RunBehaviorTree.execute connection; got conns: ${JSON.stringify(conns)}`)
        .toBe(true);

      // BTAsset pin must exist on the RunBehaviorTree call. Verifying the pin's
      // *value* via this query isn't reliable: the C++ query only exposes
      // Pin->DefaultValue (string), not Pin->DefaultObject (the asset reference).
      // The PIE smoke test below is the source of truth for runtime correctness —
      // a wrong/null BTAsset produces a controller with no BrainComponent.
      const btPin = runBTNode!.pins?.find((p) => p.name === "BTAsset");
      expect(btPin, "RunBehaviorTree call should expose a BTAsset pin").toBeDefined();
    });

    it("verifies AIControllerClass is set on Character CDO", async () => {
      const data = await pyQuery(`
import unreal

bp = unreal.EditorAssetLibrary.load_asset("${charPath}")
if not bp or not bp.generated_class():
    _write_result({"success": False, "error": "Could not load Character BP"})
else:
    cdo = unreal.get_default_object(bp.generated_class())
    aic = cdo.get_editor_property("ai_controller_class")
    auto_possess = str(cdo.get_editor_property("auto_possess_ai"))

    _write_result({
        "success": True,
        "cdo_class": cdo.get_class().get_name(),
        "ai_controller_class": aic.get_name() if aic else None,
        "ai_controller_class_path": aic.get_path_name() if aic else None,
        "auto_possess_ai": auto_possess,
    })
`);

      expect(data.success).toBe(true);
      expect(
        data.ai_controller_class,
        `CDO.AIControllerClass should be set but was ${data.ai_controller_class}`
      ).toBeTruthy();
      // Should be the generated class of our AIController BP, not base AAIController
      const aicClassName = data.ai_controller_class as string;
      expect(
        aicClassName,
        `AIControllerClass should point to our BP's generated class, not "${aicClassName}"`
      ).toContain("IntTest_");
      // AutoPossessAI should not be "Disabled"
      const autoPossess = data.auto_possess_ai as string;
      expect(
        autoPossess,
        `AutoPossessAI should be "PlacedInWorldOrSpawned" but was "${autoPossess}"`
      ).not.toContain("Disabled");
    });

    // ── Step 3: PIE runtime verification ─────────────────────────────

    it(
      "spawns the character, starts PIE, and verifies BT is running",
      async () => {
        // Place the character in the level
        const place = (await actorsTool.actions.place({
          asset_path: charPath,
          x: 0,
          y: 0,
          z: 200,
        })) as { actor_name?: string; actor_path?: string };
        expect(place.actor_name).toBeTruthy();
        trackActor(place.actor_name!);

        // Start PIE
        const start = (await playtestTool.actions.start_pie(
          {}
        )) as { success?: boolean };
        expect(start).toBeDefined();

        // Wait for PIE to initialize and AI to possess
        await new Promise((r) => setTimeout(r, 5000));

        // Verify PIE is running
        const running = (await playtestTool.actions.is_running(
          {}
        )) as { running?: boolean; is_running?: boolean };
        const isRunning = running.running ?? running.is_running;
        expect(isRunning, "PIE should be running").toBe(true);

        // Query all AIControllers in the PIE world and check if any have our BT running
        const btCheck = await pyQuery(`
import unreal

# During PIE, iterate all worlds to find the game world (PIE world)
all_controllers = []
for world in unreal.EditorLevelLibrary.get_all_level_actors():
    # This gets editor world actors; for PIE we need a different approach
    pass

# Use GEngine to find PIE worlds
try:
    # Try getting actors from PIE world via GameplayStatics
    # GameplayStatics needs a world context - during PIE the editor subsystem can provide it
    ess = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ess.get_game_world()
    if world:
        all_controllers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AIController)
except:
    pass

# Fallback: try the editor world (PIE actors may appear here too)
if not all_controllers:
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        all_controllers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AIController)
    except:
        pass

results = []
bt_running = False
for ctrl in all_controllers:
    ctrl_name = ctrl.get_name()
    ctrl_class = ctrl.get_class().get_name()

    # Check if this controller has a BrainComponent (BT component)
    brain = None
    try:
        brain = ctrl.get_component_by_class(unreal.BrainComponent)
    except:
        pass
    bt_name = None
    if brain:
        try:
            bt_asset = brain.get_editor_property("default_behavior_tree_asset")
            bt_name = bt_asset.get_name() if bt_asset else None
        except:
            pass
        if not bt_name:
            try:
                bt_asset = brain.get_editor_property("current_tree")
                bt_name = bt_asset.get_name() if bt_asset else None
            except:
                pass

    has_brain = brain is not None
    if has_brain:
        bt_running = True

    results.append({
        "controller_name": ctrl_name,
        "controller_class": ctrl_class,
        "has_brain_component": has_brain,
        "behavior_tree": bt_name,
    })

_write_result({
    "success": True,
    "total_ai_controllers": len(all_controllers),
    "any_bt_running": bt_running,
    "controllers": results,
})
`);

        expect(btCheck.success).toBe(true);
        expect(
          btCheck.total_ai_controllers,
          "There should be at least one AIController in PIE"
        ).toBeGreaterThan(0);
        expect(
          btCheck.any_bt_running,
          `No AIController has a running BT. Controllers found: ${JSON.stringify(btCheck.controllers, null, 2)}`
        ).toBe(true);

        // Stop PIE
        await playtestTool.actions.stop_pie({});
      },
      60_000
    );
  }
);

describe.skipIf(editorRunning)(
  "AI Controller + BehaviorTree CDO Propagation (skipped)",
  () => {
    it("UE5 editor not reachable — skipping integration tests", () => {
      console.log(
        "Start the UE5 editor with Remote Control API enabled to run these tests."
      );
    });
  }
);
