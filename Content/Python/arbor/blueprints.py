"""Arbor blueprints — convenience helpers for creating Blueprint assets.

Provides direct Python wrappers for the Arbor C++ builders. The preferred
``build_bt``, ``build_bp``, ``build_eqs``, and ``build_vfx`` functions pass
JSON strings directly to C++ via reflection — no temp files or file paths
needed.  For the MCP console-command path, base64-encoded variants are also
available (``Arbor.BuildBTFromString``, etc.).
"""

import json

import unreal

from arbor.utils import write_result, _safe_arbor_call, _to_vector, _to_rotator


def _write_and_execute(bp_json, content_path):
    """Build a Blueprint asset from a JSON dict.

    Calls ``UBlueprintBuilder::BuildBlueprintFromJSONString`` directly via
    Python reflection — no temp files or file paths needed.

    Args:
        bp_json: Dict matching the Blueprint builder JSON schema.
        content_path: UE content path for the output asset.

    Returns:
        Expected asset path string, or ``None`` on failure.
    """
    try:
        name = bp_json.get("name", "BP_Unnamed")
        json_string = json.dumps(bp_json)

        result = unreal.BlueprintBuilder.build_blueprint_from_json_string(
            json_string, content_path
        )

        asset_path = f"{content_path}/{name}"
        if result is not None:
            unreal.log(f"[arbor.blueprints] built Blueprint: {asset_path}")
            return asset_path
        else:
            unreal.log_error(f"[arbor.blueprints] BlueprintBuilder returned None for {name}")
            return None
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] _write_and_execute: {e}")
        return None


def build_behavior_tree(bt_json, content_path="/Game/AI"):
    """Build BehaviorTree + Blackboard assets from a JSON dict.

    Calls ``UBehaviorTreeBuilder::BuildBehaviorTreeFromJSONString`` directly
    via Python reflection — no temp files or file paths needed.

    Args:
        bt_json: Dict matching the Arbor BT JSON schema (name, root, blackboard).
        content_path: UE content path for the output assets.

    Returns:
        Dict ``{"bt": asset_path, "bb": asset_path}`` or ``None`` on failure.
    """
    try:
        # Normalize key aliases so callers can use either form
        if "tree" in bt_json and "root" not in bt_json:
            bt_json = dict(bt_json, root=bt_json.pop("tree") if isinstance(bt_json, dict) else bt_json["tree"])
        if "blackboard_keys" in bt_json and "blackboard" not in bt_json:
            keys = bt_json["blackboard_keys"]
            raw_name = bt_json.get("name", "BT_Unnamed")
            base = raw_name[3:] if raw_name.startswith("BT_") else raw_name
            bt_json = dict(bt_json, blackboard={"name": f"BB_{base}", "keys": keys})

        name = bt_json.get("name", "BT_Unnamed")
        json_string = json.dumps(bt_json)

        result = unreal.BehaviorTreeBuilder.build_behavior_tree_from_json_string(
            json_string, content_path
        )

        if result is not None:
            bt_path = f"{content_path}/{name}"
            bb_name = bt_json.get("blackboard", {}).get("name", f"BB_{name[3:]}" if name.startswith("BT_") else f"BB_{name}")
            bb_path = f"{content_path}/{bb_name}"
            unreal.log(f"[arbor.blueprints] built BT: {bt_path}, BB: {bb_path}")
            return {"bt": bt_path, "bb": bb_path}
        else:
            unreal.log_error(f"[arbor.blueprints] BehaviorTreeBuilder returned None for {name}")
            return None
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] build_behavior_tree: {e}")
        return None


def create_character_bp(name, content_path="/Game/Blueprints",
                        mesh_path=None, anim_bp_path=None,
                        ai_controller_path=None,
                        auto_possess="PlacedInWorldOrSpawned",
                        variables=None):
    """Create or update a Character Blueprint with optional skeletal mesh and AI setup.

    Safe to re-call on an existing Blueprint — only sections with actual
    content are rebuilt; omitted sections are preserved.

    Args:
        name: Blueprint asset name (e.g. ``"BP_Wolf"``).
        content_path: Content folder for the asset.
        mesh_path: Content path to a SkeletalMesh asset.
        anim_bp_path: Content path to an AnimBlueprint class.
        ai_controller_path: Content path to an AI Controller Blueprint.
        auto_possess: Auto-possess mode string (default ``"PlacedInWorldOrSpawned"``).
        variables: List of variable dicts ``[{"name": str, "type": str, "default": ...}]``.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    try:
        bp_json = {
            "name": name,
            "parent_class": "Character",
        }

        # Only include sections that have content — absent keys are
        # preserved by the C++ builder, so re-calling this function
        # won't wipe existing components/variables/defaults.
        if mesh_path:
            mesh_comp = {
                "name": "Mesh",
                "type": "SkeletalMeshComponent",
                "properties": {
                    "SkeletalMeshAsset": mesh_path,
                    "RelativeLocation": {"X": 0, "Y": 0, "Z": -88},
                    "RelativeRotation": {"Pitch": 0, "Yaw": -90, "Roll": 0},
                },
            }
            if anim_bp_path:
                mesh_comp["properties"]["AnimClass"] = anim_bp_path
            bp_json["components"] = [mesh_comp]

        if variables:
            bp_json["variables"] = variables

        defaults = {}
        if ai_controller_path:
            defaults["AIControllerClass"] = ai_controller_path
        if auto_possess:
            defaults["AutoPossessAI"] = auto_possess
        if defaults:
            bp_json["defaults"] = defaults

        return _write_and_execute(bp_json, content_path)
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] create_character_bp: {e}")
        return None


def create_ai_controller_bp(name, content_path="/Game/AI",
                             perception_config=None):
    """Create or update an AIController Blueprint, optionally wiring perception.

    Parents the Blueprint at engine-stock ``AIController``. Safe to re-call
    on an existing Blueprint — only sections with actual content are
    rebuilt; omitted sections are preserved.

    To auto-run a BehaviorTree from this controller, wire the event graph
    separately with :func:`add_node` / :func:`set_pin` / :func:`connect`:
    create a ``ReceivePossess`` event, a ``UseBlackboard`` CallFunction
    (if using a blackboard), and a ``RunBehaviorTree`` CallFunction, then
    connect them in sequence.

    Args:
        name: Blueprint asset name (e.g. ``"BP_WolfAI"``).
        content_path: Content folder for the asset.
        perception_config: List of sense config dicts, e.g.
            ``[{"type": "AISense_Sight", "SightRadius": 3000}]``.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    try:
        bp_json = {
            "name": name,
            "parent_class": "AIController",
        }

        # Only include sections that have content — absent keys are
        # preserved by the C++ builder, so re-calling this function
        # won't wipe existing components/variables/event graph.
        if perception_config:
            bp_json["components"] = [{
                "name": "AIPerception",
                "type": "AIPerceptionComponent",
                "properties": {
                    "SensesConfig": perception_config,
                },
            }]

        return _write_and_execute(bp_json, content_path)
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] create_ai_controller_bp: {e}")
        return None


def create_game_mode_bp(name, content_path="/Game/Blueprints",
                        default_pawn_class=None,
                        player_controller_class=None):
    """Create or update a GameMode Blueprint with optional pawn and controller classes.

    Safe to re-call on an existing Blueprint — only sections with actual
    content are rebuilt; omitted sections are preserved.

    Args:
        name: Blueprint asset name (e.g. ``"BP_GameMode"``).
        content_path: Content folder for the asset.
        default_pawn_class: Content path to the default pawn Blueprint.
        player_controller_class: Content path to the player controller Blueprint.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    try:
        bp_json = {
            "name": name,
            "parent_class": "GameMode",
        }

        defaults = {}
        if default_pawn_class:
            defaults["DefaultPawnClass"] = default_pawn_class
        if player_controller_class:
            defaults["PlayerControllerClass"] = player_controller_class
        if defaults:
            bp_json["defaults"] = defaults

        return _write_and_execute(bp_json, content_path)
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] create_game_mode_bp: {e}")
        return None


# =========================================================================
# Shorthand wrappers — preferred entry points for MCP and scripting.
# These pass JSON directly to C++ (no temp files, no path issues).
# =========================================================================

def build_bt(plan_dict, asset_path="/Game/AI"):
    """Build a BehaviorTree + Blackboard from a JSON dict.

    Convenience alias for :func:`build_behavior_tree`.

    Args:
        plan_dict: Dict matching the Arbor BT JSON schema.
        asset_path: UE content path for the output assets.

    Returns:
        Dict ``{"bt": path, "bb": path}`` or ``None`` on failure.
    """
    return build_behavior_tree(plan_dict, asset_path)


def build_bp(bp_dict, asset_path="/Game/Blueprints"):
    """Build a Blueprint from a JSON dict.

    Args:
        bp_dict: Dict matching the Arbor Blueprint JSON schema.  Supports
            ``name``, ``parent_class``, ``components``, ``variables``,
            ``defaults``, ``timelines``, and ``event_graph`` keys.
            The ``event_graph`` key holds ``{"nodes": [...], "connections": [...]}``
            for creating visual-script event graph logic.  See CLAUDE.md for
            the full schema and examples.
        asset_path: UE content path for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return _write_and_execute(bp_dict, asset_path)


def build_eqs(eqs_dict, asset_path="/Game/AI"):
    """Build an EQS query from a JSON dict.

    Calls ``UEQSBuilder::BuildEQSFromJSONString`` directly via Python
    reflection — no temp files or file paths needed.

    Args:
        eqs_dict: Dict matching the Arbor EQS JSON schema.
        asset_path: UE content path for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    try:
        name = eqs_dict.get("name", "EQS_Unnamed")
        json_string = json.dumps(eqs_dict)

        result = unreal.EQSBuilder.build_eqs_from_json_string(
            json_string, asset_path
        )

        asset_path_full = f"{asset_path}/{name}"
        if result is not None:
            unreal.log(f"[arbor.blueprints] built EQS: {asset_path_full}")
            return asset_path_full
        else:
            unreal.log_error(f"[arbor.blueprints] EQSBuilder returned None for {name}")
            return None
    except Exception as e:
        unreal.log_error(f"[arbor.blueprints] build_eqs: {e}")
        return None


# EQS query/editing aliases — delegate to arbor.nav
from arbor.nav import (  # noqa: E402
    query_eqs,
    add_eqs_generator,
    remove_eqs_generator,
    set_eqs_generator_params,
    add_eqs_test,
    remove_eqs_test,
    set_eqs_test_params,
)


# ============================================================================
# Granular Blueprint Editing
# ============================================================================

@_safe_arbor_call
def query_blueprint(asset_path):
    """Query full Blueprint structure including event graph nodes and connections.

    Args:
        asset_path: Content path like "/Game/BP/BP_Wolf".

    Returns:
        Dict with name, parent_class, components, variables, event_graph
        (nodes with guids + connections). Each node has a 'guid' for use
        with granular editing operations.
    """
    result_json = unreal.BlueprintBuilder.query_blueprint(asset_path)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def add_node(asset_path, node_spec):
    """Add a single node to a Blueprint's event graph.

    Args:
        asset_path: Content path to the Blueprint.
        node_spec: Dict matching node JSON schema (type, event/function/variable,
            defaults, optional position as [x, y]).
            Supported types include: Event, ComponentEvent (alias:
            ComponentBoundEvent), CallFunction, VariableGet, VariableSet,
            Branch, Timeline, CastTo, or any UK2Node subclass name.
            ComponentEvent/ComponentBoundEvent accepts both 'event' and
            'delegate' as the delegate property field name.

    Returns:
        Dict with 'success' and 'node_guid', or error dict on failure.
    """
    result_json = unreal.BlueprintBuilder.add_event_graph_node(
        asset_path, json.dumps(node_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_node(asset_path, node_guid):
    """Remove a node from a Blueprint's event graph by GUID.

    Also removes hidden component reference nodes if they only link to
    the removed node.

    Args:
        asset_path: Content path to the Blueprint.
        node_guid: GUID string from query_blueprint output.

    Returns:
        Dict with 'success' and 'removed_count', or error dict on failure.
    """
    result_json = unreal.BlueprintBuilder.remove_event_graph_node(
        asset_path, node_guid)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_pin_default(asset_path, node_guid, pin_name, value):
    """Set the default value of a pin on a Blueprint event graph node.

    Args:
        asset_path: Content path to the Blueprint.
        node_guid: GUID string of the target node.
        pin_name: Name of the pin (e.g. "InString", "Duration").
        value: Default value as string. For Vector use "X,Y,Z" format.

    Returns:
        Dict with 'success', or error dict on failure.
    """
    result_json = unreal.BlueprintBuilder.set_pin_default(
        asset_path, node_guid, pin_name, str(value))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def connect_pins(asset_path, from_guid, from_pin, to_guid, to_pin):
    """Connect two pins across nodes in a Blueprint event graph.

    Args:
        asset_path: Content path to the Blueprint.
        from_guid: GUID of the source node.
        from_pin: Name of the output pin (e.g. "Then", "ReturnValue").
        to_guid: GUID of the target node.
        to_pin: Name of the input pin (e.g. "Execute", "InString").

    Returns:
        Dict with 'success', or error dict on failure.
    """
    result_json = unreal.BlueprintBuilder.connect_pins(
        asset_path, from_guid, from_pin, to_guid, to_pin)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def disconnect_pin(asset_path, node_guid, pin_name):
    """Disconnect all links from a pin on a Blueprint event graph node."""
    result_json = unreal.BlueprintBuilder.disconnect_pin(
        asset_path, node_guid, pin_name)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def compile_blueprint(asset_path):
    """Compile and save a Blueprint.

    Args:
        asset_path: Content path to the Blueprint (e.g. "/Game/BP/BP_Wolf").

    Returns:
        Dict with:
        - success (bool): False if the Blueprint has compile errors.
        - compile_errors (list[str]): Error messages from the UE5 compiler.
        - compile_warnings (list[str]): Warning messages from the UE5 compiler.
    """
    result_json = unreal.BlueprintBuilder.compile_and_save_blueprint(
        asset_path)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Pin Introspection
# ============================================================================

@_safe_arbor_call
def get_node_pins(node_spec, context_blueprint=None):
    """Get exact internal pin names for a Blueprint node type.

    Creates a temporary node matching the spec, reads its pins, then
    discards it.  No assets are modified.

    Args:
        node_spec: Dict matching the add_node JSON schema.  Examples:
            {"type": "Branch"}
            {"type": "CallFunction", "function": "PrintString"}
            {"type": "UK2Node_ExecutionSequence"}
            {"type": "CastTo", "class": "AIController"}
        context_blueprint: Optional content path (e.g. "/Game/BP/BP_Wolf")
            for node types that need Blueprint context (CallFunction with
            target, VariableGet/Set, ComponentEvent, Timeline).

    Returns:
        Dict with 'success', 'node_type', and 'pins' list.  Each pin has:
        - name: Internal pin name (use this with connect_pins)
        - direction: "input" or "output"
        - category: Pin type category (exec, bool, float, string, object, etc.)
        - default: Default value if set (optional)
    """
    result_json = unreal.BlueprintBuilder.get_node_pins(
        json.dumps(node_spec), context_blueprint or "")
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Granular Component Editing
# ============================================================================

@_safe_arbor_call
def add_component(asset_path, component_spec):
    """Add a component to a Blueprint's Simple Construction Script."""
    result_json = unreal.BlueprintBuilder.add_scs_component(
        asset_path, json.dumps(component_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_component(asset_path, component_name):
    """Remove a component from a Blueprint's Simple Construction Script."""
    result_json = unreal.BlueprintBuilder.remove_scs_component(
        asset_path, component_name)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_component_property(asset_path, component_name, properties):
    """Set properties on an existing Blueprint component."""
    result_json = unreal.BlueprintBuilder.set_component_property(
        asset_path, component_name, json.dumps(properties))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_component_transform(asset_path, component_name, location=None, rotation=None, scale=None):
    """Set the relative transform (location, rotation, scale) of a Blueprint component.

    Args:
        asset_path: Content path to the Blueprint asset.
        component_name: Name of the component to modify.
        location: Dict or tuple {x, y, z} in cm. Optional.
        rotation: Dict or tuple {pitch, yaw, roll} in degrees. Optional.
        scale: Dict or tuple {x, y, z}. Optional.
    """
    props = {}
    if location is not None:
        v = _to_vector(location) if not isinstance(location, dict) else location
        if isinstance(v, dict):
            props["RelativeLocation"] = {"X": v.get("x", v.get("X", 0)),
                                         "Y": v.get("y", v.get("Y", 0)),
                                         "Z": v.get("z", v.get("Z", 0))}
        else:
            props["RelativeLocation"] = {"X": v.x, "Y": v.y, "Z": v.z}
    if rotation is not None:
        r = _to_rotator(rotation) if not isinstance(rotation, dict) else rotation
        if isinstance(r, dict):
            props["RelativeRotation"] = {"Pitch": r.get("pitch", r.get("Pitch", 0)),
                                         "Yaw": r.get("yaw", r.get("Yaw", 0)),
                                         "Roll": r.get("roll", r.get("Roll", 0))}
        else:
            props["RelativeRotation"] = {"Pitch": r.pitch, "Yaw": r.yaw, "Roll": r.roll}
    if scale is not None:
        s = _to_vector(scale) if not isinstance(scale, dict) else scale
        if isinstance(s, dict):
            props["RelativeScale3D"] = {"X": s.get("x", s.get("X", 1)),
                                        "Y": s.get("y", s.get("Y", 1)),
                                        "Z": s.get("z", s.get("Z", 1))}
        else:
            props["RelativeScale3D"] = {"X": s.x, "Y": s.y, "Z": s.z}
    if not props:
        return {"success": False, "error": "At least one of location, rotation, or scale required"}
    return set_component_property(asset_path, component_name, props)


# ============================================================================
# Granular BehaviorTree Editing
# ============================================================================

@_safe_arbor_call
def query_behavior_tree(asset_path):
    """Query full BehaviorTree structure including nodes with tree paths."""
    result_json = unreal.BehaviorTreeBuilder.query_behavior_tree(asset_path)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def add_bt_node(asset_path, parent_path, child_index, node_spec):
    """Add a node to a BehaviorTree."""
    result_json = unreal.BehaviorTreeBuilder.add_bt_node(
        asset_path, parent_path, child_index, json.dumps(node_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_bt_node(asset_path, node_path):
    """Remove a node from a BehaviorTree by tree path."""
    result_json = unreal.BehaviorTreeBuilder.remove_bt_node(
        asset_path, node_path)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_bt_node_params(asset_path, node_path, params):
    """Set parameters on a BehaviorTree node via reflection."""
    result_json = unreal.BehaviorTreeBuilder.set_bt_node_params(
        asset_path, node_path, json.dumps(params))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def layout_behavior_tree(asset_path):
    """Auto-layout a BehaviorTree's editor graph for clean visual display."""
    result_json = unreal.BehaviorTreeBuilder.layout_behavior_tree(
        asset_path)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# AnimGraph Editing
# ============================================================================

@_safe_arbor_call
def setup_locomotion_graph(asset_path, blendspace_path,
                           variable_name="Speed", variable_axis="X"):
    """Set up a locomotion AnimGraph: BlendSpace -> OutputPose."""
    params = {
        "blendspace_path": blendspace_path,
        "variable_name": variable_name,
        "variable_axis": variable_axis,
    }
    result_json = unreal.AnimBlueprintBuilder.setup_locomotion_graph(
        asset_path, json.dumps(params))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def query_anim_graph(asset_path):
    """Query AnimBlueprint structure including AnimGraph nodes.

    Args:
        asset_path: Content path like "/Game/AI/ABP_Ghost".

    Returns:
        Dict with 'name', 'parent_class', 'skeleton', 'variables',
        'anim_graph' (containing 'nodes' and 'connections'). None on failure.
    """
    result_json = unreal.AnimBlueprintBuilder.query_anim_graph(asset_path)
    if result_json:
        return json.loads(result_json)
    return None
