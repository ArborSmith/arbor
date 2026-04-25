"""Arbor VFX — local fog volumes, Niagara spawning, and decals.

Use store-bought or hand-made Niagara assets.  This module provides
helpers to search for, spawn, and scatter them — but does **not** create
Niagara systems from scratch.
"""

import random

import unreal

from arbor.utils import (
    _to_vector,
    _to_rotator,
    _to_linear_color,
    load_asset,
)


# ---------------------------------------------------------------------------
# Local fog volumes
# ---------------------------------------------------------------------------

def add_local_fog_volume(location, extent=(500, 500, 200), density=5.0,
                         color=(0.8, 0.85, 0.9), label=None):
    """Spawn a LocalFogVolume for localised fog patches.

    Good for low-lying mist, cave fog, swamp haze.

    Args:
        location: ``(x, y, z)`` world position (centre of the volume).
        extent: ``(x, y, z)`` half-extents controlling the fog shape.
            The LocalFogVolume uses radial + height falloff rather than a
            box, so *extent* is applied as actor scale.
        density: Fog extinction/density.  Higher = thicker.  Default 5.0.
        color: ``(r, g, b)`` fog albedo colour, 0-1 floats.
        label: Optional editor label.

    Returns:
        The spawned ``unreal.LocalFogVolume`` actor, or ``None``.
    """
    try:
        loc = _to_vector(location)
        ext = _to_vector(extent)

        # Scale factor: default LocalFogVolume is ~100 units radius
        scale = unreal.Vector(ext.x / 100.0, ext.y / 100.0, ext.z / 100.0)

        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.LocalFogVolume, loc, unreal.Rotator()
        )
        if actor is None:
            unreal.log_error("[arbor.vfx] add_local_fog_volume: spawn failed")
            return None

        actor.set_actor_scale3d(scale)

        # Configure the fog component
        comp = actor.get_component_by_class(unreal.LocalFogVolumeComponent)
        if comp:
            comp.set_editor_property("radial_fog_extinction", density)
            comp.set_editor_property("height_fog_extinction", density)
            comp.set_editor_property("fog_albedo", _to_linear_color(color))

        if label:
            actor.set_actor_label(label)
        else:
            actor.set_actor_label("LocalFog")

        unreal.log(f"[arbor.vfx] add_local_fog_volume: spawned at {loc}")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.vfx] add_local_fog_volume: {e}")
        return None


# ---------------------------------------------------------------------------
# Niagara
# ---------------------------------------------------------------------------

def list_niagara_systems(search_paths=None):
    """Search the content browser for available Niagara System assets.

    Uses ``get_assets_by_path`` per search path then filters by class,
    which is much faster than a global class-filtered query (engine content
    has thousands of assets).

    Args:
        search_paths: Optional list of content paths to search
            (e.g. ``["/Game/", "/Engine/"]``).  Defaults to ``["/Game/"]``.
            Scanning ``/Engine/`` is slow and rarely needed; pass it
            explicitly if you want engine-bundled systems.

    Returns:
        List of asset path strings.
    """
    if search_paths is None:
        search_paths = ["/Game/"]

    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        results = []
        for sp in search_paths:
            path = sp.rstrip("/")
            try:
                assets = registry.get_assets_by_path(path, recursive=True)
                for a in assets:
                    class_path = str(a.asset_class_path)
                    if "NiagaraSystem" in class_path:
                        results.append(
                            str(a.package_name) + "." + str(a.asset_name)
                        )
            except Exception:
                pass

        unreal.log(
            f"[arbor.vfx] list_niagara_systems: found {len(results)} systems"
        )
        return results
    except Exception as e:
        unreal.log_error(f"[arbor.vfx] list_niagara_systems: {e}")
        return []


def spawn_niagara_system(system_path, location, rotation=(0, 0, 0),
                         scale=(1, 1, 1), auto_activate=True, label=None):
    """Spawn a NiagaraActor with the given Niagara System asset.

    Args:
        system_path: Content path to the NiagaraSystem asset.
        location: ``(x, y, z)`` world position.
        rotation: ``(pitch, yaw, roll)`` in degrees.
        scale: ``(x, y, z)`` scale factor.
        auto_activate: Whether the system starts playing immediately.
        label: Optional editor label.

    Returns:
        The spawned ``unreal.NiagaraActor``, or ``None``.
    """
    try:
        system_asset = load_asset(system_path)
        if system_asset is None:
            unreal.log_error(
                f"[arbor.vfx] spawn_niagara_system: could not load '{system_path}'"
            )
            return None

        loc = _to_vector(location)
        rot = _to_rotator(rotation)
        scl = _to_vector(scale)

        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.NiagaraActor, loc, rot
        )
        if actor is None:
            unreal.log_error("[arbor.vfx] spawn_niagara_system: spawn failed")
            return None

        actor.set_actor_scale3d(scl)

        # Set the Niagara system on the component
        comp = actor.get_component_by_class(unreal.NiagaraComponent)
        if comp:
            comp.set_asset(system_asset)
            comp.set_editor_property("auto_activate", auto_activate)

        if label:
            actor.set_actor_label(label)

        unreal.log(
            f"[arbor.vfx] spawn_niagara_system: '{system_path}' at {loc}"
        )
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.vfx] spawn_niagara_system: {e}")
        return None


# ---------------------------------------------------------------------------
# Decals
# ---------------------------------------------------------------------------

def list_decal_materials(search_paths=None):
    """Search the content browser for decal-compatible Material assets.

    Looks for materials with ``decal`` in the name or path.

    Args:
        search_paths: Optional list of content paths to search.
            Defaults to ``["/Game/"]``.

    Returns:
        List of asset path strings.
    """
    if search_paths is None:
        search_paths = ["/Game/"]

    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        results = []
        for sp in search_paths:
            path = sp.rstrip("/")
            try:
                assets = registry.get_assets_by_path(path, recursive=True)
                for a in assets:
                    class_path = str(a.asset_class_path)
                    if "Material" not in class_path:
                        continue
                    name_lower = str(a.asset_name).lower()
                    pkg_lower = str(a.package_name).lower()
                    if "decal" in name_lower or "decal" in pkg_lower:
                        results.append(
                            str(a.package_name) + "." + str(a.asset_name)
                        )
            except Exception:
                pass

        unreal.log(f"[arbor.vfx] list_decal_materials: found {len(results)}")
        return results
    except Exception as e:
        unreal.log_error(f"[arbor.vfx] list_decal_materials: {e}")
        return []


def spawn_decal(material_path, location, rotation=(0, -90, 0),
                size=(200, 200, 200), label=None):
    """Spawn a DecalActor at the given location.

    Default rotation ``(0, -90, 0)`` faces the decal downward for ground
    projection.

    Args:
        material_path: Content path to a decal Material or MaterialInstance.
        location: ``(x, y, z)`` world position.
        rotation: ``(pitch, yaw, roll)`` in degrees.
        size: ``(x, y, z)`` decal projection size in cm.
        label: Optional editor label.

    Returns:
        The spawned ``unreal.DecalActor``, or ``None``.
    """
    try:
        mat = load_asset(material_path)
        if mat is None:
            unreal.log_error(
                f"[arbor.vfx] spawn_decal: could not load material "
                f"'{material_path}'"
            )
            return None

        loc = _to_vector(location)
        rot = _to_rotator(rotation)

        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.DecalActor, loc, rot
        )
        if actor is None:
            unreal.log_error("[arbor.vfx] spawn_decal: spawn failed")
            return None

        comp = actor.get_component_by_class(unreal.DecalComponent)
        if comp:
            comp.set_decal_material(mat)
            comp.set_editor_property("decal_size", _to_vector(size))

        if label:
            actor.set_actor_label(label)

        unreal.log(f"[arbor.vfx] spawn_decal: '{material_path}' at {loc}")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.vfx] spawn_decal: {e}")
        return None


def scatter_decals(material_path, count, bounds_min, bounds_max,
                   size_min=100, size_max=300, random_rotation=True,
                   label_prefix="Decal", seed=None):
    """Scatter multiple ground decals randomly in an area.

    Good for ground moss, dirt patches, puddles.

    Args:
        material_path: Content path to a decal Material.
        count: Number of decals to scatter.
        bounds_min: ``(x, y, z)`` minimum corner of scatter region.
        bounds_max: ``(x, y, z)`` maximum corner of scatter region.
        size_min: Minimum decal size (uniform).
        size_max: Maximum decal size (uniform).
        random_rotation: Randomise yaw rotation for variety.
        label_prefix: Prefix for actor labels.
        seed: Optional random seed for reproducibility.

    Returns:
        List of spawned ``DecalActor`` objects.
    """
    if seed is not None:
        random.seed(seed)

    bmin = _to_vector(bounds_min)
    bmax = _to_vector(bounds_max)

    actors = []
    for i in range(count):
        x = random.uniform(bmin.x, bmax.x)
        y = random.uniform(bmin.y, bmax.y)
        z = random.uniform(bmin.z, bmax.z)

        s = random.uniform(size_min, size_max)
        yaw = random.uniform(0, 360) if random_rotation else 0

        lbl = f"{label_prefix}_{i}"
        actor = spawn_decal(
            material_path,
            location=(x, y, z),
            rotation=(0, -90, yaw),
            size=(s, s, s),
            label=lbl,
        )
        if actor:
            actors.append(actor)

    unreal.log(
        f"[arbor.vfx] scatter_decals: spawned {len(actors)}/{count} decals"
    )
    return actors
