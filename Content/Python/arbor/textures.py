"""Arbor textures — AI texture generation review, import, and material creation.

Bridges the MCP-generated images with the UE5 editor:
  - ``show_texture_review(images_json)`` — opens the review window
  - ``get_texture_review_result()`` — reads the user's selection
  - ``import_texture(path, name)`` — imports a disk image as UTexture2D
  - ``create_pbr_material(name, textures)`` — creates a Material with PBR maps
  - ``import_and_create_material(paths, name)`` — full pipeline
"""

import base64
import json
import os

import unreal

from arbor.utils import write_result, load_asset


# ---------------------------------------------------------------------------
# Review window bridge
# ---------------------------------------------------------------------------

def show_texture_review(images_json):
    """Open the Arbor Texture Review editor window with generated images.

    Args:
        images_json: JSON string (or dict) describing images to display.
            Format::

                {
                    "images": [
                        {
                            "path": "/abs/path/to/image.png",
                            "label": "Variant 1",
                            "pbr": {
                                "albedo": "/abs/path/albedo.png",
                                "normal": "/abs/path/normal.png",
                                ...
                            }
                        }
                    ],
                    "prompt": "weathered stone wall",
                    "source": "scenario"
                }
    """
    # Normalize input to a dict
    if isinstance(images_json, str):
        images_json = json.loads(images_json)

    # Accept a bare list of images and wrap it
    if isinstance(images_json, list):
        images_json = {"images": images_json}

    if not isinstance(images_json, dict):
        raise TypeError(
            f"show_texture_review: expected dict or list, got {type(images_json).__name__}. "
            f"Format: {{\"images\": [{{\"path\": ..., \"label\": ...}}]}}"
        )

    if "images" not in images_json:
        raise ValueError(
            "show_texture_review: missing 'images' key. "
            "Format: {\"images\": [{\"path\": ..., \"label\": ...}]}"
        )

    if not images_json["images"]:
        raise ValueError("show_texture_review: 'images' array is empty")

    encoded = base64.b64encode(json.dumps(images_json).encode("utf-8")).decode("ascii")
    unreal.SystemLibrary.execute_console_command(
        None, f"Arbor.TextureReview {encoded}"
    )
    unreal.log("[arbor.textures] Opened texture review window")


def get_texture_review_result():
    """Read the user's selection from the texture review window.

    Writes the result via ``write_result()`` for the MCP bridge to read.
    Returns ``{"status": "pending"}`` if no selection has been made yet.

    Returns:
        The result dict (also written to ``Saved/Arbor/last_result.json``).
    """
    project_dir = unreal.Paths.project_dir()
    result_path = os.path.join(project_dir, "Saved", "Arbor",
                               "texture_review_result.json")

    if not os.path.exists(result_path):
        result = {"status": "pending"}
        write_result(result)
        return result

    with open(result_path, "r", encoding="utf-8") as f:
        result = json.load(f)

    # Clean up after reading so we don't read stale results
    os.remove(result_path)

    write_result(result)
    return result


# ---------------------------------------------------------------------------
# Texture import
# ---------------------------------------------------------------------------

def import_texture(image_path, asset_name, content_path="/Game/Textures"):
    """Import an image file from disk as a UTexture2D asset.

    Args:
        image_path: Absolute path to a PNG or JPG file on disk.
        asset_name: Asset name in the Content Browser (e.g. ``"T_StoneWall_Albedo"``).
        content_path: Content browser folder (e.g. ``"/Game/Textures"``).

    Returns:
        The content path to the imported texture (e.g. ``"/Game/Textures/T_StoneWall_Albedo"``),
        or ``None`` on failure.
    """
    image_path = image_path.replace("\\", "/")

    if not os.path.exists(image_path):
        unreal.log_error(f"[arbor.textures] File not found: {image_path}")
        return None

    task = unreal.AssetImportTask()
    task.set_editor_property("automated", True)
    task.set_editor_property("destination_path", content_path)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("filename", image_path)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported_path = f"{content_path}/{asset_name}"
    asset = load_asset(imported_path)
    if asset:
        unreal.log(f"[arbor.textures] Imported texture: {imported_path}")
        return imported_path
    else:
        unreal.log_error(f"[arbor.textures] Failed to import: {image_path}")
        return None


# ---------------------------------------------------------------------------
# PBR material creation
# ---------------------------------------------------------------------------

def create_pbr_material(name, textures, content_path="/Game/Materials"):
    """Create a Material with PBR texture maps connected.

    Reuses ``arbor.materials`` helpers for texture wiring.

    Args:
        name: Material asset name (e.g. ``"M_StoneWall"``).
        textures: Dict mapping map types to content paths of imported textures.
            Supported keys: ``"albedo"``, ``"normal"``, ``"roughness"``,
            ``"metallic"``, ``"ao"``.
        content_path: Content browser folder for the material.

    Returns:
        The content path to the created Material (e.g. ``"/Game/Materials/M_StoneWall"``),
        or ``None`` on failure.
    """
    from arbor.materials import _get_asset_tools, _get_mel, _connect_texture

    try:
        factory = unreal.MaterialFactoryNew()
        mat = _get_asset_tools().create_asset(
            name, content_path, unreal.Material, factory
        )
        if mat is None:
            unreal.log_error(f"[arbor.textures] Failed to create material '{name}'")
            return None

        mel = _get_mel()
        if mel is None:
            unreal.EditorAssetLibrary.save_loaded_asset(mat)
            return f"{content_path}/{name}"

        # Map texture types to UE5 material properties
        property_map = {
            "albedo": unreal.MaterialProperty.MP_BASE_COLOR,
            "normal": unreal.MaterialProperty.MP_NORMAL,
            "roughness": unreal.MaterialProperty.MP_ROUGHNESS,
            "metallic": unreal.MaterialProperty.MP_METALLIC,
            "ao": unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,
        }

        for tex_type, mat_prop in property_map.items():
            tex_path = textures.get(tex_type)
            if tex_path:
                _connect_texture(mel, mat, tex_path, mat_prop)

        mel.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat)

        result_path = f"{content_path}/{name}"
        unreal.log(f"[arbor.textures] Created PBR material: {result_path}")
        return result_path

    except Exception as e:
        unreal.log_error(f"[arbor.textures] create_pbr_material: {e}")
        return None


# ---------------------------------------------------------------------------
# Full pipeline
# ---------------------------------------------------------------------------

def import_and_create_material(image_paths, material_name,
                               content_path="/Game/Textures/Generated",
                               material_path="/Game/Materials/Generated"):
    """Import texture images from disk and create a PBR material.

    This is the main entry point for the Claude-driven workflow: after the user
    selects an image in the review window, this function imports all associated
    texture files and wires them into a new Material.

    Args:
        image_paths: Dict mapping map types to absolute disk paths.
            Keys: ``"albedo"``, ``"normal"``, ``"roughness"``, ``"metallic"``, ``"ao"``.
            For non-PBR images (e.g. FAL), just pass ``{"albedo": "/path/to/image.png"}``.
        material_name: Name for the Material asset (e.g. ``"M_StoneWall"``).
        content_path: Content browser folder for imported textures.
        material_path: Content browser folder for the Material.

    Returns:
        Dict with ``"textures"`` (map type → content path) and ``"material"`` (content path).
        Also written via ``write_result()`` for the MCP bridge.
    """
    imported = {}
    base_name = material_name.replace("M_", "T_")

    for tex_type, disk_path in image_paths.items():
        if disk_path and os.path.exists(disk_path):
            asset_name = f"{base_name}_{tex_type.capitalize()}"
            result = import_texture(disk_path, asset_name, content_path)
            if result:
                imported[tex_type] = result

    mat_path = None
    if imported:
        mat_path = create_pbr_material(material_name, imported, material_path)

    output = {
        "textures": imported,
        "material": mat_path,
    }
    write_result(output)
    return output


def import_and_create_material_instance(image_paths, material_name,
                                        content_path="/Game/Textures/Generated",
                                        material_path="/Game/Materials/Generated",
                                        tiling=1.0,
                                        base_material_path=None):
    """Import textures and create a Material Instance with tiling support.

    Uses a parameterized base material with ``TextureSampleParameter2D`` nodes,
    avoiding the ``get_material_expressions()`` hang.  Tiling is adjustable
    via the Material Instance's ``Tiling`` scalar parameter.

    This is the **recommended** function for the AI texture pipeline.

    Args:
        image_paths: Dict mapping map types to absolute disk paths.
            Keys: ``"albedo"``, ``"normal"``, ``"roughness"``, ``"metallic"``, ``"ao"``.
        material_name: Name for the Material Instance (e.g. ``"MI_StoneWall"``).
        content_path: Content browser folder for imported textures.
        material_path: Content browser folder for the Material Instance.
        tiling: UV tiling multiplier (e.g. ``4.0`` for 4x repeat).  Default: 1.0.
        base_material_path: Content path to an existing parameterized base material.
            If ``None``, creates/reuses ``M_PBR_Parameterized`` in ``/Game/Materials``.

    Returns:
        Dict with ``"textures"``, ``"material_instance"``, and ``"base_material"`` paths.
        Also written via ``write_result()`` for the MCP bridge.
    """
    from arbor.materials import ensure_pbr_base_material, create_material_instance

    # 1. Ensure the parameterized base material exists
    if base_material_path is None:
        base_material_path = ensure_pbr_base_material()
    if base_material_path is None:
        unreal.log_error("[arbor.textures] Failed to create/find PBR base material")
        return {"textures": {}, "material_instance": None, "base_material": None}

    # 2. Import texture files from disk
    imported = {}
    base_name = material_name.replace("MI_", "T_").replace("M_", "T_")

    # Map from image_paths keys to parameter names on the base material
    param_name_map = {
        "albedo": "Albedo",
        "normal": "Normal",
        "roughness": "Roughness",
        "metallic": "Metallic",
        "ao": "AO",
    }

    for tex_type, disk_path in image_paths.items():
        if disk_path and os.path.exists(disk_path):
            asset_name = f"{base_name}_{tex_type.capitalize()}"
            result = import_texture(disk_path, asset_name, content_path)
            if result:
                imported[tex_type] = result

    # 3. Build Material Instance params: textures + tiling
    instance_params = {"Tiling": tiling}

    for tex_type, content_tex_path in imported.items():
        param_name = param_name_map.get(tex_type)
        if param_name:
            instance_params[param_name] = content_tex_path

    # 4. Create the Material Instance
    mic_path = None
    if imported:
        mic = create_material_instance(
            base_material_path, material_name, material_path,
            params=instance_params
        )
        if mic is not None:
            mic_path = f"{material_path}/{material_name}"

    output = {
        "textures": imported,
        "material_instance": mic_path,
        "base_material": base_material_path,
    }
    write_result(output)
    return output
