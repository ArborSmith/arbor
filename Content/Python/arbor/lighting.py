"""Arbor lighting — lights, atmosphere, post-processing, and scene presets.

Scene presets (setup_outdoor_scene, setup_indoor_scene) and post-process
volume creation delegate to ArborLightingTools C++. Individual light and
atmosphere spawn functions remain in Python as lightweight wrappers.
"""

import json
import unreal

from arbor.utils import _to_vector, _to_rotator, _to_linear_color, make_rotator


# ---------------------------------------------------------------------------
# Internal spawn helper
# ---------------------------------------------------------------------------

def _spawn_light(actor_class, location, rotation, label):
    """Spawn a light actor and return it."""
    loc = _to_vector(location) if location else unreal.Vector(0, 0, 0)
    rot = _to_rotator(rotation) if rotation else make_rotator(0, 0, 0)
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, loc, rot)
    if actor and label:
        actor.set_actor_label(label)
    return actor


def _actor_label(actor):
    """Return the actor label string, or None."""
    if actor is None:
        return None
    return actor.get_actor_label()


# ---------------------------------------------------------------------------
# Individual lights
# ---------------------------------------------------------------------------

def add_point_light(location=(0, 0, 0), intensity=5000.0, color=(1, 0.9, 0.8),
                    radius=1000.0, label=None):
    """Spawn a PointLight."""
    try:
        actor = _spawn_light(unreal.PointLight, location, None, label)
        if actor is None:
            return None
        comp = actor.point_light_component
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("light_color", _to_linear_color(color).to_color(True))
        comp.set_editor_property("attenuation_radius", radius)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_point_light: {e}")
        return None


def add_spot_light(location=(0, 0, 0), rotation=(0, 0, 0), intensity=5000.0,
                   color=(1, 1, 1), inner_angle=22.0, outer_angle=44.0, label=None):
    """Spawn a SpotLight."""
    try:
        actor = _spawn_light(unreal.SpotLight, location, rotation, label)
        if actor is None:
            return None
        comp = actor.spot_light_component
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("light_color", _to_linear_color(color).to_color(True))
        comp.set_editor_property("inner_cone_angle", inner_angle)
        comp.set_editor_property("outer_cone_angle", outer_angle)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_spot_light: {e}")
        return None


def add_directional_light(rotation=(-45, 0, 0), intensity=10.0,
                          color=(1, 0.95, 0.85), label="Sun"):
    """Spawn a DirectionalLight (sun)."""
    try:
        actor = _spawn_light(unreal.DirectionalLight, (0, 0, 0), rotation, label)
        if actor is None:
            return None
        comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
        if comp:
            comp.set_editor_property("intensity", intensity)
            comp.set_editor_property("light_color", _to_linear_color(color).to_color(True))
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_directional_light: {e}")
        return None


def add_rect_light(location=(0, 0, 0), rotation=(0, 0, 0), intensity=5000.0,
                   width=200.0, height=200.0, color=(1, 1, 1), label=None):
    """Spawn a RectLight."""
    try:
        actor = _spawn_light(unreal.RectLight, location, rotation, label)
        if actor is None:
            return None
        comp = actor.get_component_by_class(unreal.RectLightComponent)
        if comp:
            comp.set_editor_property("intensity", intensity)
            comp.set_editor_property("light_color", _to_linear_color(color).to_color(True))
            comp.set_editor_property("source_width", width)
            comp.set_editor_property("source_height", height)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_rect_light: {e}")
        return None


# ---------------------------------------------------------------------------
# Atmosphere / sky
# ---------------------------------------------------------------------------

def add_sky_atmosphere(label="SkyAtmosphere"):
    """Spawn a SkyAtmosphere actor."""
    try:
        actor = _spawn_light(unreal.SkyAtmosphere, (0, 0, 0), None, label)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_sky_atmosphere: {e}")
        return None


def add_sky_light(intensity=1.0, label="SkyLight"):
    """Spawn a SkyLight."""
    try:
        actor = _spawn_light(unreal.SkyLight, (0, 0, 500), None, label)
        if actor is None:
            return None
        comp = actor.get_component_by_class(unreal.SkyLightComponent)
        if comp:
            comp.set_editor_property("intensity", intensity)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_sky_light: {e}")
        return None


def add_exponential_height_fog(density=0.02, color=(0.5, 0.6, 0.7), label="Fog"):
    """Spawn an ExponentialHeightFog actor."""
    try:
        actor = _spawn_light(unreal.ExponentialHeightFog, (0, 0, 0), None, label)
        if actor is None:
            return None
        comp = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if comp:
            comp.set_editor_property("fog_density", density)
            comp.set_editor_property("fog_inscattering_luminance", _to_linear_color(color))
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_exponential_height_fog: {e}")
        return None


def add_volumetric_cloud(label="VolumetricCloud"):
    """Spawn a VolumetricCloud actor."""
    try:
        actor = _spawn_light(unreal.VolumetricCloud, (0, 0, 0), None, label)
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_volumetric_cloud: {e}")
        return None


# ---------------------------------------------------------------------------
# Post-process — delegates to ArborLightingTools C++
# ---------------------------------------------------------------------------

def add_post_process_volume(location=(0, 0, 0), extent=(10000, 10000, 10000),
                            infinite_extent=True, bloom_intensity=0.675,
                            auto_exposure_min=1.0, auto_exposure_max=1.0,
                            label="PostProcess"):
    """Spawn a PostProcessVolume via C++."""
    try:
        loc = _to_vector(location) if location else unreal.Vector(0, 0, 0)
        params = json.dumps({
            "location": [loc.x, loc.y, loc.z],
            "infinite_extent": infinite_extent,
            "bloom_intensity": bloom_intensity,
            "auto_exposure_min": auto_exposure_min,
            "auto_exposure_max": auto_exposure_max,
            "label": label,
        })
        result_json = unreal.ArborLightingTools.add_post_process_volume(params)
        result = json.loads(result_json)
        if not result.get("success"):
            unreal.log_error(f"[arbor.lighting] add_post_process_volume: {result.get('error')}")
            return None
        return None  # PostProcessVolume actor; C++ handles it
    except Exception as e:
        unreal.log_error(f"[arbor.lighting] add_post_process_volume: {e}")
        return None


# ---------------------------------------------------------------------------
# Scene presets — delegate to ArborLightingTools C++
# ---------------------------------------------------------------------------

def setup_outdoor_scene(sun_rotation=(-45, 30, 0), fog_density=0.01):
    """Create a complete outdoor lighting rig via C++.

    Spawns: DirectionalLight (sun), SkyAtmosphere, SkyLight,
    ExponentialHeightFog, VolumetricCloud, and a PostProcessVolume.
    Removes existing actors of those types first.

    Returns:
        Dict of spawned actor labels keyed by role.
    """
    params = json.dumps({
        "sun_rotation": list(sun_rotation),
        "fog_density": fog_density,
    })
    result_json = unreal.ArborLightingTools.setup_outdoor_scene(params)
    result = json.loads(result_json)
    unreal.log("[arbor.lighting] setup_outdoor_scene: complete (C++)")
    return result


def setup_indoor_scene(ambient_intensity=0.5):
    """Create a basic indoor lighting rig via C++.

    Spawns: SkyLight (low intensity), PostProcessVolume, and a RectLight.
    Removes existing SkyLight and PostProcessVolume first.

    Returns:
        Dict of spawned actor labels keyed by role.
    """
    params = json.dumps({"ambient_intensity": ambient_intensity})
    result_json = unreal.ArborLightingTools.setup_indoor_scene(params)
    result = json.loads(result_json)
    unreal.log("[arbor.lighting] setup_indoor_scene: complete (C++)")
    return result
