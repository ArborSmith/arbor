"""Arbor mesh utilities — pivot correction, collision, and mesh data operations."""

import unreal
from arbor.utils import load_asset, save_asset


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def fix_mesh_pivot(asset_path, pivot="bottom"):
    """Permanently adjust the pivot point of a StaticMesh asset.

    Shifts all vertex positions so the pivot sits at the specified location
    relative to the mesh's bounding box.  Saves the asset after modification.

    Args:
        asset_path: Content path to the StaticMesh
                    (e.g. ``"/Game/GeneratedAssets/MyModel"``).
        pivot: ``"bottom"`` (min Z = 0), ``"center"`` (no-op),
               ``"top"`` (max Z = 0).

    Returns:
        dict with ``success``, ``offset``, ``bounds_before``,
        ``bounds_after``, ``message``.  ``None`` on critical failure.
    """
    valid = ("bottom", "center", "top")
    if pivot not in valid:
        unreal.log_error(
            f"[arbor.mesh] fix_mesh_pivot: invalid pivot '{pivot}'. "
            f"Must be one of {valid}"
        )
        return None

    mesh = load_asset(asset_path)
    if mesh is None:
        unreal.log_error(
            f"[arbor.mesh] fix_mesh_pivot: could not load '{asset_path}'"
        )
        return None

    if not isinstance(mesh, unreal.StaticMesh):
        unreal.log_error(
            f"[arbor.mesh] fix_mesh_pivot: '{asset_path}' is "
            f"{type(mesh).__name__}, not StaticMesh"
        )
        return None

    if pivot == "center":
        unreal.log("[arbor.mesh] fix_mesh_pivot: center pivot — no change needed")
        return {
            "success": True,
            "offset": [0, 0, 0],
            "message": "center pivot — no change",
        }

    # --- read current bounds -------------------------------------------------
    bbox = mesh.get_bounding_box()
    bounds_before = _box_to_dict(bbox)

    if pivot == "bottom":
        offset_z = -bbox.min.z
    else:  # "top"
        offset_z = -bbox.max.z

    if abs(offset_z) < 0.01:
        unreal.log("[arbor.mesh] fix_mesh_pivot: already at desired pivot")
        return {
            "success": True,
            "offset": [0, 0, 0],
            "bounds": bounds_before,
            "message": "already at desired pivot",
        }

    offset = unreal.Vector(0.0, 0.0, offset_z)
    unreal.log(f"[arbor.mesh] fix_mesh_pivot: shifting Z by {offset_z:.2f}")

    # --- shift vertices ------------------------------------------------------
    ok = _apply_vertex_offset(mesh, offset)
    if not ok:
        return None

    # --- save ----------------------------------------------------------------
    save_asset(asset_path)

    new_bbox = mesh.get_bounding_box()
    bounds_after = _box_to_dict(new_bbox)

    unreal.log(
        f"[arbor.mesh] fix_mesh_pivot: done — pivot={pivot}, "
        f"offset_z={offset_z:.2f}"
    )
    return {
        "success": True,
        "offset": [0.0, 0.0, round(offset_z, 4)],
        "bounds_before": bounds_before,
        "bounds_after": bounds_after,
        "message": f"pivot set to {pivot} (Z offset: {offset_z:.2f})",
    }


_COLLISION_MODES = ("box", "sphere", "capsule", "convex", "complex_simple", "complex_only")

# Simple shape mode → ScriptCollisionShapeType mapping
_SHAPE_MAP = {
    "box":     "BOX",
    "sphere":  "SPHERE",
    "capsule": "CAPSULE",
}


def fix_collision(asset_path, mode="complex_simple",
                  hull_count=4, max_hull_verts=16, hull_precision=100):
    """Set up collision on a StaticMesh asset.

    Replaces any existing collision with the requested type, then saves.

    Args:
        asset_path: Content path to the StaticMesh
                    (e.g. ``"/Game/Forest/Meshes/SM_BoulderLarge"``).
        mode: Collision mode — one of:

            * ``"box"``            — simple box (fast, default UE5 import)
            * ``"sphere"``         — simple sphere
            * ``"capsule"``        — capsule, good for characters
            * ``"convex"``         — auto-convex decomposition, good balance
            * ``"complex_simple"`` — render mesh as complex + auto convex as
              simple.  Best default for props.
            * ``"complex_only"``   — render mesh IS collision.  Most accurate
              but most expensive.  Good for static environment pieces.
        hull_count: Number of convex hulls for ``"convex"`` and
            ``"complex_simple"`` modes.  Default 4.
        max_hull_verts: Max vertices per hull.  Default 16.
        hull_precision: Precision of decomposition (1–100).  Default 100.

    Returns:
        Dict with ``success``, ``mode``, ``message`` on success, ``None`` on
        critical failure.
    """
    if mode not in _COLLISION_MODES:
        unreal.log_error(
            f"[arbor.mesh] fix_collision: invalid mode '{mode}'. "
            f"Must be one of {_COLLISION_MODES}"
        )
        return None

    mesh = load_asset(asset_path)
    if mesh is None:
        unreal.log_error(f"[arbor.mesh] fix_collision: could not load '{asset_path}'")
        return None
    if not isinstance(mesh, unreal.StaticMesh):
        unreal.log_error(
            f"[arbor.mesh] fix_collision: '{asset_path}' is "
            f"{type(mesh).__name__}, not StaticMesh"
        )
        return None

    try:
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    except Exception:
        sub = None

    try:
        # Remove existing collision first for a clean slate
        if sub:
            sub.remove_collisions(mesh)

        body_setup = mesh.get_editor_property("body_setup")

        if mode in _SHAPE_MAP:
            # --- Simple shape collision ---
            shape_enum = getattr(unreal.ScriptCollisionShapeType, _SHAPE_MAP[mode])
            if sub:
                sub.add_simple_collisions(mesh, shape_enum)
            body_setup.set_editor_property(
                "collision_trace_flag",
                unreal.CollisionTraceFlag.CTF_USE_DEFAULT,
            )
            msg = f"simple {mode} collision set"

        elif mode == "convex":
            # --- Auto-convex decomposition ---
            if sub:
                sub.set_convex_decomposition_collisions(
                    mesh, hull_count, max_hull_verts, hull_precision
                )
            body_setup.set_editor_property(
                "collision_trace_flag",
                unreal.CollisionTraceFlag.CTF_USE_DEFAULT,
            )
            msg = f"convex decomposition ({hull_count} hulls)"

        elif mode == "complex_simple":
            # --- Complex for complex queries, auto convex for simple queries ---
            # Generate convex hulls as simple collision
            if sub:
                sub.set_convex_decomposition_collisions(
                    mesh, hull_count, max_hull_verts, hull_precision
                )
            # Use both complex (render mesh) and simple (convex hulls)
            body_setup.set_editor_property(
                "collision_trace_flag",
                unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX,
            )
            msg = f"complex+simple collision ({hull_count} convex hulls)"

        elif mode == "complex_only":
            # --- Render mesh IS the collision ---
            body_setup.set_editor_property(
                "collision_trace_flag",
                unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,
            )
            msg = "complex-as-simple (render mesh is collision)"

        save_asset(asset_path)
        unreal.log(f"[arbor.mesh] fix_collision: '{asset_path}' — {msg}")
        return {"success": True, "mode": mode, "message": msg}

    except Exception as e:
        unreal.log_error(f"[arbor.mesh] fix_collision: {e}")
        return None


def disable_collision(asset_path):
    """Disable all collision on a StaticMesh asset.

    Useful for small decorations (grass, pebbles) that don't need collision.

    Args:
        asset_path: Content path to the StaticMesh.

    Returns:
        Dict with ``success``, ``message`` on success, ``None`` on failure.
    """
    mesh = load_asset(asset_path)
    if mesh is None:
        unreal.log_error(f"[arbor.mesh] disable_collision: could not load '{asset_path}'")
        return None
    if not isinstance(mesh, unreal.StaticMesh):
        unreal.log_error(
            f"[arbor.mesh] disable_collision: '{asset_path}' is "
            f"{type(mesh).__name__}, not StaticMesh"
        )
        return None

    try:
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if sub:
            sub.remove_collisions(mesh)

        body_setup = mesh.get_editor_property("body_setup")
        body_setup.set_editor_property(
            "collision_reponse",
            unreal.BodyCollisionResponse.BODY_COLLISION_DISABLED,
        )

        save_asset(asset_path)
        unreal.log(f"[arbor.mesh] disable_collision: '{asset_path}' — collision disabled")
        return {"success": True, "message": "collision disabled"}
    except Exception as e:
        unreal.log_error(f"[arbor.mesh] disable_collision: {e}")
        return None


def fix_mesh_scale(asset_path, scale=100.0):
    """Permanently scale a StaticMesh asset's vertices by a uniform factor.

    Useful for correcting unit mismatches — e.g. Meshy exports in metres
    while UE5 uses centimetres, so ``scale=100.0`` fixes the size.

    Tries these approaches in order:

    1. ``build_scale3d`` editor property (build-time scale, no vertex edit)
    2. MeshDescription vertex multiplication (permanent vertex rewrite)

    Args:
        asset_path: Content path to the StaticMesh
                    (e.g. ``"/Game/GeneratedAssets/MyModel"``).
        scale: Uniform scale factor.  Default ``100.0`` (metres → cm).

    Returns:
        Dict with ``success``, ``scale``, ``approach``, ``bounds_before``,
        ``bounds_after``, ``message``.  ``None`` on critical failure.
    """
    if abs(scale - 1.0) < 1e-6:
        return {
            "success": True,
            "scale": scale,
            "message": "scale is 1.0 — no change needed",
        }

    mesh = load_asset(asset_path)
    if mesh is None:
        unreal.log_error(
            f"[arbor.mesh] fix_mesh_scale: could not load '{asset_path}'"
        )
        return None

    if not isinstance(mesh, unreal.StaticMesh):
        unreal.log_error(
            f"[arbor.mesh] fix_mesh_scale: '{asset_path}' is "
            f"{type(mesh).__name__}, not StaticMesh"
        )
        return None

    bbox = mesh.get_bounding_box()
    bounds_before = _box_to_dict(bbox)

    # --- approach 1: build_scale3d property -----------------------------------
    try:
        sv = unreal.Vector(scale, scale, scale)
        mesh.set_editor_property("build_scale3d", sv)
        # Force rebuild so the property takes effect
        mesh.build()
        save_asset(asset_path)

        new_bbox = mesh.get_bounding_box()
        bounds_after = _box_to_dict(new_bbox)

        # Verify scale actually changed the bounds
        size_before = max(
            bbox.max.x - bbox.min.x,
            bbox.max.y - bbox.min.y,
            bbox.max.z - bbox.min.z,
        )
        size_after = max(
            new_bbox.max.x - new_bbox.min.x,
            new_bbox.max.y - new_bbox.min.y,
            new_bbox.max.z - new_bbox.min.z,
        )
        if size_before > 0.01 and abs(size_after / size_before - scale) < scale * 0.1:
            unreal.log(
                f"[arbor.mesh] fix_mesh_scale: build_scale3d succeeded "
                f"(factor {scale})"
            )
            return {
                "success": True,
                "scale": scale,
                "approach": "build_scale3d",
                "bounds_before": bounds_before,
                "bounds_after": bounds_after,
                "message": f"scaled by {scale}x via build_scale3d",
            }
        else:
            # build_scale3d didn't visibly change bounds — reset and try vertex approach
            mesh.set_editor_property("build_scale3d", unreal.Vector(1, 1, 1))
            unreal.log_warning(
                "[arbor.mesh] fix_mesh_scale: build_scale3d did not affect "
                "bounding box — falling through to vertex scaling"
            )
    except Exception as e:
        unreal.log_warning(
            f"[arbor.mesh] fix_mesh_scale: build_scale3d failed: {e}"
        )

    # --- approach 2: vertex manipulation --------------------------------------
    ok = _apply_vertex_scale(mesh, scale)
    if not ok:
        return None

    save_asset(asset_path)

    new_bbox = mesh.get_bounding_box()
    bounds_after = _box_to_dict(new_bbox)

    unreal.log(
        f"[arbor.mesh] fix_mesh_scale: done — scale={scale}, "
        f"approach=vertex_scale"
    )
    return {
        "success": True,
        "scale": scale,
        "approach": "vertex_scale",
        "bounds_before": bounds_before,
        "bounds_after": bounds_after,
        "message": f"scaled by {scale}x via vertex manipulation",
    }


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _box_to_dict(box):
    """Convert an ``unreal.Box`` to a JSON-serializable dict."""
    return {
        "min": {"X": box.min.x, "Y": box.min.y, "Z": box.min.z},
        "max": {"X": box.max.x, "Y": box.max.y, "Z": box.max.z},
    }


def _shift_vertex_positions(md, offset, lod_label):
    """Shift every vertex in a MeshDescription-like object by *offset*.

    Tries several API variations (plain int ids, VertexID wrapper)
    because the exact Python binding varies across UE5 versions.

    Returns the number of vertices shifted, or -1 on failure.
    """
    count = md.get_vertex_count()
    if count == 0:
        unreal.log_warning(f"[arbor.mesh] {lod_label}: 0 vertices")
        return 0

    # --- try 1: plain int vertex ids ----------------------------------------
    try:
        for i in range(count):
            pos = md.get_vertex_position(i)
            md.set_vertex_position(
                i,
                unreal.Vector(
                    pos.x + offset.x,
                    pos.y + offset.y,
                    pos.z + offset.z,
                ),
            )
        unreal.log(
            f"[arbor.mesh] {lod_label}: shifted {count} verts (plain int ids)"
        )
        return count
    except Exception as e:
        unreal.log_warning(
            f"[arbor.mesh] {lod_label}: plain int ids failed: {e}"
        )

    # --- try 2: VertexID wrapper --------------------------------------------
    try:
        for i in range(count):
            vid = unreal.VertexID(i)
            pos = md.get_vertex_position(vid)
            md.set_vertex_position(
                vid,
                unreal.Vector(
                    pos.x + offset.x,
                    pos.y + offset.y,
                    pos.z + offset.z,
                ),
            )
        unreal.log(
            f"[arbor.mesh] {lod_label}: shifted {count} verts (VertexID)"
        )
        return count
    except Exception as e:
        unreal.log_warning(
            f"[arbor.mesh] {lod_label}: VertexID wrapper failed: {e}"
        )

    return -1


def _apply_vertex_offset(mesh, offset):
    """Shift all vertex positions in *mesh* by *offset*.

    Tries three approaches in order:

    1. ``get_mesh_description`` + in-place modify + ``commit_mesh_description``
    2. ``get_static_mesh_description`` + modify + ``build_from_static_mesh_descriptions``
    3. ``create_static_mesh_description`` copy workflow

    Returns ``True`` on success.
    """
    # ---- determine LOD count ------------------------------------------------
    try:
        num_lods = mesh.get_num_lods()
    except Exception:
        try:
            num_lods = unreal.EditorStaticMeshLibrary.get_lod_count(mesh)
        except Exception:
            num_lods = 1
    if num_lods == 0:
        num_lods = 1

    # ---- approach 1: get_mesh_description (in-place) ------------------------
    try:
        any_shifted = False
        for lod in range(num_lods):
            md = mesh.get_mesh_description(lod)
            if md is None:
                unreal.log_warning(
                    f"[arbor.mesh] approach 1: LOD {lod} returned None, "
                    "skipping"
                )
                continue
            n = _shift_vertex_positions(md, offset, f"A1/LOD{lod}")
            if n < 0:
                raise RuntimeError(f"vertex shift failed for LOD {lod}")
            mesh.commit_mesh_description(lod)
            any_shifted = True
        if any_shifted:
            try:
                mesh.build()
            except Exception:
                pass  # some versions auto-rebuild on commit
            unreal.log(
                "[arbor.mesh] approach 1 (get_mesh_description) succeeded"
            )
            return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] approach 1 failed: {e}")

    # ---- approach 2: get_static_mesh_description ----------------------------
    try:
        descriptions = []
        for lod in range(num_lods):
            smd = mesh.get_static_mesh_description(lod)
            if smd is None:
                continue
            n = _shift_vertex_positions(smd, offset, f"A2/LOD{lod}")
            if n < 0:
                raise RuntimeError(f"vertex shift failed for LOD {lod}")
            descriptions.append(smd)
        if descriptions:
            mesh.build_from_static_mesh_descriptions(descriptions)
            unreal.log(
                "[arbor.mesh] approach 2 "
                "(get_static_mesh_description) succeeded"
            )
            return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] approach 2 failed: {e}")

    # ---- approach 3: copy via render data + build_from_descriptions ---------
    try:
        # Read vertex positions from LOD 0 render data
        lod0_rd = mesh.get_editor_property("render_data")
        if lod0_rd is None:
            raise RuntimeError("render_data is None")

        smd = mesh.create_static_mesh_description()
        # Copy source mesh description from LOD 0
        mesh.get_mesh_description(0)  # ensure loaded
        src = mesh.get_mesh_description(0)
        if src is not None:
            # try to copy data into smd from src
            n = _shift_vertex_positions(src, offset, "A3/LOD0")
            if n >= 0:
                mesh.commit_mesh_description(0)
                try:
                    mesh.build()
                except Exception:
                    pass
                unreal.log(
                    "[arbor.mesh] approach 3 (fallback copy) succeeded"
                )
                return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] approach 3 failed: {e}")

    unreal.log_error(
        "[arbor.mesh] all vertex shift approaches failed — "
        "check UE5 Output Log for details"
    )
    return False


def _scale_vertex_positions(md, scale, lod_label):
    """Scale every vertex in a MeshDescription-like object by *scale*.

    Same API-variation handling as ``_shift_vertex_positions``.
    Returns the number of vertices scaled, or -1 on failure.
    """
    count = md.get_vertex_count()
    if count == 0:
        unreal.log_warning(f"[arbor.mesh] {lod_label}: 0 vertices")
        return 0

    # --- try 1: plain int vertex ids ----------------------------------------
    try:
        for i in range(count):
            pos = md.get_vertex_position(i)
            md.set_vertex_position(
                i,
                unreal.Vector(
                    pos.x * scale,
                    pos.y * scale,
                    pos.z * scale,
                ),
            )
        unreal.log(
            f"[arbor.mesh] {lod_label}: scaled {count} verts (plain int ids)"
        )
        return count
    except Exception as e:
        unreal.log_warning(
            f"[arbor.mesh] {lod_label}: plain int ids failed: {e}"
        )

    # --- try 2: VertexID wrapper --------------------------------------------
    try:
        for i in range(count):
            vid = unreal.VertexID(i)
            pos = md.get_vertex_position(vid)
            md.set_vertex_position(
                vid,
                unreal.Vector(
                    pos.x * scale,
                    pos.y * scale,
                    pos.z * scale,
                ),
            )
        unreal.log(
            f"[arbor.mesh] {lod_label}: scaled {count} verts (VertexID)"
        )
        return count
    except Exception as e:
        unreal.log_warning(
            f"[arbor.mesh] {lod_label}: VertexID wrapper failed: {e}"
        )

    return -1


def _apply_vertex_scale(mesh, scale):
    """Scale all vertex positions in *mesh* by *scale*.

    Mirrors ``_apply_vertex_offset`` but multiplies instead of adding.
    Returns ``True`` on success.
    """
    # ---- determine LOD count ------------------------------------------------
    try:
        num_lods = mesh.get_num_lods()
    except Exception:
        try:
            num_lods = unreal.EditorStaticMeshLibrary.get_lod_count(mesh)
        except Exception:
            num_lods = 1
    if num_lods == 0:
        num_lods = 1

    # ---- approach 1: get_mesh_description (in-place) ------------------------
    try:
        any_scaled = False
        for lod in range(num_lods):
            md = mesh.get_mesh_description(lod)
            if md is None:
                unreal.log_warning(
                    f"[arbor.mesh] scale A1: LOD {lod} returned None, skipping"
                )
                continue
            n = _scale_vertex_positions(md, scale, f"scale-A1/LOD{lod}")
            if n < 0:
                raise RuntimeError(f"vertex scale failed for LOD {lod}")
            mesh.commit_mesh_description(lod)
            any_scaled = True
        if any_scaled:
            try:
                mesh.build()
            except Exception:
                pass
            unreal.log(
                "[arbor.mesh] vertex scale approach 1 "
                "(get_mesh_description) succeeded"
            )
            return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] vertex scale approach 1 failed: {e}")

    # ---- approach 2: get_static_mesh_description ----------------------------
    try:
        descriptions = []
        for lod in range(num_lods):
            smd = mesh.get_static_mesh_description(lod)
            if smd is None:
                continue
            n = _scale_vertex_positions(smd, scale, f"scale-A2/LOD{lod}")
            if n < 0:
                raise RuntimeError(f"vertex scale failed for LOD {lod}")
            descriptions.append(smd)
        if descriptions:
            mesh.build_from_static_mesh_descriptions(descriptions)
            unreal.log(
                "[arbor.mesh] vertex scale approach 2 "
                "(get_static_mesh_description) succeeded"
            )
            return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] vertex scale approach 2 failed: {e}")

    # ---- approach 3: fallback copy -----------------------------------------
    try:
        src = mesh.get_mesh_description(0)
        if src is not None:
            n = _scale_vertex_positions(src, scale, "scale-A3/LOD0")
            if n >= 0:
                mesh.commit_mesh_description(0)
                try:
                    mesh.build()
                except Exception:
                    pass
                unreal.log(
                    "[arbor.mesh] vertex scale approach 3 (fallback) succeeded"
                )
                return True
    except Exception as e:
        unreal.log_warning(f"[arbor.mesh] vertex scale approach 3 failed: {e}")

    unreal.log_error(
        "[arbor.mesh] all vertex scale approaches failed — "
        "check UE5 Output Log for details"
    )
    return False
