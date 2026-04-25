"""Arbor environment — graph-based environment building.

Orchestrates the anchor-based environment pipeline: validate a semantic
graph, resolve it to world transforms via the C++ layout solver, and
spawn/despawn the resulting actors.

Usage::

    import arbor.environment as env

    graph = {
        "id": "test_layout",
        "nodes": {
            "floor": {"asset_path": "/Game/Fab/SM_Floor", "asset_type": "floor"},
            "wall":  {"asset_path": "/Game/Fab/SM_Wall",  "asset_type": "wall"},
            "prop":  {"asset_path": "/Game/Fab/SM_Barrel", "asset_type": "prop"},
        },
        "edges": [
            {"from": {"node": "floor", "anchor": "edge_north"},
             "to":   {"node": "wall",  "anchor": "snap_base"},
             "relationship": "adjacent"},
            {"from": {"node": "wall",  "anchor": "face_south"},
             "to":   {"node": "prop",  "anchor": "snap_base"},
             "relationship": "facing", "params": {"gap": 100}},
        ]
    }

    result = env.build_environment(graph)
    # ... inspect in editor ...
    env.clear_environment("test_layout")
"""

import json
import unreal


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _call_cpp(result_json):
    """Parse C++ JSON result, log errors, return parsed dict."""
    result = json.loads(result_json)
    if not result.get("success"):
        error = result.get("error", "Unknown error")
        unreal.log_error(f"[arbor.environment] {error}")
    return result


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def validate_graph(graph):
    """Validate an environment graph for structural correctness.

    Checks:
    - Required fields (id, nodes)
    - Each node has asset_path
    - Each edge references existing nodes
    - No self-edges
    - No disconnected subgraphs (warning only)

    Args:
        graph: Environment graph dict.

    Returns:
        Dict with ``valid`` (bool), ``errors`` (list), ``warnings`` (list).
    """
    errors = []
    warnings = []

    if not isinstance(graph, dict):
        return {"valid": False, "errors": ["Graph must be a dict"], "warnings": []}

    # Required fields
    if "id" not in graph and "nodes" not in graph:
        errors.append("Graph must have 'nodes' field")

    nodes = graph.get("nodes", {})
    if not isinstance(nodes, dict) or len(nodes) == 0:
        errors.append("Graph must have at least one node")

    # Validate nodes
    for node_id, node in nodes.items():
        if not isinstance(node, dict):
            errors.append(f"Node '{node_id}' must be a dict")
            continue
        if "asset_path" not in node:
            errors.append(f"Node '{node_id}' missing 'asset_path'")

    # Validate edges
    edges = graph.get("edges", [])
    connected_nodes = set()

    for i, edge in enumerate(edges):
        if not isinstance(edge, dict):
            errors.append(f"Edge {i} must be a dict")
            continue

        from_ref = edge.get("from", {})
        to_ref = edge.get("to", {})

        from_node = from_ref.get("node", "") if isinstance(from_ref, dict) else ""
        to_node = to_ref.get("node", "") if isinstance(to_ref, dict) else ""

        if not from_node:
            errors.append(f"Edge {i} missing 'from.node'")
        elif from_node not in nodes:
            errors.append(f"Edge {i} references unknown node '{from_node}'")

        if not to_node:
            errors.append(f"Edge {i} missing 'to.node'")
        elif to_node not in nodes:
            errors.append(f"Edge {i} references unknown node '{to_node}'")

        if from_node and to_node and from_node == to_node:
            errors.append(f"Edge {i} is a self-edge on '{from_node}'")

        if not edge.get("relationship"):
            errors.append(f"Edge {i} missing 'relationship'")

        connected_nodes.add(from_node)
        connected_nodes.add(to_node)

    # Check for disconnected nodes
    if edges and nodes:
        disconnected = set(nodes.keys()) - connected_nodes
        if disconnected:
            warnings.append(
                f"Disconnected nodes (will be placed at fallback positions): "
                f"{', '.join(sorted(disconnected))}"
            )

    valid = len(errors) == 0
    return {"valid": valid, "errors": errors, "warnings": warnings}


# ---------------------------------------------------------------------------
# Resolve
# ---------------------------------------------------------------------------

def resolve_graph(graph):
    """Resolve an environment graph into world transforms.

    Calls the C++ ``ArborLayoutSolver::ResolveGraph`` which performs BFS
    anchor alignment.

    Args:
        graph: Environment graph dict.

    Returns:
        Dict with ``success``, ``transforms``, ``node_count``.
    """
    try:
        return _call_cpp(
            unreal.ArborLayoutSolver.resolve_graph(json.dumps(graph))
        )
    except Exception as e:
        unreal.log_error(f"[arbor.environment] resolve_graph: {e}")
        return {"success": False, "error": str(e)}


# ---------------------------------------------------------------------------
# Build & Clear
# ---------------------------------------------------------------------------

def build_environment(graph):
    """Validate, resolve, and spawn an environment from a semantic graph.

    Full pipeline:
    1. Validate graph structure
    2. Resolve graph to world transforms (C++ BFS solver)
    3. Spawn actors at resolved positions (C++ spawner)

    Args:
        graph: Environment graph dict with ``id``, ``nodes``, ``edges``.

    Returns:
        Dict with ``success``, ``spawned``, ``failed``, ``transforms``.
    """
    try:
        # 1. Validate
        validation = validate_graph(graph)
        if not validation["valid"]:
            return {
                "success": False,
                "error": "Graph validation failed",
                "validation_errors": validation["errors"],
            }
        for w in validation.get("warnings", []):
            unreal.log_warning(f"[arbor.environment] {w}")

        # 2. Resolve
        resolved = resolve_graph(graph)
        if not resolved.get("success"):
            return resolved

        transforms = resolved.get("transforms", {})
        env_id = graph.get("id", "unnamed")
        nodes = graph.get("nodes", {})

        # 3. Build spawn params: merge asset_path from graph + transforms from solver
        spawn_nodes = {}
        for node_id, node_def in nodes.items():
            t = transforms.get(node_id, {})
            spawn_nodes[node_id] = {
                "asset_path": node_def["asset_path"],
                "location": t.get("location", {"x": 0, "y": 0, "z": 0}),
                "rotation": t.get("rotation", {"pitch": 0, "yaw": 0, "roll": 0}),
                "scale": t.get("scale", {"x": 1, "y": 1, "z": 1}),
                "label": node_def.get("label"),
            }

        spawn_params = {
            "environment_id": env_id,
            "nodes": spawn_nodes,
        }

        spawn_result = _call_cpp(
            unreal.ArborEnvironmentSpawner.spawn_environment(json.dumps(spawn_params))
        )

        spawn_result["transforms"] = transforms
        if spawn_result.get("success"):
            unreal.log(
                f"[arbor.environment] build_environment: '{env_id}' — "
                f"{len(spawn_result.get('spawned', []))} actors spawned"
            )
        return spawn_result

    except Exception as e:
        unreal.log_error(f"[arbor.environment] build_environment: {e}")
        return {"success": False, "error": str(e)}


def clear_environment(environment_id):
    """Destroy all actors belonging to an environment.

    Args:
        environment_id: The environment ID used during building.

    Returns:
        Dict with ``success`` and ``destroyed_count``.
    """
    try:
        result = _call_cpp(
            unreal.ArborEnvironmentSpawner.despawn_environment(environment_id)
        )
        if result.get("success"):
            unreal.log(
                f"[arbor.environment] clear_environment: '{environment_id}' — "
                f"{result.get('destroyed_count', 0)} actors destroyed"
            )
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.environment] clear_environment: {e}")
        return {"success": False, "error": str(e)}
