"""Arbor preview — open assets in UE5 for visual inspection.

Claude Code runs in a terminal and cannot display images.  These helpers
open textures, materials, meshes, and other assets in UE5's editor so the
user can see them directly and report back which variant they prefer.

Typical workflow::

    import arbor.preview as preview
    preview.preview_textures([
        "/Game/Textures/StoneWall_Var1",
        "/Game/Textures/StoneWall_Var2",
        "/Game/Textures/StoneWall_Var3",
    ])
    # Then ask the user which one they like.
"""

import unreal

from arbor.utils import load_asset


# ---------------------------------------------------------------------------
# Multi-asset preview (Arbor preview panel)
# ---------------------------------------------------------------------------

def preview_textures(texture_paths):
    """Open the Arbor preview panel in UE5 with textures for side-by-side comparison.

    Args:
        texture_paths: List of content paths to texture assets
            (e.g. ``["/Game/Textures/Var1", "/Game/Textures/Var2"]``).
    """
    if not texture_paths:
        unreal.log_warning("[arbor.preview] preview_textures: no paths provided")
        return

    args = " ".join(texture_paths)
    unreal.SystemLibrary.execute_console_command(None, f"Arbor.Preview {args}")
    unreal.log(f"[arbor.preview] Opened preview panel with {len(texture_paths)} textures")


# ---------------------------------------------------------------------------
# Single-asset previews
# ---------------------------------------------------------------------------

def preview_asset(asset_path):
    """Open an asset in UE5's asset editor/preview.

    Works for any asset type — textures, materials, meshes, Blueprints, etc.

    Args:
        asset_path: Content path (e.g. ``"/Game/Materials/M_Stone"``).
    """
    asset = load_asset(asset_path)
    if asset is None:
        unreal.log_error(f"[arbor.preview] Asset not found: {asset_path}")
        return
    unreal.EditorAssetLibrary.open_editor_for_assets([asset])
    unreal.log(f"[arbor.preview] Opened editor for: {asset_path}")


def preview_material(material_path):
    """Apply a material to a preview sphere in the viewport for a quick visual check.

    Spawns a temporary sphere actor with the material applied so the user
    can see how it looks in the scene.  Call ``remove_preview_sphere()``
    to clean it up afterwards.

    Args:
        material_path: Content path to a Material or MaterialInstance.
    """
    material = load_asset(material_path)
    if material is None:
        unreal.log_error(f"[arbor.preview] Material not found: {material_path}")
        return

    # Spawn a sphere at the viewport camera location so it's visible
    cam_loc, cam_rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
    forward = cam_rot.get_forward_vector()
    spawn_loc = cam_loc + forward * 300.0

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, spawn_loc
    )
    if actor is None:
        unreal.log_error("[arbor.preview] Failed to spawn preview sphere")
        return

    sphere_mesh = load_asset("/Engine/BasicShapes/Sphere.Sphere")
    actor.static_mesh_component.set_static_mesh(sphere_mesh)
    actor.static_mesh_component.set_material(0, material)
    actor.set_actor_label("ArborPreviewSphere")
    actor.set_actor_scale3d(unreal.Vector(2.0, 2.0, 2.0))

    unreal.log(f"[arbor.preview] Preview sphere with material: {material_path}")


def preview_mesh(mesh_path):
    """Open the static/skeletal mesh editor for the asset.

    Args:
        mesh_path: Content path to a StaticMesh or SkeletalMesh.
    """
    asset = load_asset(mesh_path)
    if asset is None:
        unreal.log_error(f"[arbor.preview] Mesh not found: {mesh_path}")
        return
    unreal.EditorAssetLibrary.open_editor_for_assets([asset])
    unreal.log(f"[arbor.preview] Opened mesh editor for: {mesh_path}")


# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------

def remove_preview_sphere():
    """Remove the temporary preview sphere spawned by ``preview_material()``.

    Returns:
        ``True`` if a sphere was found and deleted, ``False`` otherwise.
    """
    from arbor.utils import find_actor_by_name, delete_actor

    actor = find_actor_by_name("ArborPreviewSphere")
    if actor:
        delete_actor(actor)
        unreal.log("[arbor.preview] Removed preview sphere")
        return True
    unreal.log_warning("[arbor.preview] No preview sphere found")
    return False
