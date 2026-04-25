"""Arbor pcg -- Procedural Content Generation graph builders and helpers.

Use this module to create PCG graph assets that procedurally place meshes,
foliage, and other content in the level.  PCG graphs execute on actors via
UPCGComponent and produce instanced static meshes at runtime.

For fewer than ~50 unique actors, prefer ``arbor.scatter.scatter_meshes()``.
For pure foliage instances, prefer ``arbor.foliage``.  Use PCG when you need:

- GPU-instanced scattering of hundreds/thousands of meshes
- Landscape-aware placement (respect slope, height, layers)
- Procedural density control and filtering
- Reusable generation graphs (subgraphs)
"""

import json

import unreal

from arbor.utils import _safe_arbor_call


# ============================================================================
# Full Graph Build
# ============================================================================

@_safe_arbor_call
def build_pcg_graph(pcg_json, content_path="/Game/PCG"):
    """Build a PCG graph asset from a JSON dict.

    Calls ``UPCGBuilder::BuildPCGGraphFromJSONString`` directly
    via Python reflection.

    Args:
        pcg_json: Dict matching the PCG builder JSON schema.
        content_path: UE content path for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    json_string = json.dumps(pcg_json)
    result = unreal.PCGBuilder.build_pcg_graph_from_json_string(
        json_string, content_path
    )

    name = pcg_json.get("name", "")
    if result is not None:
        asset_path = f"{content_path}/{name}"
        unreal.log(f"[arbor.pcg] build_pcg_graph: built PCG graph: {asset_path}")
        return asset_path
    return None


# ============================================================================
# Query
# ============================================================================

@_safe_arbor_call
def query_pcg_graph(asset_path):
    """Query an existing PCG graph to inspect nodes, connections, and params.

    Args:
        asset_path: Content path like ``"/Game/PCG/PCG_FoliageScatter"``.

    Returns:
        Dict with ``name``, ``nodes`` (list of type + id + params),
        and pin connection info, or ``None`` on failure.
    """
    result_json = unreal.PCGBuilder.query_pcg_graph(asset_path)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Granular Editing
# ============================================================================

@_safe_arbor_call
def add_pcg_node(asset_path, node_spec):
    """Add a node to a PCG graph.

    Args:
        asset_path: Content path to the PCG graph asset.
        node_spec: Dict with ``"type"`` and optional ``"params"``
            and ``"id"`` (display name).

    Returns:
        Dict ``{"success": bool, "node_id": str}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.add_pcg_node(
        asset_path, json.dumps(node_spec))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def remove_pcg_node(asset_path, node_id):
    """Remove a node from a PCG graph by ID.

    Args:
        asset_path: Content path to the PCG graph asset.
        node_id: Node UID string (from ``query_pcg_graph``) or title.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.remove_pcg_node(asset_path, str(node_id))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def set_pcg_node_params(asset_path, node_id, params):
    """Modify parameters on a PCG graph node.

    Args:
        asset_path: Content path to the PCG graph asset.
        node_id: Node UID string or title.
        params: Dict of parameter names to values.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.set_pcg_node_params(
        asset_path, str(node_id), json.dumps(params))
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def connect_pcg_pins(asset_path, from_node_id, from_pin, to_node_id, to_pin):
    """Wire two PCG nodes together by pin labels.

    Args:
        asset_path: Content path to the PCG graph asset.
        from_node_id: Source node UID or title.
        from_pin: Source pin label (e.g. ``"Out"``).
        to_node_id: Target node UID or title.
        to_pin: Target pin label (e.g. ``"In"``).

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.connect_pcg_pins(
        asset_path, str(from_node_id), from_pin,
        str(to_node_id), to_pin)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def disconnect_pcg_pin(asset_path, node_id, pin_label):
    """Disconnect all edges on a specific pin.

    Args:
        asset_path: Content path to the PCG graph asset.
        node_id: Node UID string or title.
        pin_label: Pin label to disconnect.

    Returns:
        Dict ``{"success": bool}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.disconnect_pcg_pin(
        asset_path, str(node_id), pin_label)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Execution
# ============================================================================

@_safe_arbor_call
def execute_pcg_graph(graph_path, actor_label):
    """Execute a PCG graph on a specific actor.

    Finds or adds a UPCGComponent, sets the graph, and generates.

    Args:
        graph_path: Content path to the PCG graph asset.
        actor_label: Label of the target actor in the level.

    Returns:
        Dict ``{"success": bool, "actor": str}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.execute_pcg_on_actor(
        graph_path, actor_label)
    if result_json:
        return json.loads(result_json)
    return None


@_safe_arbor_call
def add_pcg_component(actor_label, graph_path):
    """Add a PCGComponent to an actor and assign a graph.

    Does not trigger generation — call ``execute_pcg_graph`` for that.

    Args:
        actor_label: Label of the target actor.
        graph_path: Content path to the PCG graph asset.

    Returns:
        Dict ``{"success": bool, "component": str}`` or ``None``.
    """
    result_json = unreal.PCGBuilder.add_pcg_component_to_actor(
        actor_label, graph_path)
    if result_json:
        return json.loads(result_json)
    return None


# ============================================================================
# Preset Graphs
# ============================================================================

def create_foliage_scatter(name="PCG_FoliageScatter", mesh_paths=None,
                           density=0.5, scale_min=0.8, scale_max=1.2,
                           random_yaw=True, content_path="/Game/PCG"):
    """Create a PCG graph that scatters foliage meshes on landscape surfaces.

    Pipeline: Input → SurfaceSampler → TransformPoints → StaticMeshSpawner

    Args:
        name: Asset name.
        mesh_paths: List of content paths to StaticMesh assets.
            Uses engine sphere if ``None``.
        density: Points per squared meter (0.1 = sparse, 1.0 = dense).
        scale_min: Minimum uniform scale.
        scale_max: Maximum uniform scale.
        random_yaw: Randomize Z rotation (0-360 degrees).
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    if mesh_paths is None:
        mesh_paths = ["/Engine/BasicShapes/Sphere.Sphere"]

    nodes = [
        {
            "id": "sampler",
            "type": "SurfaceSampler",
            "params": {
                "PointsPerSquaredMeter": float(density),
            },
        },
        {
            "id": "transform",
            "type": "TransformPoints",
            "params": {
                "OffsetMin": {"X": 0, "Y": 0, "Z": 0},
                "OffsetMax": {"X": 0, "Y": 0, "Z": 0},
                "RotationMin": {"X": 0, "Y": 0, "Z": 0},
                "RotationMax": {"X": 0, "Y": 0, "Z": 360.0 if random_yaw else 0},
                "ScaleMin": {"X": scale_min, "Y": scale_min, "Z": scale_min},
                "ScaleMax": {"X": scale_max, "Y": scale_max, "Z": scale_max},
            },
        },
        {
            "id": "spawner",
            "type": "StaticMeshSpawner",
            "params": {},
        },
    ]

    connections = [
        {"from": "input", "from_pin": "Out", "to": "sampler", "to_pin": "In"},
        {"from": "sampler", "from_pin": "Out", "to": "transform", "to_pin": "In"},
        {"from": "transform", "from_pin": "Out", "to": "spawner", "to_pin": "In"},
    ]

    return build_pcg_graph({
        "name": name,
        "nodes": nodes,
        "connections": connections,
    }, content_path)


def create_rock_scatter(name="PCG_RockScatter", mesh_paths=None,
                         density=0.1, scale_min=0.5, scale_max=2.0,
                         content_path="/Game/PCG"):
    """Create a PCG graph for rock/boulder placement.

    Pipeline: Input → SurfaceSampler → DensityFilter → TransformPoints
    → StaticMeshSpawner

    Uses density filtering to create natural-looking sparse placement.

    Args:
        name: Asset name.
        mesh_paths: List of rock StaticMesh content paths.
        density: Points per squared meter.
        scale_min: Minimum uniform scale.
        scale_max: Maximum uniform scale.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    if mesh_paths is None:
        mesh_paths = ["/Engine/BasicShapes/Cube.Cube"]

    nodes = [
        {
            "id": "sampler",
            "type": "SurfaceSampler",
            "params": {
                "PointsPerSquaredMeter": float(density),
            },
        },
        {
            "id": "density_filter",
            "type": "DensityFilter",
            "params": {
                "LowerBound": 0.3,
                "UpperBound": 1.0,
            },
        },
        {
            "id": "self_prune",
            "type": "SelfPruning",
            "params": {},
        },
        {
            "id": "transform",
            "type": "TransformPoints",
            "params": {
                "RotationMin": {"X": 0, "Y": 0, "Z": 0},
                "RotationMax": {"X": 5, "Y": 5, "Z": 360},
                "ScaleMin": {"X": scale_min, "Y": scale_min, "Z": scale_min},
                "ScaleMax": {"X": scale_max, "Y": scale_max, "Z": scale_max},
            },
        },
        {
            "id": "spawner",
            "type": "StaticMeshSpawner",
            "params": {},
        },
    ]

    connections = [
        {"from": "input", "from_pin": "Out", "to": "sampler", "to_pin": "In"},
        {"from": "sampler", "from_pin": "Out", "to": "density_filter", "to_pin": "In"},
        {"from": "density_filter", "from_pin": "Out", "to": "self_prune", "to_pin": "In"},
        {"from": "self_prune", "from_pin": "Out", "to": "transform", "to_pin": "In"},
        {"from": "transform", "from_pin": "Out", "to": "spawner", "to_pin": "In"},
    ]

    return build_pcg_graph({
        "name": name,
        "nodes": nodes,
        "connections": connections,
    }, content_path)


def create_debris_scatter(name="PCG_DebrisScatter", mesh_paths=None,
                           density=0.3, scale_min=0.3, scale_max=1.0,
                           content_path="/Game/PCG"):
    """Create a PCG graph for small debris placement.

    Pipeline: Input → SurfaceSampler → SelfPruning → TransformPoints
    → StaticMeshSpawner

    Places small objects with self-pruning to avoid overlaps.

    Args:
        name: Asset name.
        mesh_paths: List of debris StaticMesh content paths.
        density: Points per squared meter.
        scale_min: Minimum uniform scale.
        scale_max: Maximum uniform scale.
        content_path: Content folder for the output asset.

    Returns:
        Asset path string, or ``None`` on failure.
    """
    if mesh_paths is None:
        mesh_paths = ["/Engine/BasicShapes/Cone.Cone"]

    nodes = [
        {
            "id": "sampler",
            "type": "SurfaceSampler",
            "params": {
                "PointsPerSquaredMeter": float(density),
            },
        },
        {
            "id": "self_prune",
            "type": "SelfPruning",
            "params": {},
        },
        {
            "id": "transform",
            "type": "TransformPoints",
            "params": {
                "RotationMin": {"X": 0, "Y": 0, "Z": 0},
                "RotationMax": {"X": 15, "Y": 15, "Z": 360},
                "ScaleMin": {"X": scale_min, "Y": scale_min, "Z": scale_min},
                "ScaleMax": {"X": scale_max, "Y": scale_max, "Z": scale_max},
            },
        },
        {
            "id": "spawner",
            "type": "StaticMeshSpawner",
            "params": {},
        },
    ]

    connections = [
        {"from": "input", "from_pin": "Out", "to": "sampler", "to_pin": "In"},
        {"from": "sampler", "from_pin": "Out", "to": "self_prune", "to_pin": "In"},
        {"from": "self_prune", "from_pin": "Out", "to": "transform", "to_pin": "In"},
        {"from": "transform", "from_pin": "Out", "to": "spawner", "to_pin": "In"},
    ]

    return build_pcg_graph({
        "name": name,
        "nodes": nodes,
        "connections": connections,
    }, content_path)


def scatter_on_landscape(mesh_paths, density=0.5, graph_name="PCG_AutoScatter",
                          content_path="/Game/PCG"):
    """Create a PCG graph and execute it on the first landscape actor.

    Convenience function that:

    1. Creates a foliage scatter PCG graph
    2. Finds the landscape actor in the level
    3. Adds PCGComponent and executes

    Args:
        mesh_paths: List of StaticMesh content paths.
        density: Points per squared meter.
        graph_name: Asset name for the graph.
        content_path: Content folder.

    Returns:
        Dict with ``graph_path``, ``actor``, ``success``, or ``None``.
    """
    try:
        # 1. Create the graph
        graph_path = create_foliage_scatter(
            name=graph_name,
            mesh_paths=mesh_paths,
            density=density,
            content_path=content_path,
        )
        if not graph_path:
            unreal.log_error("[arbor.pcg] scatter_on_landscape: failed to create graph")
            return None

        # 2. Find landscape actor
        world = unreal.EditorLevelLibrary.get_editor_world()
        if not world:
            unreal.log_error("[arbor.pcg] scatter_on_landscape: no editor world")
            return None

        landscape = None
        for actor in unreal.EditorLevelLibrary.get_all_level_actors():
            if isinstance(actor, unreal.Landscape):
                landscape = actor
                break

        if not landscape:
            unreal.log_error("[arbor.pcg] scatter_on_landscape: no Landscape found in level")
            return None

        actor_label = landscape.get_actor_label()

        # 3. Execute
        result = execute_pcg_graph(graph_path, actor_label)
        if result and result.get("success"):
            return {
                "success": True,
                "graph_path": graph_path,
                "actor": actor_label,
            }
        else:
            error = result.get("error", "Unknown") if result else "execute returned None"
            unreal.log_error(f"[arbor.pcg] scatter_on_landscape: execution failed: {error}")
            return None

    except Exception as e:
        unreal.log_error(f"[arbor.pcg] scatter_on_landscape: {e}")
        return None
