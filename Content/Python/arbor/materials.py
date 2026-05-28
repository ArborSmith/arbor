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


# ---------------------------------------------------------------------------
# Material graph editing (Phase 1 + Phase 2) — delegates to ArborMaterialGraphTools
#
# Identify expressions by stable Arbor IDs stamped into the expression's Desc
# field. IDs survive editor save/reload. Each granular call marks the asset
# dirty but does NOT recompile — call recompile_material() once when done, or
# use build_material(spec) which batches everything in one FMaterialUpdateContext.
# ---------------------------------------------------------------------------

def query_material(path):
    """Return a JSON snapshot of a material's expression graph.

    Returns:
        dict with keys: success, expressions, connections, flags, material_path.
    """
    return _call_cpp(unreal.ArborMaterialGraphTools.query_material(path))


def list_expression_types(filter=""):
    """List all UMaterialExpression subclasses, optional substring filter."""
    return _call_cpp(unreal.ArborMaterialGraphTools.list_material_expression_types(filter or ""))


def get_expression_class_params(class_name):
    """Reflect on an expression class for its editable properties + defaults."""
    return _call_cpp(unreal.ArborMaterialGraphTools.get_material_expression_class_params(class_name))


def add_expression(material_path, expression_class, *, expression_id=None,
                   properties=None, node_x=0, node_y=0):
    """Add an expression to a material. Returns dict with success + expression_id.

    Properties values match the JSON wire format:
        scalars/strings/bools as-is; FLinearColor as [r,g,b,a] or {R,G,B,A};
        FVector as [x,y,z]; UObject refs as asset path string; enums as the
        enum value name (e.g. "SAMPLERTYPE_Normal").
    """
    p = {
        "material_path": material_path,
        "expression_class": expression_class,
        "node_x": node_x, "node_y": node_y,
    }
    if expression_id:
        p["expression_id"] = expression_id
    if properties:
        p["properties"] = properties
    return _call_cpp(unreal.ArborMaterialGraphTools.add_material_expression(json.dumps(p)))


def remove_expression(material_path, expression_id):
    """Remove an expression by its sentinel ID."""
    return _call_cpp(unreal.ArborMaterialGraphTools.remove_material_expression_by_id(
        material_path, expression_id))


def set_expression_property(material_path, expression_id, property_name, value):
    """Set a property by name on an existing expression."""
    p = {
        "material_path": material_path,
        "expression_id": expression_id,
        "property_name": property_name,
        "value": value,
    }
    return _call_cpp(unreal.ArborMaterialGraphTools.set_material_expression_property(json.dumps(p)))


def connect_nodes(material_path, from_id, to_id, *, from_output="", to_input=""):
    """Wire one expression's output to another expression's input pin."""
    p = {
        "material_path": material_path,
        "from_id": from_id, "to_id": to_id,
        "from_output": from_output, "to_input": to_input,
    }
    return _call_cpp(unreal.ArborMaterialGraphTools.connect_material_nodes(json.dumps(p)))


def connect_output(material_path, expression_id, property, *, from_output=""):
    """Wire an expression's output to a material output channel.

    `property` accepts: BaseColor, Normal, Roughness, Metallic, EmissiveColor,
    Opacity, OpacityMask, AmbientOcclusion, WorldPositionOffset, Refraction,
    PixelDepthOffset, SubsurfaceColor, Tangent, Anisotropy, Specular.
    On UE 5.7+ also: FrontMaterial, SurfaceThickness, Displacement.
    """
    p = {
        "material_path": material_path,
        "expression_id": expression_id,
        "property": property,
        "from_output": from_output,
    }
    return _call_cpp(unreal.ArborMaterialGraphTools.connect_material_output(json.dumps(p)))


def recompile_material(path):
    """Explicit terminal recompile + save. Call once after batched edits."""
    return _call_cpp(unreal.ArborMaterialGraphTools.recompile_material_asset(path))


def build_material(spec):
    """Build or update a complete material from a JSON spec. Idempotent.

    See ArborMaterialGraphTools.h::BuildMaterial for spec schema.
    """
    return _call_cpp(unreal.ArborMaterialGraphTools.build_material(json.dumps(spec)))


def render_thumbnail(material_path, output_path, width=256, height=256):
    """Render a material's thumbnail to a PNG file via the UE thumbnail pipeline.

    Args:
        material_path: e.g. "/Game/Materials/M_Concrete"
        output_path:   absolute filesystem path; parent dirs auto-created
        width, height: pixel dims (default 256x256)
    """
    p = {"material_path": material_path, "output_path": output_path,
         "width": width, "height": height}
    return _call_cpp(unreal.ArborMaterialGraphTools.render_material_thumbnail(json.dumps(p)))
