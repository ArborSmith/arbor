"""Arbor materials — create, configure, and assign materials (C++ backend)."""

import json
import unreal

from arbor.utils import _to_linear_color, _resolve_actor, load_asset, write_result


def _call_cpp(result_json):
    """Parse C++ JSON result, log errors, return parsed dict."""
    result = json.loads(result_json)
    if result.get("error"):
        unreal.log_error(f"[arbor.materials] {result['error']}")
    return result


# ---------------------------------------------------------------------------
# Public API — all delegate to ArborMaterialTools C++
# ---------------------------------------------------------------------------

def create_material(name, content_path="/Game/Materials", color=(1, 1, 1),
                    metallic=0.0, roughness=0.5):
    """Create a simple material with constant base color, metallic, and roughness.

    Returns:
        The created ``unreal.Material``, or ``None``.
    """
    params = json.dumps({
        "name": name, "content_path": content_path,
        "color": list(color), "metallic": metallic, "roughness": roughness,
    })
    result = _call_cpp(unreal.ArborMaterialTools.create_material(params))
    if not result.get("success"):
        return None
    return load_asset(result.get("asset_path", ""))


def create_material_from_textures(name, content_path="/Game/Materials",
                                  base_color_path=None, normal_path=None,
                                  roughness_path=None, metallic_path=None):
    """Create a material with texture samplers wired to material outputs.

    Returns:
        The created ``unreal.Material``, or ``None``.
    """
    p = {"name": name, "content_path": content_path}
    if base_color_path: p["base_color_path"] = base_color_path
    if normal_path: p["normal_path"] = normal_path
    if roughness_path: p["roughness_path"] = roughness_path
    if metallic_path: p["metallic_path"] = metallic_path

    result = _call_cpp(unreal.ArborMaterialTools.create_material_from_textures(json.dumps(p)))
    if not result.get("success"):
        return None
    return load_asset(result.get("asset_path", ""))


def create_material_instance(parent_path, name, content_path="/Game/Materials",
                             params=None):
    """Create a MaterialInstanceConstant from a parent material.

    Returns:
        The created ``MaterialInstanceConstant``, or ``None``.
    """
    p = {"parent_path": parent_path, "name": name, "content_path": content_path}
    if params:
        # Convert tuple values to lists for JSON serialization
        serializable_params = {}
        for k, v in params.items():
            if isinstance(v, tuple):
                serializable_params[k] = list(v)
            else:
                serializable_params[k] = v
        p["params"] = serializable_params

    result = _call_cpp(unreal.ArborMaterialTools.create_material_instance(json.dumps(p)))
    if not result.get("success"):
        return None
    return load_asset(result.get("asset_path", ""))


def create_parameterized_pbr_material(name, content_path="/Game/Materials",
                                      default_tiling=1.0):
    """Create a PBR base material with parameterized textures and tiling.

    Returns:
        Content path to the created Material, or ``None``.
    """
    params = json.dumps({
        "name": name, "content_path": content_path,
        "default_tiling": default_tiling,
    })
    result = _call_cpp(unreal.ArborMaterialTools.create_parameterized_pbr_material(params))
    if not result.get("success"):
        return None
    return result.get("asset_path")


def ensure_pbr_base_material(content_path="/Game/Materials"):
    """Return the path to the shared parameterized PBR base material.

    Creates it if it doesn't already exist.

    Returns:
        Content path string.
    """
    result = _call_cpp(unreal.ArborMaterialTools.ensure_pbr_base_material(content_path))
    if not result.get("success"):
        return None
    return result.get("asset_path")


def create_world_aligned_material(name, content_path="/Game/Materials",
                                  base_color_path=None, normal_path=None,
                                  roughness_path=None, metallic_path=None,
                                  ao_path=None, tiling_scale=200.0):
    """Create a PBR material using world-aligned (tri-planar) texture projection.

    Returns:
        Content path string to the created material, or ``None``.
    """
    p = {"name": name, "content_path": content_path, "tiling_scale": tiling_scale}
    if base_color_path: p["base_color_path"] = base_color_path
    if normal_path: p["normal_path"] = normal_path
    if roughness_path: p["roughness_path"] = roughness_path
    if metallic_path: p["metallic_path"] = metallic_path
    if ao_path: p["ao_path"] = ao_path

    result = _call_cpp(unreal.ArborMaterialTools.create_world_aligned_material(json.dumps(p)))
    if not result.get("success"):
        return None
    return result.get("asset_path")


def _assign_material_single(actor, material_path, slot=0):
    """Assign a material to one actor. Returns a result dict."""
    try:
        actor = _resolve_actor(actor)
        if actor is None:
            return {"success": False, "error": "Actor not found"}

        mat = material_path if not isinstance(material_path, str) else load_asset(material_path)
        if mat is None:
            return {"success": False, "error": f"Material not found: {material_path}"}

        comp = actor.get_component_by_class(unreal.StaticMeshComponent)
        if comp is None:
            label = actor.get_actor_label()
            return {"success": False, "error": f"No StaticMeshComponent on '{label}'"}

        comp.set_material(slot, mat)
        label = actor.get_actor_label()
        return {"success": True, "actor": label, "slot": slot}
    except Exception as e:
        return {"success": False, "error": str(e)}


def assign_material(actor, material_path, slot=0):
    """Assign a material to an actor's static mesh component."""
    result = _assign_material_single(actor, material_path, slot)
    write_result(result)
    return result


def assign_material_by_name(actor_name, material_path, slot=0):
    """Find actor(s) by name and assign a material."""
    if isinstance(actor_name, (list, tuple)):
        # Batch assignment via C++
        p = {
            "actor_names": list(actor_name),
            "material_path": material_path if isinstance(material_path, str) else "",
            "slot": slot,
        }
        result_json = unreal.ArborMaterialTools.assign_material(json.dumps(p))
        result = json.loads(result_json)
    else:
        result = _assign_material_single(actor_name, material_path, slot)
    write_result(result)
    return result
