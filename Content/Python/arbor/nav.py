"""Arbor nav — AI navigation setup helpers and EQS query builders."""

import json

import unreal

from arbor.utils import _to_vector, load_asset, make_rotator, _safe_arbor_call


def add_navmesh_volume(center=(0, 0, 0), extent=(2000, 2000, 500), label="NavMesh"):
    """Spawn a NavMeshBoundsVolume.

    The volume defines where UE5 builds navigation mesh.  Press **P**
    in the viewport to visualise the nav mesh (green = walkable).

    Args:
        center: ``(x, y, z)`` volume centre.
        extent: ``(x, y, z)`` half-extents in cm.
        label: Optional editor display name.

    Returns:
        The ``NavMeshBoundsVolume`` actor, or ``None``.
    """
    try:
        loc = _to_vector(center)
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.NavMeshBoundsVolume, loc, make_rotator(0, 0, 0)
        )
        if actor is None:
            unreal.log_error("[arbor.nav] add_navmesh_volume: spawn failed")
            return None

        if label:
            actor.set_actor_label(label)

        # Scale the volume to match the desired extent.
        # NavMeshBoundsVolume default brush is 200x200x200, so scale = extent / 100.
        ext = _to_vector(extent)
        actor.set_actor_scale3d(unreal.Vector(
            ext.x / 100.0, ext.y / 100.0, ext.z / 100.0
        ))

        unreal.log(f"[arbor.nav] add_navmesh_volume: {extent} at {center}")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.nav] add_navmesh_volume: {e}")
        return None


def build_paths():
    """Trigger Editor Build Paths to regenerate navmesh.

    Rebuilds the navigation mesh for all ``NavMeshBoundsVolume`` actors
    in the current level.  Call this after spawning or moving nav volumes
    so AI characters can navigate immediately without a manual editor Build.

    Returns:
        ``True`` if the build was triggered successfully, ``False`` on failure.
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world is None:
            unreal.log_error("[arbor.nav] build_paths: no editor world")
            return False

        nav_sys = unreal.NavigationSystemV1.get_navigation_system(world)
        if nav_sys is None:
            unreal.log_error(
                "[arbor.nav] build_paths: no NavigationSystem — "
                "ensure at least one NavMeshBoundsVolume exists"
            )
            return False

        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        unreal.log("[arbor.nav] build_paths: navmesh rebuild triggered")
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.nav] build_paths: {e}")
        return False


def setup_ai_controller(pawn_actor, behavior_tree_path, blackboard_path=None):
    """Configure AI on a pawn: assign a BehaviorTree and optional Blackboard.

    This sets the ``AIControllerClass`` property on the pawn and loads
    the BT/BB assets so they're ready when the game starts.

    Args:
        pawn_actor: The pawn ``unreal.Actor`` to configure.
        behavior_tree_path: Content path to the ``BehaviorTree`` asset.
        blackboard_path: Content path to the ``BlackboardData`` asset
                         (optional — the BT usually has one embedded).

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if pawn_actor is None:
            unreal.log_error("[arbor.nav] setup_ai_controller: pawn_actor is None")
            return False

        bt = load_asset(behavior_tree_path)
        if bt is None:
            unreal.log_error(f"[arbor.nav] setup_ai_controller: BT not found: {behavior_tree_path}")
            return False

        # Set AIControllerClass if not already set
        try:
            ai_ctrl_class = unreal.load_class(None, "/Script/AIModule.AIController")
            pawn_actor.set_editor_property("ai_controller_class", ai_ctrl_class)
        except Exception:
            unreal.log_warning("[arbor.nav] setup_ai_controller: could not set ai_controller_class")

        # Set AutoPossessAI so the AI controller spawns automatically
        try:
            pawn_actor.set_editor_property(
                "auto_possess_ai",
                unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED
            )
        except Exception:
            pass

        if blackboard_path:
            bb = load_asset(blackboard_path)
            if bb:
                unreal.log(f"[arbor.nav] setup_ai_controller: BB loaded: {blackboard_path}")

        unreal.log(f"[arbor.nav] setup_ai_controller: configured '{pawn_actor.get_actor_label()}' "
                   f"with BT '{behavior_tree_path}'")
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.nav] setup_ai_controller: {e}")
        return False


def add_eqs_context(name="EQS_TestQuery", content_path="/Game/AI"):
    """Create an EQS (Environment Query System) testing pawn.

    Spawns an ``EQSTestingPawn`` in the level for visualising EQS queries.

    Args:
        name: Label for the testing pawn.
        content_path: Unused (reserved for future EQS asset creation).

    Returns:
        The ``EQSTestingPawn`` actor, or ``None``.
    """
    try:
        try:
            eqs_class = unreal.EQSTestingPawn
        except AttributeError:
            unreal.log_warning("[arbor.nav] add_eqs_context: EQSTestingPawn class not available. "
                               "Ensure the EnvironmentQueryEditor plugin is enabled.")
            return None

        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            eqs_class, unreal.Vector(0, 0, 100), make_rotator(0, 0, 0)
        )
        if actor:
            actor.set_actor_label(name)
            unreal.log(f"[arbor.nav] add_eqs_context: spawned '{name}'")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.nav] add_eqs_context: {e}")
        return None


# ============================================================================
# EQS Query Builders
# ============================================================================

@_safe_arbor_call
def create_eqs(name, generators, tests, content_path="/Game/AI"):
    """Build an EQS query asset from a description.

    Creates an ``EnvQuery`` asset via the C++ ``EQSBuilder``.  Each
    generator becomes an Option in the query, and all tests are applied
    to every Option.

    Args:
        name: Asset name (e.g. ``"EQS_FindPatrolPoint"``).
        generators: List of generator dicts, each with ``"type"`` and
            optional ``"params"`` keys.
        tests: List of test dicts, each with ``"type"`` and optional
            ``"params"`` keys.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string (e.g. ``"/Game/AI/EQS_FindPatrolPoint"``),
        or ``None`` on failure.
    """
    eqs_json = {
        "name": name,
        "generators": generators,
        "tests": tests,
    }

    json_string = json.dumps(eqs_json)
    result = unreal.EQSBuilder.build_eqs_from_json_string(
        json_string, content_path
    )

    if result is not None:
        asset_path = f"{content_path}/{name}"
        unreal.log(f"[arbor.nav] create_eqs: built EQS query: {asset_path}")
        return asset_path
    return None


def create_eqs_find_patrol_point(name="EQS_FindPatrolPoint", radius=2000,
                                  min_distance=500, content_path="/Game/AI"):
    """Grid around querier, filter by distance and navigability.

    Args:
        name: Asset name.
        radius: Half-size of the grid in cm.
        min_distance: Minimum distance from querier.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return create_eqs(
        name,
        generators=[{
            "type": "SimpleGrid",
            "params": {
                "GridSize": float(radius),
                "SpaceBetween": 200.0,
                "GenerateAround": "Querier",
            },
        }],
        tests=[
            {
                "type": "Distance",
                "params": {
                    "DistanceTo": "Querier",
                    "TestPurpose": "FilterAndScore",
                    "FilterType": "Range",
                    "FloatValueMin": float(min_distance),
                    "FloatValueMax": float(radius),
                    "ScoringEquation": "InverseLinear",
                },
            },
            {
                "type": "PathExist",
                "params": {
                    "PathFrom": "Querier",
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": True,
                },
            },
        ],
        content_path=content_path,
    )


def create_eqs_find_cover(name="EQS_FindCover", radius=1500,
                           hide_from="AllPlayers", content_path="/Game/AI"):
    """Grid around querier, filter by no line of sight to threat, score by distance.

    Args:
        name: Asset name.
        radius: Half-size of the grid in cm.
        hide_from: Context to hide from (e.g. ``"AllPlayers"``).
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return create_eqs(
        name,
        generators=[{
            "type": "SimpleGrid",
            "params": {
                "GridSize": float(radius),
                "SpaceBetween": 150.0,
                "GenerateAround": "Querier",
            },
        }],
        tests=[
            {
                "type": "Trace",
                "params": {
                    "TraceFrom": "Querier",
                    "Context": hide_from,
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": False,
                },
            },
            {
                "type": "PathExist",
                "params": {
                    "PathFrom": "Querier",
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": True,
                },
            },
            {
                "type": "Distance",
                "params": {
                    "DistanceTo": "Querier",
                    "TestPurpose": "ScoreOnly",
                    "ScoringEquation": "InverseLinear",
                },
            },
        ],
        content_path=content_path,
    )


def create_eqs_find_flank_position(name="EQS_FindFlank", radius=1500,
                                    content_path="/Game/AI"):
    """Donut around target, score by dot product to prefer positions behind target.

    Args:
        name: Asset name.
        radius: Circle radius in cm.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return create_eqs(
        name,
        generators=[{
            "type": "OnCircle",
            "params": {
                "CircleRadius": float(radius),
                "SpaceBetween": 100.0,
                "CircleCenter": "Querier",
            },
        }],
        tests=[
            {
                "type": "Dot",
                "params": {
                    "LineA": "Querier",
                    "LineB": "Item",
                    "TestPurpose": "ScoreOnly",
                    "ScoringEquation": "Linear",
                },
            },
            {
                "type": "PathExist",
                "params": {
                    "PathFrom": "Querier",
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": True,
                },
            },
        ],
        content_path=content_path,
    )


def create_eqs_find_nearest_player(name="EQS_FindNearestPlayer",
                                    radius=5000, content_path="/Game/AI"):
    """ActorsOfClass generator for player pawns, score by inverse distance.

    Args:
        name: Asset name.
        radius: Search radius in cm.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return create_eqs(
        name,
        generators=[{
            "type": "ActorsOfClass",
            "params": {
                "SearchedActorClass": "Character",
                "SearchRadius": float(radius),
            },
        }],
        tests=[
            {
                "type": "Distance",
                "params": {
                    "DistanceTo": "Querier",
                    "TestPurpose": "FilterAndScore",
                    "ScoringEquation": "InverseLinear",
                },
            },
        ],
        content_path=content_path,
    )


def create_eqs_find_attack_position(name="EQS_FindAttackPos",
                                     min_range=200, max_range=800,
                                     content_path="/Game/AI"):
    """Ring around target, filter by LOS and distance range, score by distance.

    Args:
        name: Asset name.
        min_range: Minimum attack distance in cm.
        max_range: Maximum attack distance in cm.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    return create_eqs(
        name,
        generators=[{
            "type": "Donut",
            "params": {
                "InnerRadius": float(min_range),
                "OuterRadius": float(max_range),
                "NumberOfRings": 3,
                "PointsPerRing": 16,
            },
        }],
        tests=[
            {
                "type": "Trace",
                "params": {
                    "TraceFrom": "Querier",
                    "Context": "Querier",
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": True,
                },
            },
            {
                "type": "Distance",
                "params": {
                    "DistanceTo": "Querier",
                    "TestPurpose": "FilterAndScore",
                    "FilterType": "Range",
                    "FloatValueMin": float(min_range),
                    "FloatValueMax": float(max_range),
                    "ScoringEquation": "Linear",
                },
            },
            {
                "type": "PathExist",
                "params": {
                    "PathFrom": "Querier",
                    "TestPurpose": "FilterOnly",
                    "BoolMatch": True,
                },
            },
        ],
        content_path=content_path,
    )


# ============================================================================
# EQS Query / Inspect
# ============================================================================

@_safe_arbor_call
def query_eqs(asset_path):
    """Query an EQS asset to inspect its generators, tests, and params.

    Args:
        asset_path: Content path like ``"/Game/AI/EQS_GhostPatrol"``.

    Returns:
        Dict with ``name``, ``generators`` (list of type + params),
        ``tests`` (list of type + params), or ``None`` on failure.
    """
    result_json = unreal.EQSBuilder.query_eqs(asset_path)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Granular EQS Editing
# ============================================================================

@_safe_arbor_call
def add_eqs_generator(asset_path, option_index, generator_spec):
    """Add a generator (option) to an EQS query.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Insertion index among options (-1 to append).
        generator_spec: Dict with ``"type"`` and optional ``"params"``.

    Returns:
        Dict ``{"success": bool, "option_index": int}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.add_eqs_generator(
        asset_path, option_index, json.dumps(generator_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_eqs_generator(asset_path, option_index):
    """Remove a generator (option) from an EQS query by index.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Index of the option to remove.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.remove_eqs_generator(
        asset_path, option_index)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_eqs_generator_params(asset_path, option_index, params):
    """Modify parameters on a generator in an EQS query.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Index of the option whose generator to modify.
        params: Dict of parameter names to values.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.set_eqs_generator_params(
        asset_path, option_index, json.dumps(params))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def add_eqs_test(asset_path, option_index, test_index, test_spec):
    """Add a test to an EQS query.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Option to add the test to (-1 for all options).
        test_index: Insertion index among tests (-1 to append).
        test_spec: Dict with ``"type"`` and optional ``"params"``.

    Returns:
        Dict ``{"success": bool, "test_index": int}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.add_eqs_test(
        asset_path, option_index, test_index, json.dumps(test_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_eqs_test(asset_path, option_index, test_index):
    """Remove a test from an EQS query by index.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Option to remove the test from (-1 for all options).
        test_index: Index of the test to remove.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.remove_eqs_test(
        asset_path, option_index, test_index)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_eqs_test_params(asset_path, option_index, test_index, params):
    """Modify parameters on a test in an EQS query.

    Args:
        asset_path: Content path to the EQS asset.
        option_index: Option containing the test (-1 for all options).
        test_index: Index of the test to modify.
        params: Dict of parameter names to values.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.EQSBuilder.set_eqs_test_params(
        asset_path, option_index, test_index, json.dumps(params))
    if result_json:
        return json.loads(result_json)
    return None