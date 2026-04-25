"""Arbor anchors — mesh anchor metadata extraction and management.

Thin wrapper around C++ ``ArborAnchorAnalyzer`` UFUNCTIONs.  Anchors are
snap/connection points on meshes (door frames, wall edges, road endpoints)
used by the environment graph system to place assets relative to each other
without specifying explicit coordinates.

Usage::

    import arbor.anchors as anchors

    # Analyze a mesh and generate anchors
    result = anchors.analyze_mesh("/Game/Fab/SM_House_01", asset_type="building")

    # Read back saved metadata
    meta = anchors.get_anchor_metadata("/Game/Fab/SM_House_01")

    # Manually set/override metadata
    anchors.set_anchor_metadata("/Game/Fab/SM_House_01", custom_metadata)
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
        unreal.log_error(f"[arbor.anchors] {error}")
    return result


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def analyze_mesh(asset_path, asset_type=None):
    """Analyze a static mesh and generate anchor metadata.

    Computes footprint, generates cardinal-face anchors (N/S/E/W),
    a bottom-center snap_base, and type-specific anchors.  Results
    are automatically saved to a ``.anchor.json`` sidecar file.

    Args:
        asset_path: Content path to the static mesh
            (e.g. ``"/Game/Fab/MedievalVillage/SM_House_01"``).
        asset_type: Optional type hint — one of ``"building"``,
            ``"road_segment"``, ``"prop"``, ``"wall"``, ``"floor"``.
            Adds type-specific anchors when set.

    Returns:
        Dict with keys: ``success``, ``asset_path``, ``asset_type``,
        ``footprint``, ``bounds_3d``, ``anchors``, ``sidecar_path``.
    """
    try:
        params = {"asset_path": asset_path}
        if asset_type:
            params["asset_type"] = asset_type
        result = _call_cpp(
            unreal.ArborAnchorAnalyzer.analyze_mesh(json.dumps(params))
        )
        if result.get("success"):
            unreal.log(
                f"[arbor.anchors] analyze_mesh: {asset_path} → "
                f"{len(result.get('anchors', []))} anchors"
            )
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] analyze_mesh: {e}")
        return {"success": False, "error": str(e)}


def get_anchor_metadata(asset_path):
    """Read anchor metadata from a ``.anchor.json`` sidecar file.

    Args:
        asset_path: Content path to the static mesh.

    Returns:
        Dict of anchor metadata, or ``{success: False, error: ...}``.
    """
    try:
        return _call_cpp(
            unreal.ArborAnchorAnalyzer.get_anchor_metadata(asset_path)
        )
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] get_anchor_metadata: {e}")
        return {"success": False, "error": str(e)}


def set_anchor_metadata(asset_path, metadata):
    """Write anchor metadata to a ``.anchor.json`` sidecar file.

    Args:
        asset_path: Content path to the static mesh.
        metadata: Dict of anchor metadata to save.

    Returns:
        Dict with ``success`` and ``sidecar_path``.
    """
    try:
        params = {"asset_path": asset_path, "metadata": metadata}
        result = _call_cpp(
            unreal.ArborAnchorAnalyzer.set_anchor_metadata(json.dumps(params))
        )
        if result.get("success"):
            unreal.log(
                f"[arbor.anchors] set_anchor_metadata: saved {result.get('sidecar_path')}"
            )
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] set_anchor_metadata: {e}")
        return {"success": False, "error": str(e)}


# ---------------------------------------------------------------------------
# Anchor colors by type
# ---------------------------------------------------------------------------

_ANCHOR_COLORS = {
    "floor_edge":      unreal.LinearColor(r=0.0, g=0.4, b=1.0, a=1.0),   # Blue
    "wall_edge":       unreal.LinearColor(r=1.0, g=0.5, b=0.0, a=1.0),   # Orange
    "wall_connector":  unreal.LinearColor(r=1.0, g=0.8, b=0.0, a=1.0),   # Yellow
    "snap_point":      unreal.LinearColor(r=1.0, g=0.0, b=0.0, a=1.0),   # Red
    "surface":         unreal.LinearColor(r=0.0, g=0.9, b=0.2, a=1.0),   # Green
    "door":            unreal.LinearColor(r=0.8, g=0.0, b=0.8, a=1.0),   # Purple
    "path_connect":    unreal.LinearColor(r=0.0, g=0.8, b=0.8, a=1.0),   # Cyan
    "road_edge":       unreal.LinearColor(r=0.5, g=0.5, b=0.5, a=1.0),   # Gray
}

_DEFAULT_COLOR = unreal.LinearColor(r=0.7, g=0.7, b=0.7, a=1.0)


def _get_debug_settings():
    """Read anchor debug settings from plugin config."""
    try:
        settings = unreal.ArborSettings.get_default_object()
        return (
            settings.get_editor_property("show_anchor_debug"),
            settings.get_editor_property("anchor_debug_duration"),
        )
    except Exception:
        return (True, -1.0)


# ---------------------------------------------------------------------------
# Debug-draw visualization
# ---------------------------------------------------------------------------

def visualize_anchors(asset_path, location=(0, 0, 0), radius=6.0,
                      arrow_length=30.0, duration=None, thickness=2.0):
    """Draw debug spheres and arrows at each anchor point in the editor viewport.

    Uses C++ ``DrawDebugHelpers`` which render directly in the editor
    (no PIE required). No actors are created.

    Colors by anchor type:
        Blue = floor_edge, Orange = wall_edge, Yellow = wall_connector,
        Red = snap_point, Green = surface, Purple = door, Cyan = path_connect.

    Call ``clear_debug_anchors()`` to flush persistent draws.

    Args:
        asset_path: Content path to the static mesh.
        location: World position ``(x, y, z)`` tuple — offset applied
            to all anchor positions (useful when mesh is already placed).
        radius: Debug sphere radius in cm (default 6).
        arrow_length: Length of direction arrows in cm.
        duration: Override draw duration in seconds. ``None`` = use
            plugin setting. ``-1`` = persistent until cleared.
        thickness: Line thickness for arrows.

    Returns:
        Dict with ``success`` and ``anchor_count``.
    """
    try:
        if duration is None:
            _, dur = _get_debug_settings()
            duration = dur

        params = {
            "asset_path": asset_path,
            "location": {"x": location[0], "y": location[1], "z": location[2]},
            "radius": radius,
            "arrow_length": arrow_length,
            "duration": duration,
            "thickness": thickness,
        }
        return _call_cpp(
            unreal.ArborAnchorAnalyzer.draw_anchors(json.dumps(params))
        )
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] visualize_anchors: {e}")
        return {"success": False, "error": str(e)}


def analyze_pack(folder_path, asset_type=None):
    """Analyze all static meshes in a content folder (batch AnalyzeMesh).

    Args:
        folder_path: Content folder path (e.g. ``"/Game/Fab/MedievalVillage"``).
        asset_type: Optional type hint applied to all meshes in the folder.

    Returns:
        Dict with ``success``, ``analyzed``, ``failed``, ``results``, ``errors``.
    """
    try:
        params = {"folder_path": folder_path}
        if asset_type:
            params["asset_type"] = asset_type
        result = _call_cpp(
            unreal.ArborAnchorAnalyzer.analyze_pack(json.dumps(params))
        )
        if result.get("success"):
            unreal.log(
                f"[arbor.anchors] analyze_pack: {folder_path} → "
                f"{result.get('analyzed', 0)} analyzed, {result.get('failed', 0)} failed"
            )
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] analyze_pack: {e}")
        return {"success": False, "error": str(e)}


def list_analyzed_assets(folder_path=""):
    """List all assets that have anchor metadata in registries.

    Args:
        folder_path: Optional folder filter (e.g. ``"/Game/Fab"``).
            Empty string returns all.

    Returns:
        Dict with ``success``, ``count``, ``assets``.
    """
    try:
        result = _call_cpp(
            unreal.ArborAnchorAnalyzer.list_analyzed_assets(folder_path)
        )
        if result.get("success"):
            unreal.log(
                f"[arbor.anchors] list_analyzed_assets: {result.get('count', 0)} assets"
            )
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] list_analyzed_assets: {e}")
        return {"success": False, "error": str(e)}


def find_compatible_anchors(from_asset, to_asset, filter_type=None):
    """Find compatible anchor pairs between two analyzed assets.

    Args:
        from_asset: Source asset content path.
        to_asset: Target asset content path.
        filter_type: Optional anchor type filter (e.g. ``"wall_snap"``).

    Returns:
        Dict with ``success`` and ``pairs`` array.
    """
    try:
        params = {"from_asset": from_asset, "to_asset": to_asset}
        if filter_type:
            params["filter_type"] = filter_type
        return _call_cpp(
            unreal.ArborAnchorAnalyzer.find_compatible_anchors(json.dumps(params))
        )
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] find_compatible_anchors: {e}")
        return {"success": False, "error": str(e)}


def clear_debug_anchors():
    """Flush all persistent debug draws from the editor viewport.

    Returns:
        Dict with ``success``.
    """
    try:
        unreal.ArborAnchorAnalyzer.flush_anchors()
        unreal.log("[arbor.anchors] clear_debug_anchors: flushed")
        return {"success": True}
    except Exception as e:
        unreal.log_error(f"[arbor.anchors] clear_debug_anchors: {e}")
        return {"success": False, "error": str(e)}
