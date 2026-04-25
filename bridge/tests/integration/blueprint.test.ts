import { describe, it, expect, afterAll } from "vitest";
import { isConnected, callArbor } from "../../src/ue5-client.js";
import { blueprintTool } from "../../src/registry/blueprint.js";
import { aiTool } from "../../src/registry/ai.js";
import { deleteTestAssets } from "../helpers/asset-cleanup.js";
import { runPython } from "../../src/tools/core/run-python.js";

const editorRunning = await isConnected();

const PREFIX = "IntTest_";
let counter = 0;
function uniqueName(): string {
  return `${PREFIX}${Date.now()}_${counter++}`;
}

const CONTENT_PATH = "/Game/IntTest";

describe.runIf(editorRunning)("Blueprint Operations", () => {
  afterAll(async () => {
    await deleteTestAssets();
  });

  // ── Discovery ─────────────────────────────────────────────────────

  describe("BP Discovery", () => {
    it("list_node_types returns an array", async () => {
      const result = (await blueprintTool.actions.list_node_types(
        {}
      )) as { types?: unknown[] };
      expect(result).toBeDefined();
    });

    it("list_node_types filters by name", async () => {
      const result = (await blueprintTool.actions.list_node_types({
        filter: "Branch",
      })) as { types?: unknown[] };
      expect(result).toBeDefined();
    });

    it("list_component_types returns an array", async () => {
      const result = (await blueprintTool.actions.list_component_types(
        {}
      )) as { types?: unknown[] };
      expect(result).toBeDefined();
    });

    it("list_functions returns functions for KismetMathLibrary", async () => {
      const result = (await blueprintTool.actions.list_functions({
        class_name: "KismetMathLibrary",
      })) as { functions?: unknown[] };
      expect(result).toBeDefined();
    });
  });

  // ── Node Pins ─────────────────────────────────────────────────────

  describe("Node Pins", () => {
    it("get_node_pins returns pin listing for BeginPlay", async () => {
      // Create a context BP first to avoid transient BP name collision crash
      const ctxName = uniqueName();
      const ctx = (await blueprintTool.actions.create_character({
        name: ctxName,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(ctx.success).toBe(true);

      const result = (await blueprintTool.actions.get_node_pins({
        node_spec: { type: "Event", event: "BeginPlay" },
        asset_path: ctx.asset_path,
      })) as { pins?: unknown[] };
      expect(result).toBeDefined();
    });

    it("get_node_pins by node_guid returns pins for an existing node", async () => {
      const bpName = uniqueName();
      const created = (await blueprintTool.actions.create_character({
        name: bpName,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(created.success).toBe(true);

      // Add a node and get its GUID
      const added = (await blueprintTool.actions.add_node({
        asset_path: created.asset_path,
        node_spec: { type: "CallFunction", function: "K2_GetPawn" },
      })) as { success: boolean; node_guid: string };
      expect(added.success).toBe(true);
      expect(added.node_guid).toBeDefined();

      // Get pins by GUID
      const result = (await blueprintTool.actions.get_node_pins({
        asset_path: created.asset_path,
        node_guid: added.node_guid,
      })) as { success: boolean; pins: Array<{ name: string; direction: string }> };
      expect(result.success).toBe(true);
      expect(result.pins).toBeDefined();
      expect(result.pins.length).toBeGreaterThan(0);
      // Should have at least a ReturnValue output pin
      const outputPins = result.pins.filter((p) => p.direction === "output");
      expect(outputPins.length).toBeGreaterThan(0);
    });
  });

  // ── Create ────────────────────────────────────────────────────────

  describe("Create Blueprints", () => {
    it("creates a Character blueprint", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path?: string };
      expect(result.success).toBe(true);
      expect(result.asset_path).toContain(CONTENT_PATH);
    });

    it("creates a Character blueprint with correct mesh offset and rotation", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
        mesh_path: "/Engine/EngineMeshes/SkeletalCube.SkeletalCube",
      })) as { success: boolean; asset_path?: string };
      expect(result.success).toBe(true);

      // Use arbor.inspect to read the CDO's Mesh component properties
      const pyResult = await runPython({
        code: `
import arbor.inspect
data = arbor.inspect.inspect_blueprint("${result.asset_path}")
if not data:
    _write_result({"success": False, "error": "inspect_blueprint returned None"})
else:
    # Find the Mesh component in CDO properties
    mesh_props = {}
    cdo = data.get("cdo_properties", [])
    for p in cdo:
        if p["name"] == "mesh":
            comp_ref = p.get("value")
            if comp_ref and isinstance(comp_ref, dict):
                mesh_props["found"] = True
            break

    # Read the Mesh component's relative transform directly
    import unreal
    bp = unreal.EditorAssetLibrary.load_asset("${result.asset_path}")
    gen_class = bp.generated_class()
    cdo_obj = unreal.get_default_object(gen_class)
    mesh_comp = cdo_obj.get_editor_property("mesh")
    rel_loc = mesh_comp.get_editor_property("relative_location")
    rel_rot = mesh_comp.get_editor_property("relative_rotation")
    _write_result({
        "success": True,
        "relative_location": {"X": rel_loc.x, "Y": rel_loc.y, "Z": rel_loc.z},
        "relative_rotation": {"Pitch": rel_rot.pitch, "Yaw": rel_rot.yaw, "Roll": rel_rot.roll},
    })
`,
      });
      expect(pyResult.success).toBe(true);
      const data = pyResult.result as Record<string, unknown>;

      // Verify Z offset = -88 (default CapsuleHalfHeight)
      const relLoc = data.relative_location as { X: number; Y: number; Z: number };
      expect(relLoc.Z).toBe(-88);

      // Verify Yaw = -90 (standard UE5 character mesh rotation)
      const relRot = data.relative_rotation as { Pitch: number; Yaw: number; Roll: number };
      expect(relRot.Pitch).toBe(0);
      expect(relRot.Yaw).toBe(-90);
      expect(relRot.Roll).toBe(0);
    });

    it("creates a GameMode blueprint", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_game_mode({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path?: string };
      expect(result.success).toBe(true);
      expect(result.asset_path).toContain(CONTENT_PATH);
    });

    it("creates an AIController blueprint", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_ai_controller({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path?: string };
      expect(result.success).toBe(true);
      expect(result.asset_path).toContain(CONTENT_PATH);
    });

    it("creates an AIController with perception_config and has AIPerceptionComponent", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_ai_controller({
        name,
        content_path: CONTENT_PATH,
        perception_config: [
          { sense: "AISense_Sight", dominant: true, params: { SightRadius: 3000 } },
        ],
      })) as { success: boolean; asset_path: string };
      expect(result.success).toBe(true);

      // Query the BP and check for the perception component
      const query = (await blueprintTool.actions.query({
        asset_path: result.asset_path,
      })) as { success: boolean; components: Array<{ name: string; class: string }> };
      expect(query.success).toBe(true);
      const perceptionComp = query.components.find(
        (c) => c.class === "AIPerceptionComponent"
      );
      expect(perceptionComp).toBeDefined();
    });

    it("verifies DetectionByAffiliation defaults to all-true on Sight sense", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_ai_controller({
        name,
        content_path: CONTENT_PATH,
        perception_config: [
          { sense: "AISense_Sight", params: { SightRadius: 3000 } },
        ],
      })) as { success: boolean; asset_path: string };
      expect(result.success).toBe(true);

      // Use C++ QuerySenseConfig to read sense config properties from the SCS template
      const query = (await callArbor("BlueprintBuilder", "QuerySenseConfig", {
        AssetPath: result.asset_path,
      })) as { ReturnValue: string };
      const senseData = JSON.parse(query.ReturnValue);
      expect(senseData.success).toBe(true);
      expect(senseData.senses).toHaveLength(1);

      const sight = senseData.senses[0];
      expect(sight.class).toBe("AISenseConfig_Sight");
      expect(sight.detect_enemies).toBe(true);
      expect(sight.detect_neutrals).toBe(true);
      expect(sight.detect_friendlies).toBe(true);
      expect(sight.sight_radius).toBe(3000);
    });

    it("respects dominant flag and DetectionByAffiliation overrides", async () => {
      const name = uniqueName();
      const result = (await blueprintTool.actions.create_ai_controller({
        name,
        content_path: CONTENT_PATH,
        perception_config: [
          {
            sense: "AISense_Sight",
            dominant: true,
            params: { SightRadius: 2000, DetectFriendlies: false },
          },
        ],
      })) as { success: boolean; asset_path: string };
      expect(result.success).toBe(true);

      const query = (await callArbor("BlueprintBuilder", "QuerySenseConfig", {
        AssetPath: result.asset_path,
      })) as { ReturnValue: string };
      const senseData = JSON.parse(query.ReturnValue);
      expect(senseData.success).toBe(true);
      expect(senseData.senses).toHaveLength(1);

      const sight = senseData.senses[0];
      expect(sight.dominant_sense).toBe(true);
      expect(sight.detect_enemies).toBe(true);
      expect(sight.detect_neutrals).toBe(true);
      expect(sight.detect_friendlies).toBe(false);
      expect(sight.sight_radius).toBe(2000);
    });
  });

  // ── BP Query ──────────────────────────────────────────────────────

  describe("BP Query", () => {
    it("queries a created blueprint", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);

      const query = (await blueprintTool.actions.query({
        asset_path: create.asset_path,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── Node Editing ──────────────────────────────────────────────────

  describe("BP Node Editing", () => {
    it("adds nodes, connects them, and compiles", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);
      const bpPath = create.asset_path;

      // Add BeginPlay event
      const addBeginPlay = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "Event", event: "BeginPlay" },
      })) as { success: boolean; node_guid?: string };
      expect(addBeginPlay.success).toBe(true);
      expect(addBeginPlay.node_guid).toBeDefined();

      // Add PrintString
      const addPrint = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "CallFunction", function: "PrintString" },
      })) as { success: boolean; node_guid?: string };
      expect(addPrint.success).toBe(true);
      expect(addPrint.node_guid).toBeDefined();

      // Connect BeginPlay exec → PrintString exec
      const connect = (await blueprintTool.actions.connect({
        asset_path: bpPath,
        from_guid: addBeginPlay.node_guid!,
        from_pin: "then",
        to_guid: addPrint.node_guid!,
        to_pin: "execute",
      })) as { success: boolean };
      expect(connect.success).toBe(true);

      // Compile
      const compile = (await blueprintTool.actions.compile({
        asset_path: bpPath,
      })) as { success: boolean; errors?: unknown[] };
      expect(compile.success).toBe(true);
    });
  });

  // ── Pin Defaults ──────────────────────────────────────────────────

  describe("BP Pin Defaults", () => {
    it("sets a string pin default and verifies it in query", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);
      const bpPath = create.asset_path;

      // Add PrintString node
      const addNode = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "CallFunction", function: "PrintString" },
      })) as { success: boolean; node_guid?: string };
      expect(addNode.success).toBe(true);

      // Set InString pin value
      const setPin = (await blueprintTool.actions.set_pin({
        asset_path: bpPath,
        node_guid: addNode.node_guid!,
        pin_name: "InString",
        pin_value: "Hello Integration Test",
      })) as { success: boolean };
      expect(setPin.success).toBe(true);

      // Query and verify the pin value was actually set
      const query = (await blueprintTool.actions.query({
        asset_path: bpPath,
      })) as { event_graph?: { nodes?: Array<{ guid?: string; pins?: Array<{ name: string; default?: string }> }> } };
      expect(query).toBeDefined();
      const node = query.event_graph?.nodes?.find(
        (n) => n.guid === addNode.node_guid
      );
      expect(node).toBeDefined();
      const pin = node?.pins?.find((p) => p.name === "InString");
      expect(pin?.default).toBe("Hello Integration Test");
    });

    it("sets an object reference pin (asset path)", async () => {
      // Create a BehaviorTree asset to reference
      const btName = uniqueName();
      const btCreate = (await aiTool.actions.create_bt({
        name: btName,
        content_path: CONTENT_PATH,
        tree: { type: "Sequence", children: [{ type: "Task", class: "BTTask_Wait" }] },
      })) as { success: boolean; behavior_tree?: string };
      expect(btCreate.success).toBe(true);
      const btPath = btCreate.behavior_tree!;

      // Create a Blueprint with an AIController parent to add RunBehaviorTree
      const bpName = uniqueName();
      const bpCreate = (await blueprintTool.actions.create_character({
        name: bpName,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(bpCreate.success).toBe(true);
      const bpPath = bpCreate.asset_path;

      // Add RunBehaviorTree node (has a BTAsset pin of type UBehaviorTree*)
      const addNode = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "CallFunction", function: "RunBehaviorTree", owner_class: "AIController" },
      })) as { success: boolean; node_guid?: string };
      expect(addNode.success).toBe(true);
      expect(addNode.node_guid).toBeDefined();

      // Set the BTAsset pin to the BehaviorTree asset path
      const setPin = (await blueprintTool.actions.set_pin({
        asset_path: bpPath,
        node_guid: addNode.node_guid!,
        pin_name: "BTAsset",
        pin_value: btPath,
      })) as { success: boolean; error?: string };
      expect(setPin.error).toBeUndefined();
      expect(setPin.success).toBe(true);
    });
  });

  // ── Component Ops ─────────────────────────────────────────────────

  describe("Component Operations", () => {
    it("adds, sets property on, and removes a component", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);
      const bpPath = create.asset_path;

      // Add BoxCollision component
      const addComp = (await blueprintTool.actions.add_component({
        asset_path: bpPath,
        component_spec: { name: "TestBox", type: "BoxComponent" },
      })) as { success: boolean };
      expect(addComp.success).toBe(true);

      // Set a property on it
      const setProp = (await blueprintTool.actions.set_component_property({
        asset_path: bpPath,
        component_name: "TestBox",
        property_spec: { bGenerateOverlapEvents: true },
      })) as { success: boolean };
      expect(setProp.success).toBe(true);

      // Remove it
      const removeComp = (await blueprintTool.actions.remove_component({
        asset_path: bpPath,
        component_name: "TestBox",
      })) as { success: boolean };
      expect(removeComp.success).toBe(true);
    });

    it("query returns inherited C++ components on a Character BP", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);

      const query = (await blueprintTool.actions.query({
        asset_path: create.asset_path,
      })) as { success: boolean; components: Array<{ name: string; class: string; inherited?: boolean }> };
      expect(query.success).toBe(true);
      // Character BPs inherit Mesh, CapsuleComponent, CharacterMovement from C++
      const meshComp = query.components.find((c) => c.class === "SkeletalMeshComponent");
      expect(meshComp).toBeDefined();
      expect(meshComp!.inherited).toBe(true);
    });

    it("set_component_property works on inherited Mesh component", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);

      const setProp = (await blueprintTool.actions.set_component_property({
        asset_path: create.asset_path,
        component_name: "CharacterMesh0",
        property_spec: { bHiddenInGame: true },
      })) as { success: boolean };
      expect(setProp.success).toBe(true);
    });

    it("set_component_transform sets location and rotation on inherited Mesh", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);

      const result = (await blueprintTool.actions.set_component_transform({
        asset_path: create.asset_path,
        component_name: "Mesh",
        location: { x: 0, y: 0, z: -88 },
        rotation: { pitch: 0, yaw: -90, roll: 0 },
      })) as { success: boolean };
      expect(result.success).toBe(true);
    });

    it("set_component_transform sets scale on a BP-added component", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);
      const bpPath = create.asset_path;

      await blueprintTool.actions.add_component({
        asset_path: bpPath,
        component_spec: { name: "TestBox", type: "BoxComponent" },
      });

      const result = (await blueprintTool.actions.set_component_transform({
        asset_path: bpPath,
        component_name: "TestBox",
        scale: { x: 2, y: 2, z: 2 },
      })) as { success: boolean };
      expect(result.success).toBe(true);
    });

    it("set_component_transform rejects call with no transform fields", async () => {
      await expect(
        blueprintTool.actions.set_component_transform({
          asset_path: "/Game/IntTest/Dummy",
          component_name: "Mesh",
        }),
      ).rejects.toThrow("At least one of location, rotation, or scale required");
    });
  });

  // ── Lifecycle ─────────────────────────────────────────────────────

  describe("BP Lifecycle", () => {
    it("full round-trip: create → add nodes → wire → set pins → compile → query", async () => {
      const name = uniqueName();
      const create = (await blueprintTool.actions.create_character({
        name,
        content_path: CONTENT_PATH,
      })) as { success: boolean; asset_path: string };
      expect(create.success).toBe(true);
      const bpPath = create.asset_path;

      // Add BeginPlay
      const beginPlay = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "Event", event: "BeginPlay" },
      })) as { success: boolean; node_guid: string };
      expect(beginPlay.success).toBe(true);

      // Add Branch
      const branch = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "Branch" },
      })) as { success: boolean; node_guid: string };
      expect(branch.success).toBe(true);

      // Add two PrintStrings
      const printTrue = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "CallFunction", function: "PrintString" },
      })) as { success: boolean; node_guid: string };
      expect(printTrue.success).toBe(true);

      const printFalse = (await blueprintTool.actions.add_node({
        asset_path: bpPath,
        node_spec: { type: "CallFunction", function: "PrintString" },
      })) as { success: boolean; node_guid: string };
      expect(printFalse.success).toBe(true);

      // Wire: BeginPlay → Branch
      const c1 = (await blueprintTool.actions.connect({
        asset_path: bpPath,
        from_guid: beginPlay.node_guid,
        from_pin: "then",
        to_guid: branch.node_guid,
        to_pin: "execute",
      })) as { success: boolean };
      expect(c1.success).toBe(true);

      // Wire: Branch then → PrintString 1
      const c2 = (await blueprintTool.actions.connect({
        asset_path: bpPath,
        from_guid: branch.node_guid,
        from_pin: "then",
        to_guid: printTrue.node_guid,
        to_pin: "execute",
      })) as { success: boolean };
      expect(c2.success).toBe(true);

      // Wire: Branch else → PrintString 2
      const c3 = (await blueprintTool.actions.connect({
        asset_path: bpPath,
        from_guid: branch.node_guid,
        from_pin: "else",
        to_guid: printFalse.node_guid,
        to_pin: "execute",
      })) as { success: boolean };
      expect(c3.success).toBe(true);

      // Set pin defaults
      const sp1 = (await blueprintTool.actions.set_pin({
        asset_path: bpPath,
        node_guid: printTrue.node_guid,
        pin_name: "InString",
        pin_value: "Condition was TRUE",
      })) as { success: boolean };
      expect(sp1.success).toBe(true);

      const sp2 = (await blueprintTool.actions.set_pin({
        asset_path: bpPath,
        node_guid: printFalse.node_guid,
        pin_name: "InString",
        pin_value: "Condition was FALSE",
      })) as { success: boolean };
      expect(sp2.success).toBe(true);

      // Compile
      const compile = (await blueprintTool.actions.compile({
        asset_path: bpPath,
      })) as { success: boolean };
      expect(compile.success).toBe(true);

      // Query and verify
      const query = (await blueprintTool.actions.query({
        asset_path: bpPath,
      })) as Record<string, unknown>;
      expect(query).toBeDefined();
    });
  });

  // ── AnimBlueprint / AnimGraph ───────────────────────────────────────

  describe("AnimBlueprint", () => {
    let abpPath: string;
    let bsPath: string;
    let skeletonPath: string;

    it("creates ABP and BlendSpace1D via Python, then setup_anim wires variable", async () => {
      const abpName = uniqueName();
      const bsName = uniqueName();
      abpPath = `${CONTENT_PATH}/${abpName}`;
      bsPath = `${CONTENT_PATH}/${bsName}`;

      // Create a BlendSpace1D via Python (no Arbor builder for BlendSpaces — test fixture only)
      const pyResult = await runPython({
        code: `
import unreal

# Find any skeleton to use
skeleton = None
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "Skeleton"))
for a in assets:
    skeleton = unreal.load_asset(str(a.package_name) + "." + str(a.asset_name))
    if skeleton:
        break

if not skeleton:
    _write_result({"success": False, "error": "No skeleton found in project"})
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    bs_factory = unreal.BlendSpaceFactory1D()
    bs_factory.set_editor_property("target_skeleton", skeleton)
    bs = tools.create_asset("${bsName}", "${CONTENT_PATH}", None, bs_factory)
    if not bs:
        _write_result({"success": False, "error": "Failed to create BlendSpace1D"})
    else:
        unreal.EditorAssetLibrary.save_loaded_asset(bs)
        _write_result({
            "success": True,
            "bs_path": bs.get_path_name().split(".")[0],
            "skeleton_path": skeleton.get_path_name(),
        })
`,
      });
      expect(pyResult.success).toBe(true);
      const pyData = pyResult as { success: boolean; bs_path?: string; skeleton_path?: string };
      if (pyData.bs_path) bsPath = pyData.bs_path;

      // Create ABP via Arbor's build_bp with parent_class AnimInstance
      const buildResult = await callArbor("BlueprintBuilder", "BuildBlueprintFromJSONString", {
        JsonString: JSON.stringify({
          name: abpName,
          parent_class: "AnimInstance",
          skeleton: pyData.skeleton_path,
        }),
        AssetPath: CONTENT_PATH,
      });
      expect(buildResult.ReturnValue).toBeTruthy();

      // Call setup_anim
      const setupResult = (await blueprintTool.actions.setup_anim({
        asset_path: abpPath,
        blendspace_path: bsPath,
        variable_name: "Speed",
        variable_axis: "X",
      })) as { success: boolean; pose_wired: boolean; variable_wired: boolean; blendspace_node_guid?: string };

      expect(setupResult.success).toBe(true);
      expect(setupResult.pose_wired).toBe(true);
      expect(setupResult.variable_wired).toBe(true);
      expect(setupResult.blendspace_node_guid).toBeDefined();
    });

    it("query_anim returns AnimGraph structure", async () => {
      if (!abpPath) return;

      const result = (await blueprintTool.actions.query_anim({
        asset_path: abpPath,
      })) as {
        success: boolean;
        anim_graph?: { nodes?: unknown[]; connections?: unknown[] };
        variables?: Array<{ name: string }>;
      };

      expect(result.success).toBe(true);
      expect(result.anim_graph).toBeDefined();
      expect(result.anim_graph!.nodes!.length).toBeGreaterThanOrEqual(2); // Root + BlendSpacePlayer
      expect(result.anim_graph!.connections!.length).toBeGreaterThanOrEqual(1);
      expect(result.variables).toBeDefined();
      const speedVar = result.variables!.find((v) => v.name === "Speed");
      expect(speedVar).toBeDefined();
    });

    it("disconnect works on AnimGraph nodes", async () => {
      if (!abpPath) return;

      // Get node GUIDs from query_anim
      const q1 = (await blueprintTool.actions.query_anim({
        asset_path: abpPath,
      })) as {
        success: boolean;
        anim_graph?: {
          nodes?: Array<{ guid: string; class: string; pins?: Array<{ name: string; direction: string; linked_count?: number }> }>;
          connections?: Array<{ from: string; from_pin: string; to: string; to_pin: string }>;
        };
      };

      // Find the pose connection (BlendSpacePlayer -> Root)
      const poseConn = q1.anim_graph!.connections!.find((c) =>
        q1.anim_graph!.nodes!.some((n) => n.guid === c.to && n.class.includes("Root"))
      );
      expect(poseConn).toBeDefined();

      // Disconnect the pose output on the BlendSpacePlayer
      const disc = (await blueprintTool.actions.disconnect({
        asset_path: abpPath,
        node_guid: poseConn!.from,
        pin_name: poseConn!.from_pin,
      })) as { success: boolean };
      expect(disc.success).toBe(true);

      // Verify disconnected
      const q2 = (await blueprintTool.actions.query_anim({
        asset_path: abpPath,
      })) as { success: boolean; anim_graph?: { connections?: unknown[] } };
      const poseConns = (q2.anim_graph!.connections as Array<{ to: string }>).filter((c) =>
        c.to === poseConn!.to
      );
      expect(poseConns.length).toBe(0);
    });

    it("connect works on AnimGraph nodes", async () => {
      if (!abpPath) return;

      // Get current nodes
      const q = (await blueprintTool.actions.query_anim({
        asset_path: abpPath,
      })) as {
        success: boolean;
        anim_graph?: {
          nodes?: Array<{ guid: string; class: string; pins?: Array<{ name: string; direction: string }> }>;
        };
      };

      const bsNode = q.anim_graph!.nodes!.find((n) => n.class.includes("BlendSpacePlayer"));
      const rootNode = q.anim_graph!.nodes!.find((n) => n.class.includes("Root"));
      expect(bsNode).toBeDefined();
      expect(rootNode).toBeDefined();

      // Find pose output pin on BSPlayer and input pin on Root
      const outPin = bsNode!.pins!.find((p) => p.direction === "output");
      const inPin = rootNode!.pins!.find((p) => p.direction === "input");
      expect(outPin).toBeDefined();
      expect(inPin).toBeDefined();

      // Reconnect
      const conn = (await blueprintTool.actions.connect({
        asset_path: abpPath,
        from_guid: bsNode!.guid,
        from_pin: outPin!.name,
        to_guid: rootNode!.guid,
        to_pin: inPin!.name,
      })) as { success: boolean };
      expect(conn.success).toBe(true);

      // Verify reconnected
      const q2 = (await blueprintTool.actions.query_anim({
        asset_path: abpPath,
      })) as { success: boolean; anim_graph?: { connections?: Array<{ from: string; to: string }> } };
      const reconnected = q2.anim_graph!.connections!.some(
        (c) => c.from === bsNode!.guid && c.to === rootNode!.guid
      );
      expect(reconnected).toBe(true);
    });
  });

  // ── AnimBlueprint 2D BlendSpace ──────────────────────────────────────

  describe("AnimBlueprint 2D BlendSpace", () => {
    let abp2dPath: string;
    let bs2dPath: string;

    it("setup_anim with variable_bindings wires both axes of a 2D BlendSpace", async () => {
      const abpName = uniqueName();
      const bsName = uniqueName();
      abp2dPath = `${CONTENT_PATH}/${abpName}`;
      bs2dPath = `${CONTENT_PATH}/${bsName}`;

      // Create a 2D BlendSpace via Python
      const pyResult = await runPython({
        code: `
import unreal

# Find any skeleton to use
skeleton = None
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "Skeleton"))
for a in assets:
    skeleton = unreal.load_asset(str(a.package_name) + "." + str(a.asset_name))
    if skeleton:
        break

if not skeleton:
    _write_result({"success": False, "error": "No skeleton found in project"})
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    bs_factory = unreal.BlendSpaceFactoryNew()
    bs_factory.set_editor_property("target_skeleton", skeleton)
    bs = tools.create_asset("${bsName}", "${CONTENT_PATH}", None, bs_factory)
    if not bs:
        _write_result({"success": False, "error": "Failed to create 2D BlendSpace"})
    else:
        unreal.EditorAssetLibrary.save_loaded_asset(bs)
        _write_result({
            "success": True,
            "bs_path": bs.get_path_name().split(".")[0],
            "skeleton_path": skeleton.get_path_name(),
        })
`,
      });
      expect(pyResult.success).toBe(true);
      const pyData = pyResult as { success: boolean; bs_path?: string; skeleton_path?: string };
      if (pyData.bs_path) bs2dPath = pyData.bs_path;

      // Create ABP
      const buildResult = await callArbor("BlueprintBuilder", "BuildBlueprintFromJSONString", {
        JsonString: JSON.stringify({
          name: abpName,
          parent_class: "AnimInstance",
          skeleton: pyData.skeleton_path,
        }),
        AssetPath: CONTENT_PATH,
      });
      expect(buildResult.ReturnValue).toBeTruthy();

      // Call setup_anim with variable_bindings
      const setupResult = (await blueprintTool.actions.setup_anim({
        asset_path: abp2dPath,
        blendspace_path: bs2dPath,
        variable_bindings: [
          { variable_name: "Direction", variable_axis: "X" },
          { variable_name: "Speed", variable_axis: "Y" },
        ],
      })) as {
        success: boolean;
        pose_wired: boolean;
        variable_wired: boolean;
        variables_wired?: Array<{ name: string; axis: string; wired: boolean }>;
      };

      expect(setupResult.success).toBe(true);
      expect(setupResult.pose_wired).toBe(true);
      expect(setupResult.variable_wired).toBe(true);
      expect(setupResult.variables_wired).toBeDefined();
      expect(setupResult.variables_wired!.length).toBe(2);
      expect(setupResult.variables_wired!.find((v) => v.name === "Direction")?.wired).toBe(true);
      expect(setupResult.variables_wired!.find((v) => v.name === "Speed")?.wired).toBe(true);
    });

    it("query_anim returns blendspace_axes metadata for 2D BlendSpace", async () => {
      if (!abp2dPath) return;

      const result = (await blueprintTool.actions.query_anim({
        asset_path: abp2dPath,
      })) as {
        success: boolean;
        anim_graph?: {
          nodes?: Array<{
            guid: string;
            class: string;
            blendspace_axes?: Array<{ axis: string; display_name: string; min: number; max: number }>;
          }>;
          connections?: unknown[];
        };
        variables?: Array<{ name: string }>;
      };

      expect(result.success).toBe(true);

      // Both variables should exist
      expect(result.variables!.find((v) => v.name === "Direction")).toBeDefined();
      expect(result.variables!.find((v) => v.name === "Speed")).toBeDefined();

      // BlendSpacePlayer should have blendspace_axes with 2 entries
      const bsNode = result.anim_graph!.nodes!.find((n) => n.class.includes("BlendSpacePlayer"));
      expect(bsNode).toBeDefined();
      expect(bsNode!.blendspace_axes).toBeDefined();
      expect(bsNode!.blendspace_axes!.length).toBe(2);
      expect(bsNode!.blendspace_axes![0].axis).toBe("X");
      expect(bsNode!.blendspace_axes![1].axis).toBe("Y");

      // Should have connections from both VariableGet nodes to the BlendSpacePlayer
      expect(result.anim_graph!.connections!.length).toBeGreaterThanOrEqual(3); // pose + 2 variable wires
    });
  });
});

describe.skipIf(editorRunning)("Blueprint Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
