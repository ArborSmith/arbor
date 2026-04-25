"""Arbor concept art — import and associate AI-generated images with codex entries.

Bridges the FAL/Replicate image generation pipeline with codex data assets:
  - ``import_concept_art(image_path, codex_asset_path, image_name)`` — full pipeline
  - ``import_gallery_image(image_path, codex_asset_path, image_name)`` — add to gallery
"""

import json
import os

import unreal

from arbor.textures import import_texture
from arbor.utils import write_result


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def import_concept_art(image_disk_path, codex_asset_path, image_name,
                       content_path="/Game/Textures/ConceptArt"):
    """Import a disk image as UTexture2D and set it as primary concept art on a codex entry.

    Args:
        image_disk_path: Absolute path to a PNG or JPG file on disk.
        codex_asset_path: UE5 asset path of the codex entry
            (e.g. ``"/Game/GameCodex/Loc_Forest"``).
        image_name: Asset name for the imported texture
            (e.g. ``"T_Forest_ConceptArt"``).
        content_path: Content browser folder for the imported texture.

    Returns:
        Dict with ``"texture_path"`` and ``"success"``.
        Also written via ``write_result()`` for the MCP bridge.
    """
    image_disk_path = image_disk_path.replace("\\", "/")

    if not os.path.exists(image_disk_path):
        result = {"success": False, "error": f"File not found: {image_disk_path}"}
        write_result(result)
        return result

    # 1. Import texture
    texture_path = import_texture(image_disk_path, image_name, content_path)
    if not texture_path:
        result = {"success": False, "error": f"Failed to import: {image_disk_path}"}
        write_result(result)
        return result

    # 2. Set as concept art via C++ UFUNCTION
    result_json = unreal.ArborCodexImageTools.set_concept_art(
        codex_asset_path, texture_path, ""
    )
    result = json.loads(result_json)

    if not result.get("success", False):
        write_result(result)
        return result

    output = {
        "success": True,
        "texture_path": texture_path,
        "codex_asset_path": codex_asset_path,
    }
    write_result(output)
    return output


def import_gallery_image(image_disk_path, codex_asset_path, image_name,
                         content_path="/Game/Textures/ConceptArt"):
    """Import a disk image as UTexture2D and add it to a codex entry's gallery.

    Args:
        image_disk_path: Absolute path to a PNG or JPG file on disk.
        codex_asset_path: UE5 asset path of the codex entry.
        image_name: Asset name for the imported texture.
        content_path: Content browser folder for the imported texture.

    Returns:
        Dict with ``"texture_path"``, ``"gallery_count"``, and ``"success"``.
        Also written via ``write_result()`` for the MCP bridge.
    """
    image_disk_path = image_disk_path.replace("\\", "/")

    if not os.path.exists(image_disk_path):
        result = {"success": False, "error": f"File not found: {image_disk_path}"}
        write_result(result)
        return result

    # 1. Import texture
    texture_path = import_texture(image_disk_path, image_name, content_path)
    if not texture_path:
        result = {"success": False, "error": f"Failed to import: {image_disk_path}"}
        write_result(result)
        return result

    # 2. Add to gallery via C++ UFUNCTION
    result_json = unreal.ArborCodexImageTools.add_gallery_image(
        codex_asset_path, texture_path
    )
    result = json.loads(result_json)

    if not result.get("success", False):
        write_result(result)
        return result

    output = {
        "success": True,
        "texture_path": texture_path,
        "codex_asset_path": codex_asset_path,
        "gallery_count": result.get("gallery_count", 0),
    }
    write_result(output)
    return output
