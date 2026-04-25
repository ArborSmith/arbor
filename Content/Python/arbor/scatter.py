"""Arbor scatter — random mesh placement and foliage helpers."""

import math
import random

import unreal

from arbor.utils import _to_vector, load_asset, make_rotator


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _trace_ground(x, y, start_z=50000.0, trace_distance=100000.0):
    """Cast a ray downward to find the ground surface Z.

    Delegates to C++ ``ArborActorTools.TraceGroundZ`` for version-safe tracing.

    Args:
        x: World X coordinate.
        y: World Y coordinate.
        start_z: Z to start the trace from.
        trace_distance: Total trace distance downward.

    Returns:
        Z coordinate of the hit point, or ``0.0`` if no hit.
    """
    import json as _json
    try:
        result_str = unreal.ArborActorTools.trace_ground_z(_json.dumps({
            "x": x, "y": y, "start_z": start_z, "trace_distance": trace_distance
        }))
        result = _json.loads(result_str)
        return result.get("z", 0.0) if result.get("hit") else 0.0
    except Exception as e:
        unreal.log_warning(f"[arbor.scatter] _trace_ground: {e}")
        return 0.0


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def scatter_meshes(mesh_path, count, bounds_min, bounds_max,
                   random_rotation_yaw=True, random_scale_min=0.8,
                   random_scale_max=1.2, snap_to_ground=True,
                   label_prefix="Scattered", seed=None):
    """Scatter static mesh actors randomly within a bounding box.

    Args:
        mesh_path: Content path to a ``StaticMesh`` asset.
        count: Number of instances to place.
        bounds_min: ``(x, y, z)`` minimum corner.
        bounds_max: ``(x, y, z)`` maximum corner.
        random_rotation_yaw: Randomise yaw rotation.
        random_scale_min: Minimum uniform scale multiplier.
        random_scale_max: Maximum uniform scale multiplier.
        snap_to_ground: Raycast downward to place on ground surface.
        label_prefix: Name prefix for spawned actors.
        seed: Optional random seed for reproducibility.

    Returns:
        List of spawned ``StaticMeshActor`` objects.
    """
    try:
        if seed is not None:
            random.seed(seed)

        mesh = load_asset(mesh_path)
        if mesh is None:
            return []
        if not isinstance(mesh, unreal.StaticMesh):
            unreal.log_error(f"[arbor.scatter] scatter_meshes: '{mesh_path}' is not a StaticMesh")
            return []

        bmin = _to_vector(bounds_min)
        bmax = _to_vector(bounds_max)

        actors = []
        for i in range(count):
            x = random.uniform(bmin.x, bmax.x)
            y = random.uniform(bmin.y, bmax.y)

            if snap_to_ground:
                z = _trace_ground(x, y, bmax.z)
            else:
                z = random.uniform(bmin.z, bmax.z)

            yaw = random.uniform(0, 360) if random_rotation_yaw else 0
            scl = random.uniform(random_scale_min, random_scale_max)

            actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.StaticMeshActor,
                unreal.Vector(x, y, z),
                make_rotator(0, yaw, 0),
            )
            if actor is None:
                continue

            actor.static_mesh_component.set_static_mesh(mesh)
            actor.set_actor_scale3d(unreal.Vector(scl, scl, scl))
            actor.set_actor_label(f"{label_prefix}_{i}")
            actors.append(actor)

        unreal.log(f"[arbor.scatter] scatter_meshes: placed {len(actors)}/{count} "
                   f"instances of '{mesh_path}'")
        return actors
    except Exception as e:
        unreal.log_error(f"[arbor.scatter] scatter_meshes: {e}")
        return []


def scatter_on_landscape(mesh_path, landscape_actor, count,
                         random_rotation_yaw=True, random_scale_min=0.8,
                         random_scale_max=1.2, label_prefix="Scattered",
                         seed=None):
    """Scatter meshes onto a landscape surface using downward raycasts.

    Derives the scatter bounds from the landscape's bounding box.

    Args:
        mesh_path: Content path to a ``StaticMesh`` asset.
        landscape_actor: The ``Landscape`` actor to scatter onto.
        count: Number of instances to place.
        random_rotation_yaw: Randomise yaw rotation.
        random_scale_min: Minimum uniform scale multiplier.
        random_scale_max: Maximum uniform scale multiplier.
        label_prefix: Name prefix for spawned actors.
        seed: Optional random seed for reproducibility.

    Returns:
        List of spawned ``StaticMeshActor`` objects.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.scatter] scatter_on_landscape: landscape_actor is None")
            return []

        origin, extent = landscape_actor.get_actor_bounds(False)
        bmin = (origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        bmax = (origin.x + extent.x, origin.y + extent.y, origin.z + extent.z + 10000)

        return scatter_meshes(
            mesh_path, count, bmin, bmax,
            random_rotation_yaw=random_rotation_yaw,
            random_scale_min=random_scale_min,
            random_scale_max=random_scale_max,
            snap_to_ground=True,
            label_prefix=label_prefix,
            seed=seed,
        )
    except Exception as e:
        unreal.log_error(f"[arbor.scatter] scatter_on_landscape: {e}")
        return []


def add_foliage_type(mesh_path, density=100.0, scale_min=0.8, scale_max=1.2):
    """Register a static mesh as a foliage type.

    WARNING: Foliage type creation via UE5 Python is unreliable.
    This is a best-effort stub.  Consider using ``scatter_meshes()``
    with individual StaticMeshActors as a more reliable alternative.

    Args:
        mesh_path: Content path to the mesh.
        density: Foliage density.
        scale_min: Minimum scale.
        scale_max: Maximum scale.

    Returns:
        The foliage type object, or ``None``.
    """
    try:
        unreal.log_warning("[arbor.scatter] add_foliage_type: this API is experimental. "
                           "Foliage types created via Python may not behave identically "
                           "to those created in the Foliage editor.")

        mesh = load_asset(mesh_path)
        if mesh is None:
            return None

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        try:
            factory_class = unreal.FoliageType_InstancedStaticMeshFactory
        except AttributeError:
            unreal.log_error("[arbor.scatter] add_foliage_type: factory class not available")
            return None

        foliage_type = asset_tools.create_asset(
            f"FT_{mesh.get_name()}",
            "/Game/Foliage",
            unreal.FoliageType_InstancedStaticMesh,
            factory_class(),
        )
        if foliage_type:
            foliage_type.set_editor_property("mesh", mesh)
            foliage_type.set_editor_property("density", density)
            foliage_type.set_editor_property("scaling_min", unreal.Vector(scale_min, scale_min, scale_min))
            foliage_type.set_editor_property("scaling_max", unreal.Vector(scale_max, scale_max, scale_max))
            unreal.EditorAssetLibrary.save_loaded_asset(foliage_type)
            unreal.log(f"[arbor.scatter] add_foliage_type: created for '{mesh_path}'")
        return foliage_type
    except Exception as e:
        unreal.log_error(f"[arbor.scatter] add_foliage_type: {e}")
        return None


def paint_foliage(foliage_type, center=(0, 0, 0), radius=1000.0, count=50):
    """Paint foliage instances in a radius.

    WARNING: Programmatic foliage painting is unreliable via UE5 Python.
    Instances added this way may not be recognised by the foliage editing
    tools.  Consider using ``scatter_meshes()`` as a more reliable
    alternative.

    Args:
        foliage_type: A ``FoliageType`` object from ``add_foliage_type()``.
        center: ``(x, y, z)`` centre of the paint area.
        radius: Scatter radius in cm.
        count: Number of instances to paint.

    Returns:
        ``True`` if any instances were placed, ``False`` otherwise.
    """
    try:
        unreal.log_warning("[arbor.scatter] paint_foliage: experimental — instances may not "
                           "appear in the foliage editor. Use scatter_meshes() as an alternative.")

        if foliage_type is None:
            unreal.log_error("[arbor.scatter] paint_foliage: foliage_type is None")
            return False

        c = _to_vector(center)
        transforms = []
        for _ in range(count):
            angle = random.uniform(0, 2 * math.pi)
            dist = random.uniform(0, radius)
            x = c.x + math.cos(angle) * dist
            y = c.y + math.sin(angle) * dist
            z = _trace_ground(x, y, c.z + 10000)

            t = unreal.Transform()
            t.translation = unreal.Vector(x, y, z)
            t.rotation = make_rotator(0, random.uniform(0, 360), 0).quaternion()
            transforms.append(t)

        # Attempt to add via InstancedFoliageActor
        try:
            world = unreal.EditorLevelLibrary.get_editor_world()
            foliage_actor = unreal.InstancedFoliageActor
            foliage_actor.add_instances(world, foliage_type, transforms)
            unreal.log(f"[arbor.scatter] paint_foliage: added {count} instances")
            return True
        except (AttributeError, Exception) as e:
            unreal.log_warning(f"[arbor.scatter] paint_foliage: add_instances failed: {e}")
            return False
    except Exception as e:
        unreal.log_error(f"[arbor.scatter] paint_foliage: {e}")
        return False
