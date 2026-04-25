"""Arbor inspect — runtime introspection of actors, assets, and Blueprints.

Provides property enumeration, Blueprint structure inspection, and fuzzy
property search.  Every function returns JSON-serializable data and never
raises exceptions to the caller.
"""

import unreal

from arbor.utils import (
    _resolve_actor,
    load_asset,
)


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

# Names that appear in dir() on UE5 Python wrappers but are methods, not
# user-facing properties.  Kept as a frozenset for O(1) lookup.
_SKIP_ATTRS = frozenset({
    # UObject core
    "cast", "static_class", "get_default_object", "get_class", "get_name",
    "get_fname", "get_full_name", "get_outer", "get_outermost",
    "get_path_name", "get_world", "get_typed_outer", "get_editor_property",
    "set_editor_property", "call_method", "modify",
    # Actor methods
    "get_actor_label", "set_actor_label", "get_actor_location",
    "set_actor_location", "get_actor_rotation", "set_actor_rotation",
    "get_actor_scale3d", "set_actor_scale3d", "get_actor_bounds",
    "get_components_by_class", "get_component_by_class",
    "get_attach_parent_actor", "get_attached_actors",
    "destroy_actor", "has_authority", "is_actor_being_destroyed",
    "set_actor_location_and_rotation", "add_actor_world_offset",
    "add_actor_world_rotation", "set_actor_hidden_in_game",
    "set_actor_enable_collision", "set_actor_tick_enabled",
    "was_recently_rendered", "get_overlapping_actors",
    "get_overlapping_components", "flush_net_dormancy",
    "k2_destroy_actor", "k2_attach_to",
    # Component methods
    "register_component", "unregister_component", "destroy_component",
    "set_active", "is_active", "deactivate", "activate",
    "get_owner", "get_attach_parent", "get_attach_children",
    "setup_attachment", "attach_to_component", "detach_from_component",
    "get_component_location", "get_component_rotation",
    "get_component_scale", "set_world_location", "set_world_rotation",
    "set_relative_location", "set_relative_rotation", "set_relative_scale3d",
    # Blueprint / asset internals
    "get_all_nodes", "get_all_nodes_of_class",
})


def _serialize_value(val):
    """Convert a UE5 property value to a JSON-serializable form."""
    if val is None:
        return None

    # Primitives
    if isinstance(val, (bool, int, float, str)):
        return val

    # Vector
    if isinstance(val, unreal.Vector):
        return {"X": val.x, "Y": val.y, "Z": val.z}

    # Rotator
    if isinstance(val, unreal.Rotator):
        return {"Pitch": val.pitch, "Yaw": val.yaw, "Roll": val.roll}

    # LinearColor
    if isinstance(val, unreal.LinearColor):
        return {"R": val.r, "G": val.g, "B": val.b, "A": val.a}

    # Color
    if isinstance(val, unreal.Color):
        return {"R": val.r, "G": val.g, "B": val.b, "A": val.a}

    # Transform
    if isinstance(val, unreal.Transform):
        return {
            "translation": _serialize_value(val.translation),
            "rotation": _serialize_value(val.rotation.rotator()),
            "scale": _serialize_value(val.scale3d),
        }

    # UObject subclasses — return class name + path, no recursion
    if isinstance(val, unreal.Object):
        return {
            "_type": val.get_class().get_name(),
            "_path": val.get_path_name(),
        }

    # Enum — UE5 Python enums stringify as "EnumType.VALUE"
    try:
        s = str(val)
        if "." in s and not s.startswith("<"):
            return s
    except Exception:
        pass

    # Lists / arrays
    if isinstance(val, (list, tuple)):
        return [_serialize_value(item) for item in val]

    # Fallback
    try:
        return str(val)
    except Exception:
        return "<unserializable>"


def _safe_get_property(obj, name):
    """Try to read a property via ``get_editor_property``.

    Returns:
        Tuple of ``(serialized_value, error_string_or_None)``.
    """
    try:
        val = obj.get_editor_property(name)
        return (_serialize_value(val), None)
    except Exception as e:
        return (None, str(e))


def _get_all_properties(obj):
    """Enumerate all readable properties on a UE5 object.

    Returns:
        List of dicts: ``[{"name": str, "value": any, "error"?: str}, ...]``
    """
    results = []
    seen = set()

    for attr_name in sorted(dir(obj)):
        if attr_name.startswith("_"):
            continue
        if attr_name in _SKIP_ATTRS:
            continue
        if attr_name in seen:
            continue
        seen.add(attr_name)

        # Skip callables (methods)
        try:
            attr_val = getattr(obj, attr_name, None)
            if callable(attr_val) and not isinstance(attr_val, unreal.Object):
                continue
        except Exception:
            pass

        val, err = _safe_get_property(obj, attr_name)
        entry = {"name": attr_name, "value": val}
        if err:
            entry["error"] = err
        results.append(entry)

    return results


def _get_property_names(obj):
    """Return just the property names without reading values.

    Faster than ``_get_all_properties`` when values are not needed.
    """
    names = []
    for attr_name in sorted(dir(obj)):
        if attr_name.startswith("_"):
            continue
        if attr_name in _SKIP_ATTRS:
            continue
        try:
            attr_val = getattr(obj, attr_name, None)
            if callable(attr_val) and not isinstance(attr_val, unreal.Object):
                continue
        except Exception:
            pass
        names.append(attr_name)
    return names


def _resolve_object(name_or_path):
    """Try to resolve as an actor first, then as an asset.

    Returns:
        A ``unreal.Object`` or ``None``.
    """
    # Try as actor
    obj = _resolve_actor(name_or_path)
    if obj is not None:
        return obj
    # Try as asset path
    if isinstance(name_or_path, str) and name_or_path.startswith("/"):
        return load_asset(name_or_path)
    return None


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def inspect_actor(actor_name):
    """Return detailed information about an actor in the current level.

    Includes class, transform, bounds, all components with their properties,
    and all actor-level properties.

    Args:
        actor_name: Actor label string or ``unreal.Actor`` reference.

    Returns:
        Dict with keys ``name``, ``class``, ``position``, ``rotation``,
        ``scale``, ``bounds``, ``components``, ``properties``.
        ``None`` if actor not found.
    """
    try:
        actor = _resolve_actor(actor_name)
        if actor is None:
            unreal.log_warning(f"[arbor.inspect] inspect_actor: '{actor_name}' not found")
            return None

        loc = actor.get_actor_location()
        rot = actor.get_actor_rotation()
        scl = actor.get_actor_scale3d()
        origin, extent = actor.get_actor_bounds(False)

        # Components
        comp_list = []
        for comp in actor.get_components_by_class(unreal.ActorComponent):
            comp_list.append({
                "name": comp.get_name(),
                "class": comp.get_class().get_name(),
                "properties": _get_all_properties(comp),
            })

        result = {
            "name": actor.get_actor_label(),
            "class": actor.get_class().get_name(),
            "position": {"X": loc.x, "Y": loc.y, "Z": loc.z},
            "rotation": {"Pitch": rot.pitch, "Yaw": rot.yaw, "Roll": rot.roll},
            "scale": {"X": scl.x, "Y": scl.y, "Z": scl.z},
            "bounds": {
                "min": {
                    "X": origin.x - extent.x,
                    "Y": origin.y - extent.y,
                    "Z": origin.z - extent.z,
                },
                "max": {
                    "X": origin.x + extent.x,
                    "Y": origin.y + extent.y,
                    "Z": origin.z + extent.z,
                },
            },
            "components": comp_list,
            "properties": _get_all_properties(actor),
        }

        unreal.log(f"[arbor.inspect] inspect_actor: '{actor_name}' — "
                    f"{len(comp_list)} components, {len(result['properties'])} properties")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] inspect_actor: {e}")
        return None


def inspect_blueprint(asset_path):
    """Return the structure of a Blueprint asset.

    Includes parent class, SCS components with properties, variables
    with types and defaults, and Class Default Object properties.

    Args:
        asset_path: Content path to a Blueprint
                    (e.g. ``"/Game/Blueprints/BP_Wolf"``).

    Returns:
        Dict with keys ``name``, ``asset_path``, ``parent_class``,
        ``components``, ``variables``, ``cdo_properties``.
        ``None`` on failure.
    """
    try:
        bp = load_asset(asset_path)
        if bp is None:
            return None
        if not isinstance(bp, unreal.Blueprint):
            unreal.log_warning(f"[arbor.inspect] inspect_blueprint: '{asset_path}' is not a Blueprint")
            return None

        # Parent class
        parent_class_name = ""
        parent_class_path = ""
        try:
            parent = bp.get_editor_property("parent_class")
            if parent:
                parent_class_name = parent.get_name()
                parent_class_path = parent.get_path_name()
        except Exception:
            pass

        # SCS components
        components = []
        try:
            scs = bp.get_editor_property("simple_construction_script")
            if scs:
                try:
                    nodes = scs.get_all_nodes()
                except Exception:
                    nodes = []
                for node in nodes:
                    comp_entry = {"name": "", "class": "", "properties": []}
                    try:
                        template = node.get_editor_property("component_template")
                        if template:
                            comp_entry["name"] = template.get_name()
                            comp_entry["class"] = template.get_class().get_name()
                            comp_entry["properties"] = _get_all_properties(template)
                    except Exception as ce:
                        comp_entry["error"] = str(ce)
                    # Check if inherited
                    try:
                        comp_entry["inherited"] = bool(
                            node.get_editor_property("internal_variable_name") == ""
                        )
                    except Exception:
                        comp_entry["inherited"] = False
                    components.append(comp_entry)
        except Exception:
            pass

        # Variables
        variables = []
        try:
            new_vars = bp.get_editor_property("new_variables")
            if new_vars:
                for var_desc in new_vars:
                    var_entry = {}
                    try:
                        var_entry["name"] = str(var_desc.get_editor_property("var_name"))
                    except Exception:
                        var_entry["name"] = "<unknown>"
                    try:
                        var_type = var_desc.get_editor_property("var_type")
                        var_entry["type"] = str(var_type) if var_type else "<unknown>"
                    except Exception:
                        var_entry["type"] = "<unknown>"
                    try:
                        var_entry["default"] = str(
                            var_desc.get_editor_property("default_value")
                        )
                    except Exception:
                        var_entry["default"] = ""
                    try:
                        var_entry["category"] = str(
                            var_desc.get_editor_property("category")
                        )
                    except Exception:
                        pass
                    variables.append(var_entry)
        except Exception:
            pass

        # CDO properties
        cdo_properties = []
        try:
            gen_class = bp.get_editor_property("generated_class")
            if gen_class:
                cdo = gen_class.get_default_object()
                if cdo:
                    cdo_properties = _get_all_properties(cdo)
        except Exception:
            pass

        result = {
            "name": bp.get_name(),
            "asset_path": asset_path,
            "parent_class": parent_class_name,
            "parent_class_path": parent_class_path,
            "components": components,
            "variables": variables,
            "cdo_properties": cdo_properties,
        }

        unreal.log(f"[arbor.inspect] inspect_blueprint: '{asset_path}' — "
                    f"{len(components)} components, {len(variables)} variables")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] inspect_blueprint: {e}")
        return None


def inspect_asset(asset_path):
    """Return class and properties of any asset.

    For Blueprint assets, delegates to ``inspect_blueprint`` for richer
    output.

    Args:
        asset_path: Content path (e.g. ``"/Game/Materials/M_Red"``).

    Returns:
        Dict with keys ``name``, ``asset_path``, ``class``,
        ``properties``.  ``None`` on failure.
    """
    try:
        asset = load_asset(asset_path)
        if asset is None:
            return None

        # Delegate Blueprints for richer inspection
        if isinstance(asset, unreal.Blueprint):
            return inspect_blueprint(asset_path)

        result = {
            "name": asset.get_name(),
            "asset_path": asset_path,
            "class": asset.get_class().get_name(),
            "properties": _get_all_properties(asset),
        }

        unreal.log(f"[arbor.inspect] inspect_asset: '{asset_path}' — "
                    f"{result['class']}, {len(result['properties'])} properties")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] inspect_asset: {e}")
        return None


def inspect_component(actor_name, component_name):
    """Deep-dive inspection of a single component on an actor.

    Args:
        actor_name: Actor label string or ``unreal.Actor`` reference.
        component_name: Component name (case-insensitive substring match).

    Returns:
        Dict with ``name``, ``class``, ``properties``.
        ``None`` if actor or component not found.
    """
    try:
        actor = _resolve_actor(actor_name)
        if actor is None:
            unreal.log_warning(f"[arbor.inspect] inspect_component: actor '{actor_name}' not found")
            return None

        target = component_name.lower()
        for comp in actor.get_components_by_class(unreal.ActorComponent):
            if target in comp.get_name().lower():
                result = {
                    "name": comp.get_name(),
                    "class": comp.get_class().get_name(),
                    "properties": _get_all_properties(comp),
                }
                unreal.log(f"[arbor.inspect] inspect_component: '{comp.get_name()}' — "
                           f"{len(result['properties'])} properties")
                return result

        unreal.log_warning(f"[arbor.inspect] inspect_component: "
                           f"component '{component_name}' not found on '{actor_name}'")
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] inspect_component: {e}")
        return None


def list_properties(actor_or_asset_path):
    """Return just the property names of an actor or asset.

    A quick lookup without reading values.

    Args:
        actor_or_asset_path: Actor label string or content path to an asset.

    Returns:
        Sorted list of property name strings.  Empty list on failure.
    """
    try:
        obj = _resolve_object(actor_or_asset_path)
        if obj is None:
            unreal.log_warning(f"[arbor.inspect] list_properties: '{actor_or_asset_path}' not found")
            return []

        names = _get_property_names(obj)
        unreal.log(f"[arbor.inspect] list_properties: '{actor_or_asset_path}' — {len(names)} properties")
        return names
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] list_properties: {e}")
        return []


def find_property(actor_or_asset_path, search_term):
    """Fuzzy-search properties by name substring.

    Args:
        actor_or_asset_path: Actor label string or content path.
        search_term: Case-insensitive substring to match against
                     property names.

    Returns:
        List of matching property dicts ``[{"name": str, "value": any}, ...]``.
        Empty list on failure or no matches.
    """
    try:
        obj = _resolve_object(actor_or_asset_path)
        if obj is None:
            unreal.log_warning(f"[arbor.inspect] find_property: '{actor_or_asset_path}' not found")
            return []

        needle = search_term.lower()
        props = _get_all_properties(obj)
        matches = [p for p in props if needle in p["name"].lower()]

        unreal.log(f"[arbor.inspect] find_property: '{search_term}' on "
                    f"'{actor_or_asset_path}' — {len(matches)} matches")
        return matches
    except Exception as e:
        unreal.log_error(f"[arbor.inspect] find_property: {e}")
        return []
