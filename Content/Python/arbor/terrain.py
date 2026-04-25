"""Arbor terrain — landscape creation, procedural heightmaps, layer painting,
and water bodies.

Uses the C++ ``LandscapeBuilder`` (part of the Arbor plugin) for reliable
landscape creation, heightmap manipulation, and layer weight painting.
Falls back to ``spawn_actor_from_class`` if the builder is unavailable.

Water body functions require the Water Plugin to be enabled in the project.
"""

import math
import os
import struct as _struct

import unreal

from arbor.utils import (_to_vector, load_asset, make_rotator,
                         find_actor_by_name, find_actors_by_class)


# ---------------------------------------------------------------------------
# Landscape size helpers
# ---------------------------------------------------------------------------

def get_landscape_size(section_size=63, sections_per_component=1,
                       component_count_x=8, component_count_y=8):
    """Calculate the required heightmap dimensions for a landscape.

    Args:
        section_size: Quads per section (7, 15, 31, 63, 127, 255).
        sections_per_component: 1 (1x1) or 2 (2x2).
        component_count_x: Components along X.
        component_count_y: Components along Y.

    Returns:
        Dict with ``width``, ``height``, ``total_vertices``.
    """
    quads_per_component = section_size * sections_per_component
    width = component_count_x * quads_per_component + 1
    height = component_count_y * quads_per_component + 1
    return {
        "width": width,
        "height": height,
        "total_vertices": width * height,
    }


# ---------------------------------------------------------------------------
# Internal: convert between Python bytearray and TArray<uint16>
# ---------------------------------------------------------------------------

def _bytes_to_uint16_list(data_bytes):
    """Convert bytearray of LE uint16 to a Python list of ints."""
    count = len(data_bytes) // 2
    return list(_struct.unpack_from(f'<{count}H', data_bytes))


def _uint16_list_to_ue_array(values):
    """Convert a Python list of uint16 ints to an unreal Array for C++ binding.

    LandscapeBuilder.create_landscape / set_heightmap_data expect
    TArray<uint16>.  UE5 Python exposes these as regular Python lists
    when calling UFUNCTIONs.
    """
    return values  # UE5 Python marshals list[int] → TArray<uint16> automatically


# ---------------------------------------------------------------------------
# Landscape creation
# ---------------------------------------------------------------------------

def create_landscape(location=(0, 0, 0), section_size=63,
                     sections_per_component=1, component_count_x=8,
                     component_count_y=8, scale=(100, 100, 100),
                     material_path=None, height_data=None,
                     replace_existing=True):
    """Create a Landscape actor in the editor.

    Uses the Arbor C++ ``LandscapeBuilder`` for reliable landscape creation
    with proper heightmap initialization.  Falls back to
    ``spawn_actor_from_class`` if the builder is unavailable.

    Args:
        location: ``(x, y, z)`` world position.
        section_size: Quads per section (7, 15, 31, 63, 127, 255).
        sections_per_component: 1 (1x1) or 2 (2x2).
        component_count_x: Number of components along X.
        component_count_y: Number of components along Y.
        scale: ``(x, y, z)`` landscape scale.
        material_path: Optional content path to a landscape material.
        height_data: Optional ``list[int]`` of uint16 values. Pass empty
            list or ``None`` for flat terrain.
        replace_existing: If ``True`` (default), destroy any existing
            ``ALandscape`` actors before creating a new one.

    Returns:
        The ``Landscape`` actor, or ``None`` on failure.
    """
    # Destroy existing landscapes if replacing
    if replace_existing:
        existing = find_actors_by_class("Landscape")
        if existing:
            for actor in existing:
                label = actor.get_actor_label()
                unreal.log(f"[arbor.terrain] create_landscape: "
                           f"destroying existing landscape '{label}'")
                actor.destroy_actor()
            unreal.log(f"[arbor.terrain] create_landscape: "
                       f"destroyed {len(existing)} existing landscape(s)")

    # Try C++ LandscapeBuilder first
    actor = _create_via_builder(
        location, scale, section_size, sections_per_component,
        component_count_x, component_count_y, height_data)

    # Fallback: spawn_actor_from_class (unreliable but better than nothing)
    if actor is None:
        actor = _create_via_spawn(location, scale)

    if actor is None:
        return None

    if material_path:
        set_landscape_material(actor, material_path)

    unreal.log(f"[arbor.terrain] create_landscape: created at {location}")
    return actor


def _create_via_builder(location, scale, section_size, sections_per_component,
                        component_count_x, component_count_y, height_data):
    """Create landscape via Arbor C++ LandscapeBuilder."""
    try:
        builder = unreal.LandscapeBuilder
        loc = _to_vector(location)
        scl = _to_vector(scale)
        hd = list(height_data) if height_data else []

        landscape = builder.create_landscape(
            loc, scl,
            section_size, sections_per_component,
            component_count_x, component_count_y,
            hd)

        if landscape is not None:
            unreal.log("[arbor.terrain] created landscape via LandscapeBuilder")
            return landscape

        unreal.log_warning("[arbor.terrain] LandscapeBuilder.create_landscape returned None")
        return None
    except AttributeError:
        unreal.log_warning("[arbor.terrain] LandscapeBuilder not available, "
                           "is the Arbor C++ plugin loaded?")
        return None
    except Exception as e:
        unreal.log_warning(f"[arbor.terrain] LandscapeBuilder failed: {e}")
        return None


def _create_via_spawn(location, scale):
    """Fallback: create landscape via spawn_actor_from_class (unreliable)."""
    try:
        unreal.log_warning("[arbor.terrain] falling back to spawn_actor_from_class "
                           "(may produce placeholder)")
        loc = _to_vector(location)
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Landscape, loc, make_rotator(0, 0, 0))
        if actor is None:
            unreal.log_error("[arbor.terrain] spawn_actor_from_class returned None")
            return None

        class_name = actor.get_class().get_name()
        if "Placeholder" in class_name or "Proxy" in class_name:
            unreal.log_warning("[arbor.terrain] got placeholder, not a real landscape")
            actor.destroy_actor()
            return None

        actor.set_actor_scale3d(_to_vector(scale))
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] spawn fallback failed: {e}")
        return None


def create_flat_landscape(location=(0, 0, 0), size=8, scale=(100, 100, 100)):
    """Shortcut to create a flat landscape ready for sculpting.

    Args:
        location: ``(x, y, z)`` world position.
        size: Number of components per axis.
        scale: ``(x, y, z)`` landscape scale.

    Returns:
        The ``Landscape`` actor, or ``None``.
    """
    return create_landscape(
        location=location,
        section_size=63,
        sections_per_component=1,
        component_count_x=size,
        component_count_y=size,
        scale=scale,
    )


# ---------------------------------------------------------------------------
# Heightmap operations
# ---------------------------------------------------------------------------

def apply_heightmap(landscape_actor, heightmap_data, layer_name="heightmap"):
    """Apply uint16 heightmap data to an existing landscape.

    Args:
        landscape_actor: Landscape actor (from ``create_landscape``).
        heightmap_data: ``list[int]`` of uint16 values, OR ``bytearray``
            of packed uint16 LE (from ``arbor.noise.heightmap_to_uint16``).
        layer_name: Unused (kept for API compatibility).

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] apply_heightmap: landscape_actor is None")
            return False

        # Convert bytearray to list[int] if needed
        if isinstance(heightmap_data, (bytes, bytearray)):
            values = _bytes_to_uint16_list(heightmap_data)
        else:
            values = list(heightmap_data)

        result = unreal.LandscapeBuilder.set_heightmap_data(
            landscape_actor, values)

        if result:
            unreal.log(f"[arbor.terrain] apply_heightmap: applied {len(values)} values")
        else:
            unreal.log_error("[arbor.terrain] apply_heightmap: LandscapeBuilder returned false")

        return result
    except AttributeError:
        unreal.log_error("[arbor.terrain] apply_heightmap: LandscapeBuilder not available")
        return False
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] apply_heightmap: {e}")
        return False


def get_heightmap_data(landscape_actor):
    """Read heightmap data from an existing landscape.

    Args:
        landscape_actor: Landscape actor.

    Returns:
        ``list[int]`` of uint16 values, or ``None`` on failure.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] get_heightmap_data: landscape_actor is None")
            return None

        data = unreal.LandscapeBuilder.get_heightmap_data(landscape_actor)
        if data is not None and len(data) > 0:
            return list(data)

        unreal.log_error("[arbor.terrain] get_heightmap_data: no data returned")
        return None
    except AttributeError:
        unreal.log_error("[arbor.terrain] get_heightmap_data: LandscapeBuilder not available")
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] get_heightmap_data: {e}")
        return None


def import_heightmap(landscape_actor, heightmap_path, layer_name="heightmap"):
    """Import a heightmap file onto an existing landscape.

    NOTE: The heightmap must be a 16-bit single-channel image (PNG or RAW).

    Args:
        landscape_actor: An existing ``Landscape`` actor.
        heightmap_path: Absolute file path to the heightmap image.
        layer_name: Import layer name.

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if not os.path.isfile(heightmap_path):
            unreal.log_error(f"[arbor.terrain] import_heightmap: file not found: {heightmap_path}")
            return False

        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] import_heightmap: landscape_actor is None")
            return False

        # Try LandscapeEditorUtils
        try:
            unreal.LandscapeEditorUtils.import_heightmap(landscape_actor, heightmap_path)
            unreal.log(f"[arbor.terrain] import_heightmap: imported via LandscapeEditorUtils")
            return True
        except (AttributeError, Exception):
            pass

        unreal.log_warning(
            f"[arbor.terrain] import_heightmap: automatic import not available. "
            f"Import manually in Landscape Mode > Manage > Import: {heightmap_path}")
        return False
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] import_heightmap: {e}")
        return False


# ---------------------------------------------------------------------------
# Material
# ---------------------------------------------------------------------------

def set_landscape_material(landscape_actor, material_path):
    """Set the material on an existing landscape.

    Args:
        landscape_actor: An existing ``Landscape`` actor, or a string
            actor label to resolve via ``find_actor_by_name()``.
        material_path: Content path to a landscape material.

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if isinstance(landscape_actor, str):
            resolved = find_actor_by_name(landscape_actor)
            if resolved is None:
                unreal.log_error(
                    f"[arbor.terrain] set_landscape_material: "
                    f"actor '{landscape_actor}' not found")
                return False
            landscape_actor = resolved

        if landscape_actor is None:
            unreal.log_error(
                "[arbor.terrain] set_landscape_material: landscape_actor is None")
            return False

        mat = load_asset(material_path)
        if mat is None:
            unreal.log_error(
                f"[arbor.terrain] set_landscape_material: "
                f"material '{material_path}' not found")
            return False

        landscape_actor.set_editor_property("landscape_material", mat)
        unreal.log(f"[arbor.terrain] set_landscape_material: applied '{material_path}'")
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] set_landscape_material: {e}")
        return False


def create_landscape_material(layer_names, save_path="/Game/Landscape",
                              material_name="M_Landscape_Auto"):
    """Create a basic landscape material with a LandscapeLayerBlend node.

    Uses the C++ ``LandscapeBuilder.CreateBasicLandscapeMaterial`` to create
    a material with color parameters for each layer.  Sensible default colors
    are assigned based on layer name (Grass=green, Dirt=brown, Rock=gray, etc.).

    If the material already exists, returns its path without recreating.

    Args:
        layer_names: List of layer name strings (e.g. ``["Grass", "Dirt", "Rock"]``).
        save_path: Content folder to save the material into.
        material_name: Name for the material asset.

    Returns:
        Content path of the created material, or ``None`` on failure.
    """
    try:
        path = unreal.LandscapeBuilder.create_basic_landscape_material(
            layer_names, save_path, material_name)
        if path:
            unreal.log(f"[arbor.terrain] create_landscape_material: "
                       f"created '{path}' with layers {layer_names}")
            return str(path)
        unreal.log_error("[arbor.terrain] create_landscape_material: "
                         "C++ builder returned empty path")
        return None
    except AttributeError:
        unreal.log_error("[arbor.terrain] create_landscape_material: "
                         "LandscapeBuilder.CreateBasicLandscapeMaterial not available")
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] create_landscape_material: {e}")
        return None


# ---------------------------------------------------------------------------
# Procedural terrain generation
# ---------------------------------------------------------------------------

def create_rolling_hills(location=(0, 0, 0), component_count=8,
                         section_size=63, scale=(100, 100, 100),
                         frequency=4.0, amplitude=0.5, octaves=4,
                         seed=None, material_path=None, noise_type="fbm"):
    """Create a landscape with procedural rolling hills.

    Generates a noise-based heightmap and creates a landscape with it
    in one call using the C++ ``LandscapeBuilder``.

    Args:
        location: ``(x, y, z)`` world position.
        component_count: Components per axis (8 = 505x505 vertices).
        section_size: Quads per section (63 default).
        scale: ``(x, y, z)`` landscape scale. Z controls height range.
        frequency: Noise frequency (2.0 = gentle, 4.0 = moderate,
            8.0 = many small hills).
        amplitude: Height variation (0.0 = flat, 0.5 = moderate,
            1.0 = full range).
        octaves: Noise detail layers (1-8).
        seed: Random seed for reproducibility.
        material_path: Optional landscape material content path.
        noise_type: ``"fbm"`` for rolling hills, ``"ridge"`` for mountains.

    Returns:
        Dict with ``landscape`` (actor), ``heightmap_size`` (w, h),
        ``seed``, ``label``, or ``None`` on failure.
    """
    from arbor.noise import generate_heightmap, heightmap_to_uint16
    import random as _rand

    # Calculate dimensions
    size_info = get_landscape_size(section_size, 1, component_count, component_count)
    w, h = size_info["width"], size_info["height"]

    if seed is None:
        seed = _rand.randint(0, 2**31 - 1)

    unreal.log(f"[arbor.terrain] create_rolling_hills: generating {w}x{h} heightmap "
               f"(freq={frequency}, amp={amplitude}, octaves={octaves}, seed={seed})")

    # Generate noise heightmap as float list
    hmap_float = generate_heightmap(w, h, frequency=frequency, amplitude=amplitude,
                                    octaves=octaves, seed=seed, noise_type=noise_type)

    # Convert to uint16 list for C++
    hmap_uint16 = _float_heightmap_to_uint16_list(hmap_float)

    # Create landscape with heightmap baked in
    landscape = create_landscape(
        location=location, section_size=section_size,
        sections_per_component=1,
        component_count_x=component_count,
        component_count_y=component_count,
        scale=scale, material_path=material_path,
        height_data=hmap_uint16,
    )
    if landscape is None:
        unreal.log_error("[arbor.terrain] create_rolling_hills: landscape creation failed")
        return None

    label = landscape.get_actor_label()
    unreal.log(f"[arbor.terrain] create_rolling_hills: done — '{label}'")

    return {
        "landscape": landscape,
        "heightmap_size": (w, h),
        "seed": seed,
        "label": label,
    }


def _float_heightmap_to_uint16_list(hmap_float, base=32768, scale=16384):
    """Convert float [0,1] heightmap to list[int] of uint16 for C++."""
    result = []
    for val in hmap_float:
        u16 = int(base + (val - 0.5) * 2.0 * scale)
        u16 = max(0, min(65535, u16))
        result.append(u16)
    return result


def carve_river_valley(landscape_actor, river_points_frac, width_frac=0.05,
                        depth=0.15):
    """Modify a landscape heightmap to carve a river valley.

    Reads the current heightmap, lowers terrain along the river path
    with smooth falloff for natural banks, and writes it back.

    Args:
        landscape_actor: Landscape actor.
        river_points_frac: List of ``(x_frac, y_frac)`` in [0, 1]
            from ``arbor.noise.generate_river_path``.
        width_frac: Valley width as fraction of landscape size (0.05 = 5%).
        depth: Depth of carving as fraction of height range (0.0-1.0).

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] carve_river_valley: landscape_actor is None")
            return False

        # Read current heightmap as list[int]
        heights = get_heightmap_data(landscape_actor)
        if heights is None:
            unreal.log_error("[arbor.terrain] carve_river_valley: could not read heightmap")
            return False

        num_verts = len(heights)
        side = int(math.sqrt(num_verts))
        if side * side != num_verts:
            unreal.log_error(f"[arbor.terrain] carve_river_valley: non-square heightmap "
                             f"({num_verts} vertices)")
            return False

        w = h = side
        radius_pixels = max(1, int(width_frac * w))
        depth_u16 = int(depth * 32768)

        # Densify path: interpolate between control points
        dense_points = []
        for i in range(len(river_points_frac) - 1):
            x0, y0 = river_points_frac[i]
            x1, y1 = river_points_frac[i + 1]
            dist = math.sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2)
            steps = max(int(dist * w * 0.5), 2)
            for s in range(steps):
                t = s / steps
                dense_points.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
        if river_points_frac:
            dense_points.append(river_points_frac[-1])

        # Carve along each dense point
        for fx, fy in dense_points:
            cx = int(fx * (w - 1))
            cy = int(fy * (h - 1))

            for dy in range(-radius_pixels, radius_pixels + 1):
                py = cy + dy
                if py < 0 or py >= h:
                    continue
                for dx in range(-radius_pixels, radius_pixels + 1):
                    px = cx + dx
                    if px < 0 or px >= w:
                        continue

                    dist = math.sqrt(dx * dx + dy * dy)
                    if dist > radius_pixels:
                        continue

                    t = dist / radius_pixels
                    falloff = 0.5 * (1.0 + math.cos(math.pi * t))

                    idx = py * w + px
                    heights[idx] = max(0, int(heights[idx] - depth_u16 * falloff))

        # Apply modified heightmap
        if apply_heightmap(landscape_actor, heights):
            unreal.log(f"[arbor.terrain] carve_river_valley: carved with "
                       f"{len(dense_points)} points, radius={radius_pixels}px")
            return True
        return False

    except Exception as e:
        unreal.log_error(f"[arbor.terrain] carve_river_valley: {e}")
        return False


# ---------------------------------------------------------------------------
# Water bodies (requires Water Plugin)
# ---------------------------------------------------------------------------

def refresh_water_body(actor_or_name):
    """Force a WaterBodyRiver/Lake to rebuild its spline meshes.

    Delegates to LandscapeBuilder::RefreshWaterBody C++.

    Args:
        actor_or_name: The water body actor or its label string.

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    actor = (find_actor_by_name(actor_or_name)
             if isinstance(actor_or_name, str) else actor_or_name)
    if actor is None:
        unreal.log_error("[arbor.terrain] refresh_water_body: actor not found")
        return False

    try:
        return unreal.LandscapeBuilder.refresh_water_body(actor)
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] refresh_water_body: {e}")
        return False


def add_water_body_river(spline_points, label="River", width=500.0,
                         snap_to_terrain=False, enforce_downhill=True,
                         terrain_sample_offset=3000.0):
    """Spawn a WaterBodyRiver actor with a spline path.

    Delegates to LandscapeBuilder::AddWaterBodyRiver C++.

    Args:
        spline_points: List of ``(x, y, z)`` world coordinates for
            the river path control points.
        label: Editor label for the actor.
        width: River width in cm.
        snap_to_terrain: If ``True``, adjust each point's Z to match
            the terrain surface.
        enforce_downhill: If ``True`` (and ``snap_to_terrain`` is
            ``True``), ensure Z never increases along the path.

    Returns:
        The ``WaterBodyRiver`` actor, or ``None`` on failure.
    """
    try:
        import json
        pts = [list(pt) if not isinstance(pt, (list, tuple)) else
               [pt[0], pt[1], pt[2]] for pt in spline_points]
        params = json.dumps({
            "spline_points": pts,
            "label": label,
            "width": width,
            "snap_to_terrain": snap_to_terrain,
            "enforce_downhill": enforce_downhill,
        })
        result_json = unreal.LandscapeBuilder.add_water_body_river(params)
        result = json.loads(result_json)
        if not result.get("success"):
            unreal.log_error(f"[arbor.terrain] add_water_body_river: {result.get('error')}")
            return None
        # Find and return the spawned actor
        actor_name = result.get("actor_name", label)
        return find_actor_by_name(actor_name)
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] add_water_body_river: {e}")
        return None


def add_water_body_lake(location=(0, 0, 0), radius=1000.0, label="Lake"):
    """Spawn a WaterBodyLake actor.

    Delegates to LandscapeBuilder::AddWaterBodyLake C++.

    Args:
        location: ``(x, y, z)`` center position.
        radius: Approximate lake radius in cm.
        label: Editor label.

    Returns:
        The ``WaterBodyLake`` actor, or ``None`` on failure.
    """
    try:
        import json
        loc = _to_vector(location) if location else unreal.Vector(0, 0, 0)
        params = json.dumps({
            "location": [loc.x, loc.y, loc.z],
            "radius": radius,
            "label": label,
        })
        result_json = unreal.LandscapeBuilder.add_water_body_lake(params)
        result = json.loads(result_json)
        if not result.get("success"):
            unreal.log_error(f"[arbor.terrain] add_water_body_lake: {result.get('error')}")
            return None
        actor_name = result.get("actor_name", label)
        return find_actor_by_name(actor_name)
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] add_water_body_lake: {e}")
        return None


def _trace_terrain_z(x, y, actors_to_ignore=None):
    """Cast a ray downward to find the terrain surface Z at (x, y).

    Returns the Z coordinate of the hit point, or ``None`` if no hit.
    """
    start = unreal.Vector(x, y, 50000.0)
    end = unreal.Vector(x, y, -50000.0)
    ignore = actors_to_ignore or []
    try:
        result = unreal.SystemLibrary.line_trace_single(
            unreal.EditorLevelLibrary.get_editor_world(),
            start, end,
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            False, ignore,
            unreal.DrawDebugTrace.NONE, True)
        if result is not None:
            t = result.to_tuple()
            if t[0]:  # blocking_hit
                return t[5].z  # impact_point.z
    except Exception:
        pass
    return None


# ---------------------------------------------------------------------------
# Terrain height queries
# ---------------------------------------------------------------------------

def _find_landscape():
    """Find the first Landscape actor in the current level."""
    actors = find_actors_by_class("Landscape")
    if actors:
        return actors[0]
    actors = find_actors_by_class("LandscapeStreamingProxy")
    if actors:
        return actors[0]
    return None


def _get_heightmap_grid_info(landscape):
    """Compute heightmap grid dimensions and world coordinate mapping.

    Returns a dict with keys: ``heights``, ``width``, ``height``,
    ``bounds_min_x``, ``bounds_min_y``, ``bounds_max_x``, ``bounds_max_y``,
    ``cell_size_x``, ``cell_size_y``, or ``None`` on failure.
    """
    heights = get_heightmap_data(landscape)
    if heights is None:
        unreal.log_error("[arbor.terrain] _get_heightmap_grid_info: "
                         "could not read heightmap")
        return None

    num_verts = len(heights)
    side = int(math.sqrt(num_verts))
    if side * side != num_verts:
        unreal.log_error("[arbor.terrain] _get_heightmap_grid_info: "
                         f"non-square heightmap ({num_verts} vertices)")
        return None

    origin, extent = landscape.get_actor_bounds(False)
    bounds_min_x = origin.x - extent.x
    bounds_min_y = origin.y - extent.y
    bounds_max_x = origin.x + extent.x
    bounds_max_y = origin.y + extent.y

    cell_size_x = (bounds_max_x - bounds_min_x) / max(1, side - 1)
    cell_size_y = (bounds_max_y - bounds_min_y) / max(1, side - 1)

    return {
        "heights": heights,
        "width": side,
        "height": side,
        "bounds_min_x": bounds_min_x,
        "bounds_min_y": bounds_min_y,
        "bounds_max_x": bounds_max_x,
        "bounds_max_y": bounds_max_y,
        "cell_size_x": cell_size_x,
        "cell_size_y": cell_size_y,
    }


def _find_extreme_point(landscape, region, mode):
    """Find the highest or lowest point on a landscape heightmap.

    Args:
        landscape: Landscape actor, or ``None`` to auto-detect.
        region: Optional ``(min_x, min_y, max_x, max_y)`` world bounding box.
        mode: ``"max"`` for highest, ``"min"`` for lowest.

    Returns:
        Dict ``{"x": float, "y": float, "z": float}`` or ``None``.
    """
    if landscape is None:
        landscape = _find_landscape()
    if landscape is None:
        unreal.log_error("[arbor.terrain] _find_extreme_point: "
                         "no landscape found in level")
        return None

    info = _get_heightmap_grid_info(landscape)
    if info is None:
        return None

    heights = info["heights"]
    w = info["width"]
    h = info["height"]

    # Determine search bounds in grid indices
    ix_min, iy_min = 0, 0
    ix_max, iy_max = w - 1, h - 1

    if region is not None:
        r_min_x, r_min_y, r_max_x, r_max_y = region
        ix_min = max(0, int(round(
            (r_min_x - info["bounds_min_x"]) / info["cell_size_x"])))
        iy_min = max(0, int(round(
            (r_min_y - info["bounds_min_y"]) / info["cell_size_y"])))
        ix_max = min(w - 1, int(round(
            (r_max_x - info["bounds_min_x"]) / info["cell_size_x"])))
        iy_max = min(h - 1, int(round(
            (r_max_y - info["bounds_min_y"]) / info["cell_size_y"])))
        if ix_min > ix_max or iy_min > iy_max:
            unreal.log_error("[arbor.terrain] _find_extreme_point: "
                             "region is outside landscape bounds")
            return None

    # Scan for extreme value
    use_max = mode == "max"
    best_val = -1 if use_max else 99999999
    best_ix, best_iy = 0, 0

    for iy in range(iy_min, iy_max + 1):
        row_start = iy * w
        for ix in range(ix_min, ix_max + 1):
            val = heights[row_start + ix]
            if (use_max and val > best_val) or \
               (not use_max and val < best_val):
                best_val = val
                best_ix = ix
                best_iy = iy

    # Convert grid index to world XY
    world_x = info["bounds_min_x"] + best_ix * info["cell_size_x"]
    world_y = info["bounds_min_y"] + best_iy * info["cell_size_y"]

    # Line trace for exact world Z
    world_z = _trace_terrain_z(world_x, world_y)
    if world_z is None:
        # Fallback: estimate from uint16 value using landscape transform
        loc = landscape.get_actor_location()
        scl = landscape.get_actor_scale3d()
        world_z = loc.z + (best_val - 32768) * scl.z / 512.0

    return {"x": world_x, "y": world_y, "z": world_z}


def sample_height(x, y):
    """Query the terrain surface height at world coordinates ``(x, y)``.

    Uses a downward line trace to find the terrain surface Z.  Works with
    any geometry, not just landscapes.

    Args:
        x: World X coordinate.
        y: World Y coordinate.

    Returns:
        Float Z height at the surface, or ``None`` if no terrain was hit.
    """
    return _trace_terrain_z(x, y)


def find_highest_point(landscape=None, region=None):
    """Find the highest point on a landscape.

    Reads the full heightmap and finds the vertex with maximum height,
    then converts to world coordinates.

    Args:
        landscape: Landscape actor.  If ``None``, auto-detects the first
            landscape in the level.
        region: Optional bounding box ``(min_x, min_y, max_x, max_y)`` in
            world coordinates to restrict the search area.

    Returns:
        Dict ``{"x": float, "y": float, "z": float}`` of the highest point
        in world coordinates, or ``None`` on failure.
    """
    return _find_extreme_point(landscape, region, "max")


def find_lowest_point(landscape=None, region=None):
    """Find the lowest point on a landscape.

    Reads the full heightmap and finds the vertex with minimum height,
    then converts to world coordinates.

    Args:
        landscape: Landscape actor.  If ``None``, auto-detects the first
            landscape in the level.
        region: Optional bounding box ``(min_x, min_y, max_x, max_y)`` in
            world coordinates to restrict the search area.

    Returns:
        Dict ``{"x": float, "y": float, "z": float}`` of the lowest point
        in world coordinates, or ``None`` on failure.
    """
    return _find_extreme_point(landscape, region, "min")


def find_flat_area(min_radius=500, landscape=None, region=None):
    """Find the flattest area on a landscape with at least the given radius.

    Reads the heightmap, computes slope per vertex, then finds the region
    of the specified radius with the lowest average slope.

    Args:
        min_radius: Minimum flat area radius in world cm (default 500).
        landscape: Landscape actor.  If ``None``, auto-detects.
        region: Optional ``(min_x, min_y, max_x, max_y)`` world bounding box.

    Returns:
        Dict ``{"x": float, "y": float, "z": float}`` of the center of the
        flattest area, or ``None`` on failure.
    """
    if landscape is None:
        landscape = _find_landscape()
    if landscape is None:
        unreal.log_error("[arbor.terrain] find_flat_area: "
                         "no landscape found in level")
        return None

    info = _get_heightmap_grid_info(landscape)
    if info is None:
        return None

    w = info["width"]
    h = info["height"]
    heights = info["heights"]

    # Compute slope map
    slopes = _compute_slope_map(heights, w, h)

    # Convert min_radius to grid cells
    avg_cell = (info["cell_size_x"] + info["cell_size_y"]) * 0.5
    radius_cells = max(1, int(round(min_radius / avg_cell)))

    # Build summed area table of slopes
    sat = [0.0] * ((w + 1) * (h + 1))
    sat_w = w + 1
    for iy in range(h):
        row_sum = 0.0
        for ix in range(w):
            row_sum += slopes[iy * w + ix]
            sat[(iy + 1) * sat_w + (ix + 1)] = (
                row_sum + sat[iy * sat_w + (ix + 1)])

    # Determine search bounds (inset by radius from edges)
    ix_min = radius_cells
    iy_min = radius_cells
    ix_max = w - 1 - radius_cells
    iy_max = h - 1 - radius_cells

    if region is not None:
        r_min_x, r_min_y, r_max_x, r_max_y = region
        rix_min = max(ix_min, int(round(
            (r_min_x - info["bounds_min_x"]) / info["cell_size_x"])))
        riy_min = max(iy_min, int(round(
            (r_min_y - info["bounds_min_y"]) / info["cell_size_y"])))
        rix_max = min(ix_max, int(round(
            (r_max_x - info["bounds_min_x"]) / info["cell_size_x"])))
        riy_max = min(iy_max, int(round(
            (r_max_y - info["bounds_min_y"]) / info["cell_size_y"])))
        ix_min, iy_min = rix_min, riy_min
        ix_max, iy_max = rix_max, riy_max

    if ix_min > ix_max or iy_min > iy_max:
        unreal.log_error("[arbor.terrain] find_flat_area: landscape too small "
                         f"for radius {min_radius} or region out of bounds")
        return None

    # Search for minimum average slope in box of side 2*radius_cells+1
    best_avg = 999999.0
    best_ix, best_iy = ix_min, iy_min
    box = radius_cells  # half-size of the query box

    for cy in range(iy_min, iy_max + 1):
        y1 = cy - box
        y2 = cy + box + 1  # exclusive in SAT
        for cx in range(ix_min, ix_max + 1):
            x1 = cx - box
            x2 = cx + box + 1
            total = (sat[y2 * sat_w + x2]
                     - sat[y1 * sat_w + x2]
                     - sat[y2 * sat_w + x1]
                     + sat[y1 * sat_w + x1])
            if total < best_avg:
                best_avg = total
                best_ix = cx
                best_iy = cy

    # Convert to world coordinates
    world_x = info["bounds_min_x"] + best_ix * info["cell_size_x"]
    world_y = info["bounds_min_y"] + best_iy * info["cell_size_y"]

    world_z = _trace_terrain_z(world_x, world_y)
    if world_z is None:
        loc = landscape.get_actor_location()
        scl = landscape.get_actor_scale3d()
        val = heights[best_iy * w + best_ix]
        world_z = loc.z + (val - 32768) * scl.z / 512.0

    return {"x": world_x, "y": world_y, "z": world_z}


# Water body helpers (_snap_river_points_to_terrain, _set_water_body_property,
# _find_spline_component) removed — logic moved to LandscapeBuilder C++.


# ---------------------------------------------------------------------------
# Combined terrain + river pipeline
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Layer painting
# ---------------------------------------------------------------------------

def create_layer_info(name, save_path="/Game/Landscape"):
    """Create a ``ULandscapeLayerInfoObject`` asset for a named layer.

    Args:
        name: Layer name (e.g. ``"Grass"``).  Must match the name used
            in the landscape material's ``LandscapeLayerBlend`` node.
        save_path: Content folder to save into.

    Returns:
        Content path of the created asset (e.g. ``/Game/Landscape/LI_Grass``),
        or ``None`` on failure.
    """
    try:
        path = unreal.LandscapeBuilder.create_layer_info_asset(name, save_path)
        if path:
            unreal.log(f"[arbor.terrain] create_layer_info: created '{name}' at {path}")
            return str(path)
        unreal.log_error(f"[arbor.terrain] create_layer_info: failed for '{name}'")
        return None
    except AttributeError:
        unreal.log_error("[arbor.terrain] create_layer_info: LandscapeBuilder not available")
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] create_layer_info: {e}")
        return None


def add_layer_to_landscape(landscape_actor, layer_info_path):
    """Register a layer info asset with a landscape.

    The layer must be registered before weight data can be written.

    Args:
        landscape_actor: Landscape actor.
        layer_info_path: Content path to a ``ULandscapeLayerInfoObject``
            asset (from :func:`create_layer_info`).

    Returns:
        ``True`` on success.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] add_layer_to_landscape: landscape is None")
            return False
        result = unreal.LandscapeBuilder.add_layer_to_landscape(
            landscape_actor, layer_info_path)
        return bool(result)
    except AttributeError:
        unreal.log_error("[arbor.terrain] add_layer_to_landscape: LandscapeBuilder not available")
        return False
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] add_layer_to_landscape: {e}")
        return False


def get_landscape_layers(landscape_actor):
    """List all registered layer names on a landscape.

    Args:
        landscape_actor: Landscape actor.

    Returns:
        ``list[str]`` of layer names, or empty list on failure.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] get_landscape_layers: landscape is None")
            return []
        layers = unreal.LandscapeBuilder.get_landscape_layers(landscape_actor)
        return [str(l) for l in layers] if layers else []
    except AttributeError:
        unreal.log_error("[arbor.terrain] get_landscape_layers: LandscapeBuilder not available")
        return []
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] get_landscape_layers: {e}")
        return []


def set_layer_weights(landscape_actor, layer_name, weights):
    """Write per-vertex weight data for a named layer.

    Args:
        landscape_actor: Landscape actor.
        layer_name: Name of the layer (must be registered).
        weights: ``list[int]`` of uint8 values (0-255), OR ``bytearray``.
            Length must match the landscape vertex count.

    Returns:
        ``True`` on success.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] set_layer_weights: landscape is None")
            return False

        if isinstance(weights, (bytes, bytearray)):
            values = list(weights)
        else:
            values = list(weights)

        result = unreal.LandscapeBuilder.set_layer_weights(
            landscape_actor, layer_name, values)

        if result:
            unreal.log(f"[arbor.terrain] set_layer_weights: applied {len(values)} "
                       f"values for '{layer_name}'")
        else:
            unreal.log_error(f"[arbor.terrain] set_layer_weights: failed for '{layer_name}'")
        return bool(result)
    except AttributeError:
        unreal.log_error("[arbor.terrain] set_layer_weights: LandscapeBuilder not available")
        return False
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] set_layer_weights: {e}")
        return False


def get_layer_weights(landscape_actor, layer_name):
    """Read per-vertex weight data for a named layer.

    Args:
        landscape_actor: Landscape actor.
        layer_name: Name of the layer.

    Returns:
        ``list[int]`` of uint8 values, or ``None`` on failure.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] get_layer_weights: landscape is None")
            return None
        data = unreal.LandscapeBuilder.get_layer_weights(landscape_actor, layer_name)
        if data is not None and len(data) > 0:
            return list(data)
        unreal.log_error(f"[arbor.terrain] get_layer_weights: no data for '{layer_name}'")
        return None
    except AttributeError:
        unreal.log_error("[arbor.terrain] get_layer_weights: LandscapeBuilder not available")
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.terrain] get_layer_weights: {e}")
        return None


def setup_landscape_layers(landscape_actor, layer_names,
                           save_path="/Game/Landscape"):
    """Create and register multiple layers on a landscape in one call.

    Creates ``ULandscapeLayerInfoObject`` assets for each name and
    registers them with the landscape.

    Args:
        landscape_actor: Landscape actor.
        layer_names: List of layer name strings (e.g. ``["Grass", "Dirt", "Rock"]``).
        save_path: Content folder for layer info assets.

    Returns:
        Dict with ``success``, ``layers``, ``layer_info_paths``.
    """
    if landscape_actor is None:
        unreal.log_error("[arbor.terrain] setup_landscape_layers: landscape is None")
        return {"success": False, "layers": [], "layer_info_paths": []}

    paths = []
    registered = []

    for name in layer_names:
        path = create_layer_info(name, save_path)
        if path is None:
            continue
        paths.append(path)

        if add_layer_to_landscape(landscape_actor, path):
            registered.append(name)

    unreal.log(f"[arbor.terrain] setup_landscape_layers: registered "
               f"{len(registered)}/{len(layer_names)} layers")

    return {
        "success": len(registered) == len(layer_names),
        "layers": registered,
        "layer_info_paths": paths,
    }


def paint_layer_circle(landscape_actor, layer_name, center_frac,
                       radius_frac, strength=1.0):
    """Paint a circular brush on a landscape layer.

    Args:
        landscape_actor: Landscape actor.
        layer_name: Name of the layer to paint.
        center_frac: ``(x, y)`` center in [0, 1] relative to landscape.
        radius_frac: Brush radius as fraction of landscape size.
        strength: Paint strength (0.0–1.0).

    Returns:
        ``True`` on success.
    """
    try:
        if landscape_actor is None:
            unreal.log_error("[arbor.terrain] paint_layer_circle: landscape is None")
            return False

        # Get current weights or create zeros
        existing = get_layer_weights(landscape_actor, layer_name)
        if existing is None:
            # Layer might have no data yet — create zero-filled
            heights = get_heightmap_data(landscape_actor)
            if heights is None:
                return False
            existing = [0] * len(heights)

        num_verts = len(existing)
        side = int(math.sqrt(num_verts))
        if side * side != num_verts:
            unreal.log_error("[arbor.terrain] paint_layer_circle: non-square vertex grid")
            return False

        w = h = side
        cx = int(center_frac[0] * (w - 1))
        cy = int(center_frac[1] * (h - 1))
        radius_pixels = max(1, int(radius_frac * w))
        paint_val = int(min(1.0, max(0.0, strength)) * 255)

        for dy in range(-radius_pixels, radius_pixels + 1):
            py = cy + dy
            if py < 0 or py >= h:
                continue
            for dx in range(-radius_pixels, radius_pixels + 1):
                px = cx + dx
                if px < 0 or px >= w:
                    continue

                dist = math.sqrt(dx * dx + dy * dy)
                if dist > radius_pixels:
                    continue

                t = dist / radius_pixels
                falloff = 0.5 * (1.0 + math.cos(math.pi * t))

                idx = py * w + px
                new_val = int(paint_val * falloff)
                existing[idx] = max(existing[idx], new_val)

        return set_layer_weights(landscape_actor, layer_name, existing)

    except Exception as e:
        unreal.log_error(f"[arbor.terrain] paint_layer_circle: {e}")
        return False


# ---------------------------------------------------------------------------
# Internal: slope computation
# ---------------------------------------------------------------------------

def _compute_slope_map(heights_u16, width, height):
    """Compute slope angle (degrees) per vertex from uint16 heightmap.

    Uses finite differences for gradient, converts magnitude to degrees.

    Returns:
        List of float slope angles in degrees, same length as input.
    """
    slopes = [0.0] * (width * height)

    for y in range(height):
        for x in range(width):
            idx = y * width + x

            # dz/dx
            if x == 0:
                dzdx = float(heights_u16[idx + 1] - heights_u16[idx])
            elif x == width - 1:
                dzdx = float(heights_u16[idx] - heights_u16[idx - 1])
            else:
                dzdx = float(heights_u16[idx + 1] - heights_u16[idx - 1]) * 0.5

            # dz/dy
            if y == 0:
                dzdy = float(heights_u16[idx + width] - heights_u16[idx])
            elif y == height - 1:
                dzdy = float(heights_u16[idx] - heights_u16[idx - width])
            else:
                dzdy = float(heights_u16[idx + width] - heights_u16[idx - width]) * 0.5

            gradient_mag = math.sqrt(dzdx * dzdx + dzdy * dzdy)
            # Convert to angle: gradient is in uint16 units per pixel
            # Scale factor: 1 uint16 unit ≈ 1/128 cm, 1 pixel ≈ scale cm
            # For slope angle, the absolute scale cancels out in atan
            slopes[idx] = math.atan(gradient_mag / 128.0) * (180.0 / math.pi)

    return slopes


# ---------------------------------------------------------------------------
# Auto-paint layers based on height and slope
# ---------------------------------------------------------------------------

def auto_paint_layers(landscape_actor, rules, seed=None,
                      save_path="/Game/Landscape"):
    """Automatically paint landscape layers based on height and slope.

    Reads the heightmap, computes slope per vertex, generates weight maps
    based on the provided rules, creates layer infos, and writes all weights.

    Each rule specifies height and slope thresholds with smooth falloff.
    Noise is added for natural-looking transitions.

    Args:
        landscape_actor: Landscape actor (must have a heightmap applied).
        rules: List of dicts, each with:

            - ``name`` (str): Layer name (required).
            - ``min_height`` (float, optional): Minimum normalized height [0, 1].
            - ``max_height`` (float, optional): Maximum normalized height [0, 1].
            - ``min_slope`` (float, optional): Minimum slope in degrees.
            - ``max_slope`` (float, optional): Maximum slope in degrees.
            - ``falloff`` (float, optional): Transition width (default 0.1).

        seed: Random seed for noise perturbation (optional).
        save_path: Content folder for layer info assets.

    Returns:
        Dict with ``success``, ``layers``, ``vertex_count``, or ``None``.
    """
    from arbor.noise import fbm_2d
    import random as _rand

    if landscape_actor is None:
        unreal.log_error("[arbor.terrain] auto_paint_layers: landscape is None")
        return None

    if not rules:
        unreal.log_error("[arbor.terrain] auto_paint_layers: no rules provided")
        return None

    # Read heightmap
    heights = get_heightmap_data(landscape_actor)
    if heights is None:
        unreal.log_error("[arbor.terrain] auto_paint_layers: could not read heightmap")
        return None

    num_verts = len(heights)
    side = int(math.sqrt(num_verts))
    if side * side != num_verts:
        unreal.log_error("[arbor.terrain] auto_paint_layers: non-square heightmap")
        return None

    w = h = side

    if seed is None:
        seed = _rand.randint(0, 2**31 - 1)

    unreal.log(f"[arbor.terrain] auto_paint_layers: {w}x{h}, "
               f"{len(rules)} layers, seed={seed}")

    # Normalize heights to [0, 1]
    h_min = min(heights)
    h_max = max(heights)
    h_range = max(1.0, float(h_max - h_min))
    heights_norm = [(float(v) - h_min) / h_range for v in heights]

    # Compute slope map
    slopes = _compute_slope_map(heights, w, h)

    # Generate raw weights for each layer
    num_layers = len(rules)
    raw_weights = []  # list of list[float], one per layer

    for layer_idx, rule in enumerate(rules):
        falloff = rule.get("falloff", 0.1)
        min_h = rule.get("min_height")
        max_h = rule.get("max_height")
        min_s = rule.get("min_slope")
        max_s = rule.get("max_slope")
        layer_seed = seed + layer_idx * 7

        weights = [0.0] * num_verts

        for i in range(num_verts):
            hn = heights_norm[i]
            sl = slopes[i]

            # Height factor
            h_weight = 1.0
            if min_h is not None:
                if hn < min_h:
                    h_weight = 0.0
                elif hn < min_h + falloff:
                    h_weight = (hn - min_h) / falloff
            if max_h is not None:
                if hn > max_h:
                    h_weight = 0.0
                elif hn > max_h - falloff:
                    h_weight = min(h_weight, (max_h - hn) / falloff)

            # Slope factor
            s_weight = 1.0
            if min_s is not None:
                if sl < min_s:
                    s_weight = 0.0
                elif sl < min_s + falloff * 90:  # falloff in degrees
                    s_weight = (sl - min_s) / (falloff * 90)
            if max_s is not None:
                if sl > max_s:
                    s_weight = 0.0
                elif sl > max_s - falloff * 90:
                    s_weight = min(s_weight, (max_s - sl) / (falloff * 90))

            combined = h_weight * s_weight

            # Noise perturbation for natural edges
            if combined > 0.0 and falloff > 0.0:
                x = i % w
                y = i // w
                noise_val = fbm_2d(x * 0.02, y * 0.02, octaves=3, seed=layer_seed)
                combined *= max(0.0, min(1.0, combined + (noise_val - 0.5) * falloff * 2))

            weights[i] = max(0.0, combined)

        raw_weights.append(weights)

    # Normalize per-vertex: all layers sum to 255
    layer_data = []  # list of list[int], one per layer
    for _ in range(num_layers):
        layer_data.append([0] * num_verts)

    for i in range(num_verts):
        total = sum(raw_weights[l][i] for l in range(num_layers))
        if total > 0.0:
            for l in range(num_layers):
                layer_data[l][i] = int((raw_weights[l][i] / total) * 255)
        else:
            # Default: first layer gets full weight
            layer_data[0][i] = 255

    # Setup layers and write weights
    layer_names = [r["name"] for r in rules]
    setup_result = setup_landscape_layers(landscape_actor, layer_names, save_path)

    painted = []
    for l, name in enumerate(layer_names):
        if set_layer_weights(landscape_actor, name, layer_data[l]):
            painted.append(name)
        else:
            unreal.log_warning(f"[arbor.terrain] auto_paint_layers: "
                               f"failed to paint '{name}'")

    # Verify writes persisted by reading back the first painted layer and
    # checking for non-zero values.  set_layer_weights can return True even
    # when the data silently fails to persist (e.g. edit-layer merge issue).
    verified = False
    if painted:
        verify_name = painted[0]
        readback = get_layer_weights(landscape_actor, verify_name)
        if readback is not None and any(v > 0 for v in readback):
            verified = True
        else:
            unreal.log_warning(
                f"[arbor.terrain] auto_paint_layers: verification failed — "
                f"layer '{verify_name}' has all-zero weights after writing")

    unreal.log(f"[arbor.terrain] auto_paint_layers: painted {len(painted)}/{num_layers} "
               f"layers on {num_verts} vertices (verified={verified})")

    result = {
        "success": len(painted) == num_layers and verified,
        "layers": painted,
        "vertex_count": num_verts,
        "verified": verified,
    }

    # Create and assign a basic landscape material if none is set
    try:
        current_mat = landscape_actor.get_editor_property("landscape_material")
        if current_mat is None:
            mat_path = create_landscape_material(
                layer_names, save_path, "M_Landscape_Auto")
            if mat_path:
                set_landscape_material(landscape_actor, mat_path)
                result["material"] = mat_path
                unreal.log(f"[arbor.terrain] auto_paint_layers: "
                           f"created and assigned material '{mat_path}'")
    except Exception as e:
        unreal.log_warning(f"[arbor.terrain] auto_paint_layers: "
                           f"could not auto-create material: {e}")

    return result


def setup_terrain_with_river(location=(0, 0, 0), component_count=8,
                             section_size=63, scale=(100, 100, 100),
                             frequency=4.0, amplitude=0.5, octaves=4,
                             seed=None, river=True, river_width=500.0,
                             material_path=None):
    """Create a complete rolling hills landscape with an optional river.

    Full pipeline:
      1. Generates rolling hills heightmap.
      2. Creates landscape with heightmap via C++ ``LandscapeBuilder``.
      3. If ``river=True``: generates a river path, carves a valley,
         and spawns a ``WaterBodyRiver``.
      4. Applies the landscape material (if provided).

    Args:
        location: ``(x, y, z)`` world position.
        component_count: Components per axis (default 8).
        section_size: Quads per section (default 63).
        scale: ``(x, y, z)`` landscape scale.
        frequency: Noise frequency.
        amplitude: Height variation (0.0-1.0).
        octaves: Noise detail layers.
        seed: Random seed.
        river: Whether to add a river.
        river_width: River width in cm.
        material_path: Landscape material content path.

    Returns:
        Dict with ``landscape`` (actor), ``river`` (actor or None),
        ``seed``, ``heightmap_size``, ``label``, or ``None`` on failure.
    """
    from arbor.noise import generate_heightmap, generate_river_path
    import random as _rand

    # Calculate dimensions
    size_info = get_landscape_size(section_size, 1, component_count, component_count)
    w, h = size_info["width"], size_info["height"]

    if seed is None:
        seed = _rand.randint(0, 2**31 - 1)

    unreal.log(f"[arbor.terrain] setup_terrain_with_river: {w}x{h}, seed={seed}")

    # Generate heightmap as float list
    hmap_float = generate_heightmap(w, h, frequency=frequency, amplitude=amplitude,
                                    octaves=octaves, seed=seed, noise_type="fbm")

    # Convert to uint16 list
    hmap_uint16 = _float_heightmap_to_uint16_list(hmap_float)

    # Create landscape with heightmap
    landscape = create_landscape(
        location=location, section_size=section_size,
        sections_per_component=1,
        component_count_x=component_count,
        component_count_y=component_count,
        scale=scale, material_path=material_path,
        height_data=hmap_uint16,
    )
    if landscape is None:
        return None

    # River
    river_actor = None
    if river:
        river_points_frac = generate_river_path(
            w, h, hmap_float, num_points=12, start_edge="north",
            seed=seed + 1, meander=0.3)

        # Carve valley
        carve_river_valley(landscape, river_points_frac,
                           width_frac=0.04, depth=0.12)

        # Convert fractional coords to world coords
        loc = location
        scl = scale
        landscape_world_w = (w - 1) * scl[0]
        landscape_world_h = (h - 1) * scl[1]

        world_points = []
        for fx, fy in river_points_frac:
            wx = loc[0] + fx * landscape_world_w
            wy = loc[1] + fy * landscape_world_h
            # Sample height for Z
            px = max(0, min(w - 1, int(fx * (w - 1))))
            py = max(0, min(h - 1, int(fy * (h - 1))))
            h_val = hmap_float[py * w + px]
            wz = loc[2] + (h_val - 0.5) * 2.0 * 16384 * scl[2] * 0.001953125
            world_points.append((wx, wy, wz))

        river_actor = add_water_body_river(
            world_points, label="River", width=river_width)

    label = landscape.get_actor_label()
    unreal.log(f"[arbor.terrain] setup_terrain_with_river: done — '{label}'"
               f"{', with river' if river_actor else ''}")

    return {
        "landscape": landscape,
        "river": river_actor,
        "heightmap_size": (w, h),
        "seed": seed,
        "label": label,
    }
