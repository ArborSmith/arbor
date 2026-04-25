"""Arbor foliage — instanced foliage placement for high-volume scattering.

Use this module for efficiently placing hundreds or thousands of identical
meshes (grass, flowers, pebbles, ground cover).  It uses UE5's instanced
foliage system (``InstancedFoliageActor``) when available, falling back to
``HierarchicalInstancedStaticMeshComponent`` (HISM) for reliable programmatic
placement.

For fewer than ~50 unique actors (trees, large rocks), prefer
``arbor.scatter.scatter_meshes()`` which spawns individual
``StaticMeshActor`` objects.
"""

import math
import random

import unreal

from arbor.utils import _to_vector, load_asset, find_actors_by_class, make_rotator


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
        unreal.log_warning(f"[arbor.foliage] _trace_ground: {e}")
        return 0.0


def _find_landscape():
    """Find the first Landscape actor in the current level.

    Returns:
        The ``Landscape`` actor, or ``None``.
    """
    try:
        actors = find_actors_by_class("Landscape")
        if actors:
            return actors[0]
        # Also check for LandscapeStreamingProxy
        actors = find_actors_by_class("LandscapeStreamingProxy")
        if actors:
            return actors[0]
    except Exception:
        pass
    return None


def _build_transforms(positions, random_yaw=True, scale_min=0.8, scale_max=1.2):
    """Build a list of ``unreal.Transform`` from world positions.

    Args:
        positions: List of ``(x, y, z)`` tuples.
        random_yaw: Randomise yaw rotation per instance.
        scale_min: Minimum uniform scale.
        scale_max: Maximum uniform scale.

    Returns:
        List of ``unreal.Transform`` objects.
    """
    transforms = []
    for x, y, z in positions:
        t = unreal.Transform()
        t.translation = unreal.Vector(x, y, z)
        yaw = random.uniform(0, 360) if random_yaw else 0
        t.rotation = make_rotator(0, yaw, 0).quaternion()
        scl = random.uniform(scale_min, scale_max)
        t.scale3d = unreal.Vector(scl, scl, scl)
        transforms.append(t)
    return transforms


def _get_foliage_instance_count():
    """Return total instance count across all foliage components."""
    try:
        actors = find_actors_by_class("InstancedFoliageActor")
        total = 0
        for actor in actors:
            components = actor.get_components_by_class(
                unreal.InstancedStaticMeshComponent)
            for comp in components:
                total += comp.get_instance_count()
        return total
    except Exception:
        return -1


def _try_foliage_actor(foliage_type, transforms):
    """Attempt to add instances via InstancedFoliageActor (Approach A).

    Verifies that instances were actually added by comparing the total
    foliage instance count before and after.  Returns ``False`` if the
    count didn't increase, which triggers the HISM fallback.

    Returns:
        ``True`` if instances were added successfully, ``False`` otherwise.
    """
    try:
        count_before = _get_foliage_instance_count()

        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.InstancedFoliageActor.add_instances(world, foliage_type, transforms)

        count_after = _get_foliage_instance_count()
        added = count_after - count_before

        if count_before >= 0 and added < len(transforms) // 2:
            unreal.log_warning(
                f"[arbor.foliage] _try_foliage_actor: add_instances reported no error "
                f"but only {added}/{len(transforms)} instances appeared, falling back to HISM")
            return False

        return True
    except (AttributeError, Exception):
        return False


def _add_instances_hism(mesh, transforms, label="Foliage_HISM"):
    """Fallback: spawn an actor with HISM component and add instances (Approach C).

    Args:
        mesh: A ``StaticMesh`` asset.
        transforms: List of ``unreal.Transform`` objects.
        label: Editor label for the spawned actor.

    Returns:
        The spawned actor, or ``None`` on failure.
    """
    try:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Actor,
            unreal.Vector(0, 0, 0),
            make_rotator(0, 0, 0),
        )
        if actor is None:
            return None

        actor.set_actor_label(label)

        hism = actor.add_component_by_class(
            unreal.HierarchicalInstancedStaticMeshComponent,
            False,
            unreal.Transform(),
            False,
        )
        if hism is None:
            unreal.log_error("[arbor.foliage] _add_instances_hism: failed to add HISM component")
            actor.destroy_actor()
            return None

        hism.set_static_mesh(mesh)
        hism.set_editor_property("mobility", unreal.ComponentMobility.STATIC)

        for t in transforms:
            hism.add_instance(t)

        unreal.log(f"[arbor.foliage] _add_instances_hism: added {len(transforms)} instances via HISM")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] _add_instances_hism: {e}")
        return None


def _place_instances(foliage_type, mesh, transforms, label="Foliage"):
    """Place instances using Approach A (foliage actor), falling back to C (HISM).

    Args:
        foliage_type: The ``FoliageType_InstancedStaticMesh`` asset (may be ``None``
            if only HISM fallback is desired).
        mesh: The ``StaticMesh`` asset.
        transforms: List of ``unreal.Transform`` objects.
        label: Editor label for HISM fallback actor.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    count = len(transforms)
    if count == 0:
        return {"placed": 0, "method": "none"}

    # Approach A — proper foliage system
    if foliage_type is not None and _try_foliage_actor(foliage_type, transforms):
        unreal.log(f"[arbor.foliage] placed {count} instances via InstancedFoliageActor")
        return {"placed": count, "method": "foliage"}

    # Approach C — HISM fallback
    unreal.log_warning("[arbor.foliage] InstancedFoliageActor not available, using HISM fallback")
    actor = _add_instances_hism(mesh, transforms, label=label)
    if actor is not None:
        return {"placed": count, "method": "hism"}

    unreal.log_error("[arbor.foliage] failed to place instances via both foliage and HISM")
    return {"placed": 0, "method": "none"}


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def create_foliage_type(mesh_path, name=None, content_path="/Game/Foliage",
                        density=100.0, scale_min=0.8, scale_max=1.2,
                        align_to_normal=True, random_yaw=True,
                        ground_slope_angle=45.0, cull_distance_max=10000):
    """Create a ``FoliageType_InstancedStaticMesh`` asset.

    Delegates to C++ ``ArborFoliageTools.CreateFoliageType`` when available,
    falls back to Python asset creation.

    Args:
        mesh_path: Content path to a ``StaticMesh`` asset.
        name: Asset name.  Defaults to ``"FT_<mesh_name>"``.
        content_path: Content folder for the new asset.
        density: Foliage density (instances per 1000x1000 area).
        scale_min: Minimum uniform scale multiplier.
        scale_max: Maximum uniform scale multiplier.
        align_to_normal: Align instances to surface normal.
        random_yaw: Randomise yaw rotation.
        ground_slope_angle: Maximum ground slope in degrees.
        cull_distance_max: Maximum draw distance in cm.

    Returns:
        Content path of the created asset (``str``), or ``None`` on failure.
    """
    # --- Try C++ backend first ---
    import json as _json
    try:
        result_str = unreal.ArborFoliageTools.create_foliage_type(_json.dumps({
            "mesh_path": mesh_path, "name": name, "content_path": content_path,
            "density": density, "scale_min": scale_min, "scale_max": scale_max,
            "align_to_normal": align_to_normal, "random_yaw": random_yaw,
            "ground_slope_angle": ground_slope_angle,
            "cull_distance_max": cull_distance_max,
        }))
        result = _json.loads(result_str)
        if result.get("success"):
            return result["asset_path"]
    except (AttributeError, Exception) as e:
        unreal.log_warning(f"[arbor.foliage] C++ CreateFoliageType not available, "
                           f"falling back to Python: {e}")

    # --- Python fallback ---
    try:
        mesh = load_asset(mesh_path)
        if mesh is None:
            unreal.log_error(f"[arbor.foliage] create_foliage_type: cannot load mesh '{mesh_path}'")
            return None
        if not isinstance(mesh, unreal.StaticMesh):
            unreal.log_error(f"[arbor.foliage] create_foliage_type: '{mesh_path}' is not a StaticMesh")
            return None

        asset_name = name or f"FT_{mesh.get_name()}"

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

        # Try factory-based creation first
        foliage_type = None
        try:
            factory = unreal.FoliageType_InstancedStaticMeshFactory()
            foliage_type = asset_tools.create_asset(
                asset_name, content_path,
                unreal.FoliageType_InstancedStaticMesh, factory,
            )
        except AttributeError:
            # Factory class not available — try without factory
            try:
                foliage_type = asset_tools.create_asset(
                    asset_name, content_path,
                    unreal.FoliageType_InstancedStaticMesh, None,
                )
            except Exception as e2:
                unreal.log_error(f"[arbor.foliage] create_foliage_type: asset creation failed: {e2}")
                return None

        if foliage_type is None:
            unreal.log_error("[arbor.foliage] create_foliage_type: create_asset returned None")
            return None

        # Configure properties
        foliage_type.set_editor_property("mesh", mesh)
        foliage_type.set_editor_property("density", density)
        foliage_type.set_editor_property("scaling_min",
                                         unreal.Vector(scale_min, scale_min, scale_min))
        foliage_type.set_editor_property("scaling_max",
                                         unreal.Vector(scale_max, scale_max, scale_max))
        foliage_type.set_editor_property("align_to_normal", align_to_normal)
        foliage_type.set_editor_property("random_yaw", random_yaw)
        foliage_type.set_editor_property("ground_slope_angle",
                                         unreal.FloatInterval(0.0, ground_slope_angle))

        # Cull distance — start fading at 75% of max
        cull_start = int(cull_distance_max * 0.75)
        try:
            foliage_type.set_editor_property("cull_distance",
                                             unreal.Int32Interval(cull_start, int(cull_distance_max)))
        except Exception:
            # Some UE5 versions use separate properties
            try:
                foliage_type.set_editor_property("start_cull_distance", cull_start)
                foliage_type.set_editor_property("end_cull_distance", int(cull_distance_max))
            except Exception:
                pass

        unreal.EditorAssetLibrary.save_loaded_asset(foliage_type)

        asset_path = f"{content_path}/{asset_name}"
        unreal.log(f"[arbor.foliage] create_foliage_type: created '{asset_path}' "
                   f"for mesh '{mesh_path}'")
        return asset_path
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] create_foliage_type: {e}")
        return None


def paint_foliage(foliage_type_path, center=(0, 0, 0), radius=1000, count=100,
                  snap_to_ground=True, exclude_radius=None, exclude_center=None):
    """Add foliage instances in a circular area.

    Delegates to C++ ``ArborFoliageTools.PaintFoliageInstances`` when available.

    Args:
        foliage_type_path: Content path to a ``FoliageType`` asset created by
            ``create_foliage_type()``.
        center: ``(x, y, z)`` centre of the scatter area.
        radius: Scatter radius in cm.
        count: Number of instances to place.
        snap_to_ground: Raycast downward to place on ground surface.
        exclude_radius: Optional inner exclusion radius (donut shape).
        exclude_center: ``(x, y, z)`` centre of exclusion zone.  Defaults to
            *center* if not provided.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    # --- Try C++ backend first ---
    import json as _json
    try:
        c = _to_vector(center)
        result_str = unreal.ArborFoliageTools.paint_foliage_instances(_json.dumps({
            "foliage_type_path": foliage_type_path,
            "count": count,
            "center": [c.x, c.y, c.z],
            "radius": radius,
            "snap_to_ground": snap_to_ground,
        }))
        result = _json.loads(result_str)
        if result.get("success"):
            return {"placed": result.get("placed", 0), "method": result.get("method", "foliage")}
    except (AttributeError, Exception) as e:
        unreal.log_warning(f"[arbor.foliage] C++ PaintFoliageInstances not available: {e}")

    # --- Python fallback ---
    try:
        foliage_type = load_asset(foliage_type_path)
        if foliage_type is None:
            unreal.log_error(f"[arbor.foliage] paint_foliage: cannot load '{foliage_type_path}'")
            return {"placed": 0, "method": "none"}

        # Extract mesh from the foliage type for HISM fallback
        mesh = None
        try:
            mesh = foliage_type.get_editor_property("mesh")
        except Exception:
            pass

        # Read scale range from the foliage type
        scale_min, scale_max = 0.8, 1.2
        try:
            smin = foliage_type.get_editor_property("scaling_min")
            smax = foliage_type.get_editor_property("scaling_max")
            scale_min = smin.x
            scale_max = smax.x
        except Exception:
            pass

        c = _to_vector(center)
        exc = _to_vector(exclude_center) if exclude_center else c
        min_dist = exclude_radius or 0

        positions = []
        attempts = 0
        max_attempts = count * 5
        while len(positions) < count and attempts < max_attempts:
            attempts += 1
            angle = random.uniform(0, 2 * math.pi)
            dist = random.uniform(min_dist, radius)
            x = c.x + math.cos(angle) * dist
            y = c.y + math.sin(angle) * dist

            # Check exclusion zone
            if exclude_radius and exclude_radius > 0:
                dx = x - exc.x
                dy = y - exc.y
                if math.sqrt(dx * dx + dy * dy) < exclude_radius:
                    continue

            if snap_to_ground:
                z = _trace_ground(x, y, c.z + 10000)
            else:
                z = c.z

            positions.append((x, y, z))

        random_yaw = True
        try:
            random_yaw = foliage_type.get_editor_property("random_yaw")
        except Exception:
            pass

        transforms = _build_transforms(positions, random_yaw=random_yaw,
                                       scale_min=scale_min, scale_max=scale_max)

        mesh_name = mesh.get_name() if mesh else "unknown"
        result = _place_instances(foliage_type, mesh, transforms,
                                  label=f"Foliage_{mesh_name}")
        unreal.log(f"[arbor.foliage] paint_foliage: {result['placed']}/{count} "
                   f"at ({c.x:.0f}, {c.y:.0f}) r={radius}")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] paint_foliage: {e}")
        return {"placed": 0, "method": "none"}


def paint_foliage_in_bounds(foliage_type_path, bounds_min, bounds_max,
                            count=100, snap_to_ground=True):
    """Add foliage instances in a rectangular area.

    Args:
        foliage_type_path: Content path to a ``FoliageType`` asset.
        bounds_min: ``(x, y, z)`` minimum corner.
        bounds_max: ``(x, y, z)`` maximum corner.
        count: Number of instances to place.
        snap_to_ground: Raycast downward to place on ground surface.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    try:
        foliage_type = load_asset(foliage_type_path)
        if foliage_type is None:
            unreal.log_error(f"[arbor.foliage] paint_foliage_in_bounds: "
                             f"cannot load '{foliage_type_path}'")
            return {"placed": 0, "method": "none"}

        mesh = None
        try:
            mesh = foliage_type.get_editor_property("mesh")
        except Exception:
            pass

        scale_min, scale_max = 0.8, 1.2
        try:
            smin = foliage_type.get_editor_property("scaling_min")
            smax = foliage_type.get_editor_property("scaling_max")
            scale_min = smin.x
            scale_max = smax.x
        except Exception:
            pass

        bmin = _to_vector(bounds_min)
        bmax = _to_vector(bounds_max)

        positions = []
        for _ in range(count):
            x = random.uniform(bmin.x, bmax.x)
            y = random.uniform(bmin.y, bmax.y)

            if snap_to_ground:
                z = _trace_ground(x, y, bmax.z + 10000)
            else:
                z = random.uniform(bmin.z, bmax.z)

            positions.append((x, y, z))

        random_yaw = True
        try:
            random_yaw = foliage_type.get_editor_property("random_yaw")
        except Exception:
            pass

        transforms = _build_transforms(positions, random_yaw=random_yaw,
                                       scale_min=scale_min, scale_max=scale_max)

        mesh_name = mesh.get_name() if mesh else "unknown"
        result = _place_instances(foliage_type, mesh, transforms,
                                  label=f"Foliage_{mesh_name}")
        unreal.log(f"[arbor.foliage] paint_foliage_in_bounds: {result['placed']}/{count}")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] paint_foliage_in_bounds: {e}")
        return {"placed": 0, "method": "none"}


def paint_foliage_on_landscape(foliage_type_path, landscape_actor_name=None,
                               count=500, density_map=None):
    """Scatter foliage across a landscape surface.

    Delegates to C++ ``ArborFoliageTools.PaintFoliageInstances`` when available
    (density_map not supported in C++ path — falls back to Python).

    Args:
        foliage_type_path: Content path to a ``FoliageType`` asset.
        landscape_actor_name: Editor label of the landscape actor.  If ``None``,
            finds the first landscape in the level.
        count: Number of instances to place.
        density_map: Optional callable ``(x, y) → float`` in ``[0, 1]`` for
            non-uniform distribution.  A value of ``0`` means no placement at
            that position; ``1`` means always place.  If ``None``, distribution
            is uniform.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    # --- Try C++ backend (when no density_map — C++ can't handle callables) ---
    if density_map is None:
        import json as _json
        try:
            result_str = unreal.ArborFoliageTools.paint_foliage_instances(_json.dumps({
                "foliage_type_path": foliage_type_path,
                "count": count,
                "landscape": True,
                "snap_to_ground": True,
            }))
            result = _json.loads(result_str)
            if result.get("success"):
                return {"placed": result.get("placed", 0), "method": result.get("method", "foliage")}
        except (AttributeError, Exception) as e:
            unreal.log_warning(f"[arbor.foliage] C++ PaintFoliageInstances not available: {e}")

    # --- Python fallback ---
    try:
        # Find landscape
        landscape = None
        if landscape_actor_name:
            from arbor.utils import find_actor_by_name
            landscape = find_actor_by_name(landscape_actor_name)
        else:
            landscape = _find_landscape()

        if landscape is None:
            unreal.log_error("[arbor.foliage] paint_foliage_on_landscape: "
                             "no landscape found")
            return {"placed": 0, "method": "none"}

        foliage_type = load_asset(foliage_type_path)
        if foliage_type is None:
            unreal.log_error(f"[arbor.foliage] paint_foliage_on_landscape: "
                             f"cannot load '{foliage_type_path}'")
            return {"placed": 0, "method": "none"}

        mesh = None
        try:
            mesh = foliage_type.get_editor_property("mesh")
        except Exception:
            pass

        scale_min, scale_max = 0.8, 1.2
        try:
            smin = foliage_type.get_editor_property("scaling_min")
            smax = foliage_type.get_editor_property("scaling_max")
            scale_min = smin.x
            scale_max = smax.x
        except Exception:
            pass

        # Derive bounds from landscape
        origin, extent = landscape.get_actor_bounds(False)
        x_min = origin.x - extent.x
        x_max = origin.x + extent.x
        y_min = origin.y - extent.y
        y_max = origin.y + extent.y
        z_top = origin.z + extent.z + 10000

        # Generate positions with optional density map
        positions = []
        attempts = 0
        max_attempts = count * 5
        while len(positions) < count and attempts < max_attempts:
            attempts += 1
            x = random.uniform(x_min, x_max)
            y = random.uniform(y_min, y_max)

            if density_map is not None:
                if random.random() > density_map(x, y):
                    continue

            z = _trace_ground(x, y, z_top)
            positions.append((x, y, z))

        random_yaw = True
        try:
            random_yaw = foliage_type.get_editor_property("random_yaw")
        except Exception:
            pass

        transforms = _build_transforms(positions, random_yaw=random_yaw,
                                       scale_min=scale_min, scale_max=scale_max)

        mesh_name = mesh.get_name() if mesh else "unknown"
        result = _place_instances(foliage_type, mesh, transforms,
                                  label=f"Foliage_{mesh_name}")
        unreal.log(f"[arbor.foliage] paint_foliage_on_landscape: "
                   f"{result['placed']}/{count} on '{landscape.get_actor_label()}'")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] paint_foliage_on_landscape: {e}")
        return {"placed": 0, "method": "none"}


def remove_foliage(foliage_type_path, center=None, radius=None):
    """Remove foliage instances.

    If *center* and *radius* are provided, only removes instances within that
    area.  Otherwise removes **all** instances of the given foliage type.

    Also removes HISM fallback actors whose label matches the foliage type's
    mesh name.

    Args:
        foliage_type_path: Content path to a ``FoliageType`` asset.
        center: ``(x, y, z)`` centre of removal area, or ``None`` for all.
        radius: Removal radius in cm, or ``None`` for all.

    Returns:
        ``dict`` with ``{"removed": int}``.
    """
    try:
        foliage_type = load_asset(foliage_type_path)
        removed = 0

        # Attempt removal via InstancedFoliageActor
        if foliage_type is not None:
            try:
                world = unreal.EditorLevelLibrary.get_editor_world()
                if center is not None and radius is not None:
                    c = _to_vector(center)
                    # Try sphere-based removal if API supports it
                    try:
                        unreal.InstancedFoliageActor.remove_instances(
                            world, foliage_type, c, radius)
                        unreal.log(f"[arbor.foliage] remove_foliage: removed instances "
                                   f"in radius {radius} at ({c.x:.0f}, {c.y:.0f})")
                        removed += 1  # Exact count unknown from API
                    except (AttributeError, Exception):
                        pass
                else:
                    try:
                        unreal.InstancedFoliageActor.remove_all_instances(
                            world, foliage_type)
                        unreal.log("[arbor.foliage] remove_foliage: removed all foliage instances")
                        removed += 1
                    except (AttributeError, Exception):
                        pass
            except Exception:
                pass

        # Also remove HISM fallback actors
        mesh_name = None
        if foliage_type is not None:
            try:
                mesh = foliage_type.get_editor_property("mesh")
                mesh_name = mesh.get_name() if mesh else None
            except Exception:
                pass

        # Find HISM actors with matching labels
        try:
            from arbor.utils import _get_all_level_actors
            for actor in _get_all_level_actors():
                label = actor.get_actor_label()
                if label and label.startswith("Foliage_"):
                    should_remove = False
                    if mesh_name and mesh_name in label:
                        should_remove = True
                    elif foliage_type_path and foliage_type_path.split("/")[-1] in label:
                        should_remove = True

                    if should_remove:
                        if center is not None and radius is not None:
                            c = _to_vector(center)
                            loc = actor.get_actor_location()
                            dx = loc.x - c.x
                            dy = loc.y - c.y
                            if math.sqrt(dx * dx + dy * dy) > radius:
                                continue

                        actor.destroy_actor()
                        removed += 1
        except Exception:
            pass

        unreal.log(f"[arbor.foliage] remove_foliage: removed {removed} foliage entries")
        return {"removed": removed}
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] remove_foliage: {e}")
        return {"removed": 0}


def list_foliage_types():
    """List all foliage type assets in the project.

    Returns:
        List of dicts ``{"path": str, "mesh": str, "density": float}``.
    """
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        results = []

        # Search for FoliageType assets
        try:
            assets = registry.get_assets_by_class(
                unreal.TopLevelAssetPath("/Script/Foliage", "FoliageType_InstancedStaticMesh"))
        except (AttributeError, Exception):
            # Older API
            try:
                assets = registry.get_assets_by_class("FoliageType_InstancedStaticMesh")
            except Exception:
                assets = []

        for asset_data in assets:
            path = str(asset_data.get_full_name()).split(" ")[-1]
            try:
                obj = load_asset(path)
                mesh_path = ""
                density = 0.0
                if obj:
                    try:
                        mesh = obj.get_editor_property("mesh")
                        mesh_path = mesh.get_path_name() if mesh else ""
                    except Exception:
                        pass
                    try:
                        density = obj.get_editor_property("density")
                    except Exception:
                        pass
                results.append({"path": path, "mesh": mesh_path, "density": density})
            except Exception:
                results.append({"path": path, "mesh": "", "density": 0.0})

        unreal.log(f"[arbor.foliage] list_foliage_types: found {len(results)} types")
        return results
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] list_foliage_types: {e}")
        return []


# ---------------------------------------------------------------------------
# Convenience wrappers
# ---------------------------------------------------------------------------

def scatter_grass(landscape=None, mesh_path=None, count=2000,
                  scale_min=0.5, scale_max=1.0):
    """Quick grass scatter across a landscape.

    Creates a foliage type with grass-appropriate defaults (high density,
    small scale, tight slope limit) and paints onto the landscape.

    Args:
        landscape: Landscape actor name, or ``None`` to auto-detect.
        mesh_path: Content path to a grass mesh.  Uses engine basic plane
            if not provided.
        count: Number of grass instances.
        scale_min: Minimum scale.
        scale_max: Maximum scale.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    try:
        if mesh_path is None:
            mesh_path = "/Engine/BasicShapes/Plane.Plane"

        ft_path = create_foliage_type(
            mesh_path, name="FT_Grass",
            density=200.0, scale_min=scale_min, scale_max=scale_max,
            align_to_normal=True, random_yaw=True,
            ground_slope_angle=30.0, cull_distance_max=8000,
        )
        if ft_path is None:
            return {"placed": 0, "method": "none"}

        return paint_foliage_on_landscape(ft_path, landscape_actor_name=landscape,
                                          count=count)
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] scatter_grass: {e}")
        return {"placed": 0, "method": "none"}


def scatter_bushes(mesh_path, landscape=None, count=200,
                   scale_min=0.7, scale_max=1.3):
    """Scatter bushes across a landscape.

    Creates a foliage type with bush defaults (moderate density, more scale
    variation, stricter slope filtering) and paints onto the landscape.

    Args:
        mesh_path: Content path to a bush mesh.
        landscape: Landscape actor name, or ``None`` to auto-detect.
        count: Number of bush instances.
        scale_min: Minimum scale.
        scale_max: Maximum scale.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    try:
        ft_path = create_foliage_type(
            mesh_path, name="FT_Bush",
            density=50.0, scale_min=scale_min, scale_max=scale_max,
            align_to_normal=False, random_yaw=True,
            ground_slope_angle=30.0, cull_distance_max=15000,
        )
        if ft_path is None:
            return {"placed": 0, "method": "none"}

        return paint_foliage_on_landscape(ft_path, landscape_actor_name=landscape,
                                          count=count)
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] scatter_bushes: {e}")
        return {"placed": 0, "method": "none"}


def scatter_flowers(mesh_path, center=(0, 0, 0), radius=500, count=300,
                    color_variation=True):
    """Scatter a cluster of flowers around a point.

    Args:
        mesh_path: Content path to a flower mesh.
        center: ``(x, y, z)`` centre of the cluster.
        radius: Scatter radius in cm.
        count: Number of flower instances.
        color_variation: If ``True``, uses wider scale range (0.5–1.2) to
            simulate size/colour variation.

    Returns:
        ``dict`` with ``{"placed": int, "method": str}``.
    """
    try:
        smin = 0.5 if color_variation else 0.8
        smax = 1.2 if color_variation else 1.0

        ft_path = create_foliage_type(
            mesh_path, name="FT_Flower",
            density=150.0, scale_min=smin, scale_max=smax,
            align_to_normal=True, random_yaw=True,
            ground_slope_angle=40.0, cull_distance_max=6000,
        )
        if ft_path is None:
            return {"placed": 0, "method": "none"}

        return paint_foliage(ft_path, center=center, radius=radius, count=count)
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] scatter_flowers: {e}")
        return {"placed": 0, "method": "none"}


def scatter_ground_cover(mesh_paths, landscape=None, count=1000):
    """Scatter mixed ground cover using multiple mesh types.

    Creates a foliage type for each mesh and distributes *count* evenly
    across them.

    Args:
        mesh_paths: List of content paths to ground cover meshes.
        landscape: Landscape actor name, or ``None`` to auto-detect.
        count: Total number of instances across all types.

    Returns:
        ``dict`` with ``{"placed": int, "types": int}``.
    """
    try:
        if not mesh_paths:
            unreal.log_error("[arbor.foliage] scatter_ground_cover: no mesh paths provided")
            return {"placed": 0, "types": 0}

        per_type = max(1, count // len(mesh_paths))
        total_placed = 0
        types_created = 0

        for i, mp in enumerate(mesh_paths):
            mesh_name = mp.split("/")[-1].split(".")[0]
            ft_path = create_foliage_type(
                mp, name=f"FT_GroundCover_{mesh_name}",
                density=100.0, scale_min=0.6, scale_max=1.1,
                align_to_normal=True, random_yaw=True,
                ground_slope_angle=35.0, cull_distance_max=8000,
            )
            if ft_path is None:
                continue

            types_created += 1
            result = paint_foliage_on_landscape(ft_path,
                                                landscape_actor_name=landscape,
                                                count=per_type)
            total_placed += result.get("placed", 0)

        unreal.log(f"[arbor.foliage] scatter_ground_cover: placed {total_placed} "
                   f"across {types_created} types")
        return {"placed": total_placed, "types": types_created}
    except Exception as e:
        unreal.log_error(f"[arbor.foliage] scatter_ground_cover: {e}")
        return {"placed": 0, "types": 0}
