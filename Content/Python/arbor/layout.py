"""Arbor layout — blockout primitives for level grayboxing.

Provides cubes, walls, floors, rooms, ramps, and stairs built from
``/Engine/BasicShapes/*`` static meshes.  Default cube is 100×100×100 cm
centred at origin, so all scale values are ``desired_cm / 100``.
"""

import math

import unreal

from arbor.utils import _to_vector, _to_rotator, spawn_actor, load_asset


# ---------------------------------------------------------------------------
# Shape mesh paths
# ---------------------------------------------------------------------------

SHAPE_MAP = {
    "cube": "/Engine/BasicShapes/Cube.Cube",
    "sphere": "/Engine/BasicShapes/Sphere.Sphere",
    "cylinder": "/Engine/BasicShapes/Cylinder.Cylinder",
    "cone": "/Engine/BasicShapes/Cone.Cone",
    "plane": "/Engine/BasicShapes/Plane.Plane",
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _spawn_mesh(mesh_path, location, rotation, scale, label):
    """Internal: spawn a StaticMeshActor with the given mesh."""
    loc = _to_vector(location)
    rot = _to_rotator(rotation)
    scl = _to_vector(scale)

    mesh = load_asset(mesh_path)
    if mesh is None:
        unreal.log_error(f"[arbor.layout] _spawn_mesh: could not load '{mesh_path}'")
        return None

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, loc, rot
    )
    if actor is None:
        unreal.log_error("[arbor.layout] _spawn_mesh: spawn failed")
        return None

    actor.static_mesh_component.set_static_mesh(mesh)
    actor.set_actor_scale3d(scl)
    if label:
        actor.set_actor_label(label)
    return actor


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def spawn_primitive(shape="cube", location=(0, 0, 0), scale=(1, 1, 1),
                    rotation=(0, 0, 0), label=None):
    """Spawn a BasicShapes static mesh actor.

    Args:
        shape: One of ``"cube"``, ``"sphere"``, ``"cylinder"``,
               ``"cone"``, ``"plane"``.
        location: ``(x, y, z)`` in cm.
        scale: ``(x, y, z)`` multiplier (1 = 100 cm for cube).
        rotation: ``(pitch, yaw, roll)`` in degrees.
        label: Optional editor display name.

    Returns:
        The spawned ``StaticMeshActor``, or ``None``.
    """
    try:
        mesh_path = SHAPE_MAP.get(shape.lower())
        if mesh_path is None:
            unreal.log_error(f"[arbor.layout] spawn_primitive: unknown shape '{shape}'. "
                             f"Valid: {list(SHAPE_MAP.keys())}")
            return None
        actor = _spawn_mesh(mesh_path, location, rotation, scale, label)
        if actor:
            unreal.log(f"[arbor.layout] spawn_primitive: '{shape}' at {location}")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.layout] spawn_primitive: {e}")
        return None


def make_wall(start, end, height=300.0, thickness=20.0, label=None):
    """Create a wall (scaled cube) between two XY points.

    The wall stretches from *start* to *end* on the XY plane, with
    its bottom at Z = 0 (or at the Z of *start* if provided).

    Args:
        start: ``(x, y)`` or ``(x, y, z)`` start point.
        end: ``(x, y)`` or ``(x, y, z)`` end point.
        height: Wall height in cm.  Default 300.
        thickness: Wall thickness in cm.  Default 20.
        label: Optional editor display name.

    Returns:
        The wall ``StaticMeshActor``, or ``None``.
    """
    try:
        s = _to_vector(start) if len(start) == 3 else unreal.Vector(start[0], start[1], 0)
        e = _to_vector(end) if len(end) == 3 else unreal.Vector(end[0], end[1], 0)

        dx = e.x - s.x
        dy = e.y - s.y
        length = math.sqrt(dx * dx + dy * dy)

        if length < 0.01:
            unreal.log_error("[arbor.layout] make_wall: start and end are the same point")
            return None

        mid_x = (s.x + e.x) / 2.0
        mid_y = (s.y + e.y) / 2.0
        mid_z = s.z + height / 2.0

        yaw = math.degrees(math.atan2(dy, dx))

        # Cube default is 100 cm per axis
        scale_x = length / 100.0
        scale_y = thickness / 100.0
        scale_z = height / 100.0

        actor = _spawn_mesh(
            SHAPE_MAP["cube"],
            (mid_x, mid_y, mid_z),
            (0, yaw, 0),
            (scale_x, scale_y, scale_z),
            label or "Wall",
        )
        if actor:
            unreal.log(f"[arbor.layout] make_wall: {length:.0f}cm long, {height:.0f}cm tall")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.layout] make_wall: {e}")
        return None


def make_floor(center, width=1000.0, depth=1000.0, thickness=20.0, label=None):
    """Create a floor slab (scaled cube) at *center*.

    The top surface of the slab sits at ``center.z``.

    Args:
        center: ``(x, y, z)`` position.
        width: Size along X in cm.
        depth: Size along Y in cm.
        thickness: Slab thickness in cm.  Default 20.
        label: Optional editor display name.

    Returns:
        The floor ``StaticMeshActor``, or ``None``.
    """
    try:
        c = _to_vector(center)
        loc = (c.x, c.y, c.z - thickness / 2.0)
        scale = (width / 100.0, depth / 100.0, thickness / 100.0)
        actor = _spawn_mesh(SHAPE_MAP["cube"], loc, (0, 0, 0), scale, label or "Floor")
        if actor:
            unreal.log(f"[arbor.layout] make_floor: {width:.0f}×{depth:.0f}cm")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.layout] make_floor: {e}")
        return None


def make_room(center, width=500.0, depth=500.0, height=300.0,
              wall_thickness=20.0, label="Room"):
    """Create a rectangular room: 4 walls + floor.

    Args:
        center: ``(x, y, z)`` centre of the room floor.
        width: Interior width (X axis) in cm.
        depth: Interior depth (Y axis) in cm.
        height: Wall height in cm.
        wall_thickness: Wall thickness in cm.
        label: Name prefix for the actors.

    Returns:
        List of created actors ``[floor, wall_N, wall_S, wall_E, wall_W]``.
    """
    try:
        c = _to_vector(center)
        hw = width / 2.0
        hd = depth / 2.0

        actors = []

        # Floor
        floor = make_floor(center, width + wall_thickness * 2, depth + wall_thickness * 2,
                           wall_thickness, f"{label}_Floor")
        if floor:
            actors.append(floor)

        # Wall corner coordinates (outer edges)
        corners = {
            "N": ((c.x - hw, c.y + hd, c.z), (c.x + hw, c.y + hd, c.z)),
            "S": ((c.x - hw, c.y - hd, c.z), (c.x + hw, c.y - hd, c.z)),
            "E": ((c.x + hw, c.y - hd, c.z), (c.x + hw, c.y + hd, c.z)),
            "W": ((c.x - hw, c.y - hd, c.z), (c.x - hw, c.y + hd, c.z)),
        }

        for side, (s, e) in corners.items():
            wall = make_wall(s, e, height, wall_thickness, f"{label}_Wall_{side}")
            if wall:
                actors.append(wall)

        unreal.log(f"[arbor.layout] make_room: created {len(actors)} actors for '{label}'")
        return actors
    except Exception as e:
        unreal.log_error(f"[arbor.layout] make_room: {e}")
        return []


def make_ramp(start, end, width=200.0, label=None):
    """Create a ramp between two 3D points.

    The ramp is a pitched cube connecting *start* (lower) to *end* (upper).

    Args:
        start: ``(x, y, z)`` lower end.
        end: ``(x, y, z)`` upper end.
        width: Ramp width in cm.  Default 200.
        label: Optional editor display name.

    Returns:
        The ramp ``StaticMeshActor``, or ``None``.
    """
    try:
        s = _to_vector(start)
        e = _to_vector(end)

        dx = e.x - s.x
        dy = e.y - s.y
        dz = e.z - s.z
        horiz = math.sqrt(dx * dx + dy * dy)
        length = math.sqrt(horiz * horiz + dz * dz)

        if length < 0.01:
            unreal.log_error("[arbor.layout] make_ramp: start and end are the same point")
            return None

        mid = ((s.x + e.x) / 2.0, (s.y + e.y) / 2.0, (s.z + e.z) / 2.0)
        yaw = math.degrees(math.atan2(dy, dx))
        pitch = math.degrees(math.atan2(dz, horiz))

        thickness = 20.0
        scale = (length / 100.0, width / 100.0, thickness / 100.0)

        actor = _spawn_mesh(SHAPE_MAP["cube"], mid, (pitch, yaw, 0), scale, label or "Ramp")
        if actor:
            unreal.log(f"[arbor.layout] make_ramp: {length:.0f}cm, pitch {pitch:.1f}°")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.layout] make_ramp: {e}")
        return None


def make_stairs(start, direction=(1, 0, 0), step_count=10, step_height=20.0,
                step_depth=30.0, width=200.0, label="Stairs"):
    """Create a staircase from individually spawned cube steps.

    Args:
        start: ``(x, y, z)`` position of the first step's base.
        direction: ``(x, y, z)`` direction vector (will be normalised to XY).
        step_count: Number of steps.
        step_height: Height of each step in cm.
        step_depth: Depth (tread) of each step in cm.
        width: Stair width in cm.
        label: Name prefix for step actors.

    Returns:
        List of step ``StaticMeshActor`` objects.
    """
    try:
        s = _to_vector(start)
        d = _to_vector(direction)

        # Normalise direction to XY plane
        mag = math.sqrt(d.x * d.x + d.y * d.y)
        if mag < 0.001:
            unreal.log_error("[arbor.layout] make_stairs: direction has no XY component")
            return []
        dx = d.x / mag
        dy = d.y / mag
        yaw = math.degrees(math.atan2(dy, dx))

        actors = []
        for i in range(step_count):
            x = s.x + dx * step_depth * i
            y = s.y + dy * step_depth * i
            z = s.z + step_height * (i + 0.5)  # centre of step

            scale = (step_depth / 100.0, width / 100.0, step_height / 100.0)
            step = _spawn_mesh(
                SHAPE_MAP["cube"],
                (x, y, z),
                (0, yaw, 0),
                scale,
                f"{label}_{i}",
            )
            if step:
                actors.append(step)

        unreal.log(f"[arbor.layout] make_stairs: created {len(actors)} steps")
        return actors
    except Exception as e:
        unreal.log_error(f"[arbor.layout] make_stairs: {e}")
        return []
