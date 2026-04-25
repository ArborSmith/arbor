"""Arbor structure — procedural building generation from 2D floor plans.

Lets Claude think in 2D: define rooms by position and size on a flat grid,
and Arbor extrudes everything into 3D geometry using scaled
``/Engine/BasicShapes/Cube`` meshes.

Core entry point: :func:`build_from_plan` takes a plan dict and spawns all
geometry.  Convenience builders (:func:`make_house`, :func:`make_tower`,
:func:`make_castle`) generate plans automatically.
"""

import math

import unreal

from arbor.utils import _to_vector, _to_rotator, _get_all_level_actors, delete_actor
from arbor.layout import _spawn_mesh, SHAPE_MAP, make_wall, make_floor


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

_CUBE = SHAPE_MAP["cube"]
_EPSILON = 1.0  # cm tolerance for edge matching


# ---------------------------------------------------------------------------
# Internal data
# ---------------------------------------------------------------------------

class _Room:
    """Resolved room with absolute world coordinates."""

    __slots__ = ("name", "min_x", "min_y", "max_x", "max_y",
                 "width", "depth", "height", "has_floor", "has_ceiling")

    def __init__(self, d, origin_x, origin_y, default_height):
        self.name = d.get("name", "Room")
        x = d.get("x", 0.0)
        y = d.get("y", 0.0)
        self.width = d.get("width", 500.0)
        self.depth = d.get("depth", 500.0)
        self.height = d.get("height_override") or default_height
        self.has_floor = d.get("has_floor", True)
        self.has_ceiling = d.get("has_ceiling", False)
        self.min_x = origin_x + x
        self.min_y = origin_y + y
        self.max_x = self.min_x + self.width
        self.max_y = self.min_y + self.depth


class _SharedEdge:
    """A wall segment shared between two adjacent rooms."""

    __slots__ = ("room_a", "room_b", "side_a", "side_b",
                 "start", "end", "axis")

    def __init__(self, room_a, room_b, side_a, side_b, start, end, axis):
        self.room_a = room_a
        self.room_b = room_b
        self.side_a = side_a   # e.g. "north"
        self.side_b = side_b   # e.g. "south"
        self.start = start     # (x, y) start of overlap
        self.end = end         # (x, y) end of overlap
        self.axis = axis       # "x" or "y" — which axis the edge runs along


class _Opening:
    """A door or window opening positioned along a wall."""

    __slots__ = ("offset", "width", "height", "sill_height")

    def __init__(self, offset, width, height, sill_height=0.0):
        self.offset = offset        # distance along wall to center
        self.width = width
        self.height = height
        self.sill_height = sill_height  # 0 for doors


# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------

def _rotate_point(px, py, ox, oy, yaw_deg):
    """Rotate point (px, py) around origin (ox, oy) by *yaw_deg* degrees."""
    rad = math.radians(yaw_deg)
    dx = px - ox
    dy = py - oy
    return (dx * math.cos(rad) - dy * math.sin(rad) + ox,
            dx * math.sin(rad) + dy * math.cos(rad) + oy)


def _polygon_corners(cx, cy, radius, sides):
    """Return vertices of a regular polygon centered at (cx, cy)."""
    pts = []
    for i in range(sides):
        angle = 2.0 * math.pi * i / sides
        pts.append((cx + radius * math.cos(angle),
                     cy + radius * math.sin(angle)))
    return pts


def _spawn_cube(location, scale, rotation=(0, 0, 0), label=None):
    """Spawn a single cube with given world location, scale, and rotation."""
    return _spawn_mesh(_CUBE, location, rotation, scale, label)


# ---------------------------------------------------------------------------
# Adjacency detection
# ---------------------------------------------------------------------------

def _find_shared_edges(rooms):
    """Detect walls shared between adjacent rooms.

    Returns list of :class:`_SharedEdge`.
    """
    edges = []
    for i, a in enumerate(rooms):
        for b in rooms[i + 1:]:
            # A north == B south  (both at same Y, overlap on X)
            if abs(a.max_y - b.min_y) < _EPSILON:
                ox_s = max(a.min_x, b.min_x)
                ox_e = min(a.max_x, b.max_x)
                if ox_e - ox_s > _EPSILON:
                    edges.append(_SharedEdge(
                        a, b, "north", "south",
                        (ox_s, a.max_y), (ox_e, a.max_y), "x"))

            # A south == B north
            if abs(a.min_y - b.max_y) < _EPSILON:
                ox_s = max(a.min_x, b.min_x)
                ox_e = min(a.max_x, b.max_x)
                if ox_e - ox_s > _EPSILON:
                    edges.append(_SharedEdge(
                        a, b, "south", "north",
                        (ox_s, a.min_y), (ox_e, a.min_y), "x"))

            # A east == B west  (both at same X, overlap on Y)
            if abs(a.max_x - b.min_x) < _EPSILON:
                oy_s = max(a.min_y, b.min_y)
                oy_e = min(a.max_y, b.max_y)
                if oy_e - oy_s > _EPSILON:
                    edges.append(_SharedEdge(
                        a, b, "east", "west",
                        (a.max_x, oy_s), (a.max_x, oy_e), "y"))

            # A west == B east
            if abs(a.min_x - b.max_x) < _EPSILON:
                oy_s = max(a.min_y, b.min_y)
                oy_e = min(a.max_y, b.max_y)
                if oy_e - oy_s > _EPSILON:
                    edges.append(_SharedEdge(
                        a, b, "west", "east",
                        (a.min_x, oy_s), (a.min_x, oy_e), "y"))
    return edges


# ---------------------------------------------------------------------------
# Wall splitting
# ---------------------------------------------------------------------------

def _wall_direction(start, end):
    """Return (dx, dy, length) unit direction from start to end."""
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    length = math.sqrt(dx * dx + dy * dy)
    if length < 0.01:
        return (0, 0, 0)
    return (dx / length, dy / length, length)


def _spawn_wall_piece(start_xy, dir_xy, from_pos, to_pos, base_z,
                      bottom_z_offset, piece_height, thickness, yaw, label):
    """Spawn one rectangular wall piece along a wall line.

    *from_pos* / *to_pos* are distances along the wall from its start.
    *bottom_z_offset* is the Z offset from base_z for the bottom of this piece.
    """
    piece_len = to_pos - from_pos
    if piece_len < 0.5 or piece_height < 0.5:
        return None

    mid_along = (from_pos + to_pos) / 2.0
    cx = start_xy[0] + dir_xy[0] * mid_along
    cy = start_xy[1] + dir_xy[1] * mid_along
    cz = base_z + bottom_z_offset + piece_height / 2.0

    scale = (piece_len / 100.0, thickness / 100.0, piece_height / 100.0)
    return _spawn_cube((cx, cy, cz), scale, (0, yaw, 0), label)


def _split_wall_for_openings(wall_start, wall_end, openings, wall_height,
                             thickness, base_z, label_prefix):
    """Split a wall into segments around door/window openings.

    Returns list of spawned actors.
    """
    dx, dy, wall_len = _wall_direction(wall_start, wall_end)
    if wall_len < 0.5:
        return []

    yaw = math.degrees(math.atan2(dy, dx))
    actors = []
    seg_idx = 0

    # Sort openings by position along wall
    sorted_openings = sorted(openings, key=lambda o: o.offset)

    cursor = 0.0  # current position along wall

    for opening in sorted_openings:
        left_edge = opening.offset - opening.width / 2.0
        right_edge = opening.offset + opening.width / 2.0

        # Clamp to wall bounds
        left_edge = max(0.0, left_edge)
        right_edge = min(wall_len, right_edge)

        # Full-height segment before this opening
        if left_edge - cursor > 0.5:
            a = _spawn_wall_piece(
                wall_start, (dx, dy), cursor, left_edge, base_z,
                0, wall_height, thickness, yaw,
                f"{label_prefix}_{seg_idx}")
            if a:
                actors.append(a)
            seg_idx += 1

        # Pieces around the opening
        if opening.sill_height > 0.5:
            # Window: below-sill piece
            a = _spawn_wall_piece(
                wall_start, (dx, dy), left_edge, right_edge, base_z,
                0, opening.sill_height, thickness, yaw,
                f"{label_prefix}_{seg_idx}")
            if a:
                actors.append(a)
            seg_idx += 1

        above_bottom = opening.sill_height + opening.height
        above_height = wall_height - above_bottom
        if above_height > 0.5:
            # Above-opening piece (header)
            a = _spawn_wall_piece(
                wall_start, (dx, dy), left_edge, right_edge, base_z,
                above_bottom, above_height, thickness, yaw,
                f"{label_prefix}_{seg_idx}")
            if a:
                actors.append(a)
            seg_idx += 1

        cursor = right_edge

    # Final full-height segment after last opening
    if wall_len - cursor > 0.5:
        a = _spawn_wall_piece(
            wall_start, (dx, dy), cursor, wall_len, base_z,
            0, wall_height, thickness, yaw,
            f"{label_prefix}_{seg_idx}")
        if a:
            actors.append(a)

    return actors


# ---------------------------------------------------------------------------
# Roof helpers
# ---------------------------------------------------------------------------

def _make_flat_roof(center_x, center_y, width, depth, overhang, base_z,
                    wall_height, thickness, label):
    """Spawn a flat roof slab above the walls."""
    rw = width + 2.0 * overhang
    rd = depth + 2.0 * overhang
    z = base_z + wall_height + thickness / 2.0
    scale = (rw / 100.0, rd / 100.0, thickness / 100.0)
    return _spawn_cube((center_x, center_y, z), scale, (0, 0, 0), label)


def _make_gable_roof(center_x, center_y, width, depth, overhang, pitch,
                     base_z, wall_height, label):
    """Spawn a gable roof — two tilted slabs meeting at a ridge.

    Ridge runs along X (width axis).  Slopes descend along Y.
    Rotation uses Roll (around X axis) since slopes go along Y.
    """
    actors = []
    roof_thickness = 10.0
    half_d = depth / 2.0 + overhang
    ridge_rise = math.tan(math.radians(pitch)) * half_d
    slope_len = half_d / math.cos(math.radians(pitch))
    ridge_z = base_z + wall_height + ridge_rise
    eave_z = base_z + wall_height
    rw = width + 2.0 * overhang

    # Each side: midpoint between eave and ridge
    mid_z = (eave_z + ridge_z) / 2.0

    # South slope (negative Y side) — roll tilts south edge down
    mid_y_s = center_y - half_d / 2.0
    a = _spawn_cube(
        (center_x, mid_y_s, mid_z),
        (rw / 100.0, slope_len / 100.0, roof_thickness / 100.0),
        (0, 0, pitch),
        f"{label}_L")
    if a:
        actors.append(a)

    # North slope (positive Y side) — roll tilts north edge down
    mid_y_n = center_y + half_d / 2.0
    a = _spawn_cube(
        (center_x, mid_y_n, mid_z),
        (rw / 100.0, slope_len / 100.0, roof_thickness / 100.0),
        (0, 0, -pitch),
        f"{label}_R")
    if a:
        actors.append(a)

    return actors


# ---------------------------------------------------------------------------
# build_from_plan  (core)
# ---------------------------------------------------------------------------

def build_from_plan(plan, location=(0, 0, 0), rotation=(0, 0, 0)):
    """Build 3D geometry from a 2D floor plan dict.

    Args:
        plan: Floor plan dict — see module docstring / CLAUDE.md for schema.
        location: ``(x, y, z)`` world origin for the structure.
        rotation: ``(pitch, yaw, roll)`` — only yaw is used (building rotation).

    Returns:
        List of all spawned ``unreal.Actor`` objects.
    """
    try:
        loc = _to_vector(location)
        rot = _to_rotator(rotation)
        yaw = rot.yaw

        name = plan.get("name", "Structure")
        wall_height = plan.get("wall_height", 300.0)
        wall_thick = plan.get("wall_thickness", 20.0)
        floor_thick = plan.get("floor_thickness", 10.0)
        base_z = loc.z

        # --- resolve rooms ---------------------------------------------------
        rooms = [_Room(rd, loc.x, loc.y, wall_height)
                 for rd in plan.get("rooms", [])]

        if not rooms:
            unreal.log_warning("[arbor.structure] build_from_plan: no rooms in plan")
            return []

        # Apply yaw rotation to room corners if needed
        if abs(yaw) > 0.01:
            for r in rooms:
                c1 = _rotate_point(r.min_x, r.min_y, loc.x, loc.y, yaw)
                c2 = _rotate_point(r.max_x, r.max_y, loc.x, loc.y, yaw)
                r.min_x = min(c1[0], c2[0])
                r.min_y = min(c1[1], c2[1])
                r.max_x = max(c1[0], c2[0])
                r.max_y = max(c1[1], c2[1])

        # --- shared edges ----------------------------------------------------
        shared = _find_shared_edges(rooms)

        # Build lookup: (room_name, side) -> list of SharedEdge
        shared_lookup = {}
        for se in shared:
            shared_lookup.setdefault((se.room_a.name, se.side_a), []).append(se)
            shared_lookup.setdefault((se.room_b.name, se.side_b), []).append(se)

        # --- map doors to openings on shared edges ---------------------------
        # door_openings: keyed by (room_a_name, room_b_name) -> _Opening
        door_map = {}  # (room_name, side) -> list of _Opening
        for door in plan.get("doors", []):
            between = door.get("between", [])
            if len(between) != 2:
                continue
            d_width = door.get("width", 120.0)
            d_height = door.get("height", 220.0)
            d_offset_frac = door.get("offset", 0.5)

            # Find shared edge between these two rooms
            for se in shared:
                names = {se.room_a.name, se.room_b.name}
                if set(between) == names:
                    edge_len = math.sqrt(
                        (se.end[0] - se.start[0]) ** 2 +
                        (se.end[1] - se.start[1]) ** 2)
                    offset_along = edge_len * d_offset_frac
                    opening = _Opening(offset_along, d_width, d_height, 0.0)
                    door_map.setdefault((se.room_a.name, se.side_a), []).append(opening)
                    # Don't add to room_b — the shared wall is built once
                    break

        # --- map windows to walls --------------------------------------------
        window_map = {}  # (room_name, side) -> list of _Opening
        for win in plan.get("windows", []):
            room_name = win.get("room")
            side = win.get("wall", "north").lower()
            w_width = win.get("width", 200.0)
            w_height = win.get("height", 150.0)
            w_sill = win.get("sill_height", 100.0)
            w_count = win.get("count", 1)
            spacing = win.get("spacing", "even")

            # Find the room
            room = None
            for r in rooms:
                if r.name == room_name:
                    room = r
                    break
            if room is None:
                unreal.log_warning(
                    f"[arbor.structure] window references unknown room '{room_name}'")
                continue

            # Get wall length
            if side in ("north", "south"):
                wall_len = room.width
            else:
                wall_len = room.depth

            # Compute offsets for each window
            if spacing == "even" and w_count > 0:
                segment = wall_len / (w_count + 1)
                for i in range(w_count):
                    offset = segment * (i + 1)
                    opening = _Opening(offset, w_width, w_height, w_sill)
                    window_map.setdefault((room_name, side), []).append(opening)

        # --- spawn geometry ---------------------------------------------------
        all_actors = []

        for room in rooms:
            rname = room.name
            h = room.height
            cx = (room.min_x + room.max_x) / 2.0
            cy = (room.min_y + room.max_y) / 2.0

            # Floor
            if room.has_floor:
                a = make_floor((cx, cy, base_z), room.width, room.depth,
                               floor_thick, f"{name}_Floor_{rname}")
                if a:
                    all_actors.append(a)

            # Ceiling
            if room.has_ceiling:
                ceil_z = base_z + h
                a = make_floor((cx, cy, ceil_z), room.width, room.depth,
                               floor_thick, f"{name}_Ceiling_{rname}")
                if a:
                    all_actors.append(a)

            # Walls — define each side's start/end in the room's local frame
            walls_def = {
                "north": ((room.min_x, room.max_y), (room.max_x, room.max_y)),
                "south": ((room.min_x, room.min_y), (room.max_x, room.min_y)),
                "east":  ((room.max_x, room.min_y), (room.max_x, room.max_y)),
                "west":  ((room.min_x, room.min_y), (room.min_x, room.max_y)),
            }

            for side, (ws, we) in walls_def.items():
                key = (rname, side)
                is_shared = key in shared_lookup
                openings = []
                openings.extend(door_map.get(key, []))
                openings.extend(window_map.get(key, []))

                label_pfx = f"{name}_Wall_{rname}_{side[0].upper()}"

                if is_shared and not openings:
                    # Fully shared wall with no openings — skip
                    continue

                if openings:
                    actors = _split_wall_for_openings(
                        ws, we, openings, h, wall_thick, base_z, label_pfx)
                    all_actors.extend(actors)
                else:
                    # Solid wall
                    a = make_wall(
                        (ws[0], ws[1], base_z), (we[0], we[1], base_z),
                        h, wall_thick, label_pfx)
                    if a:
                        all_actors.append(a)

        # --- roof -------------------------------------------------------------
        roof = plan.get("roof")
        if roof:
            # Compute bounding box of all rooms for the roof
            all_min_x = min(r.min_x for r in rooms)
            all_max_x = max(r.max_x for r in rooms)
            all_min_y = min(r.min_y for r in rooms)
            all_max_y = max(r.max_y for r in rooms)
            total_w = all_max_x - all_min_x
            total_d = all_max_y - all_min_y
            roof_cx = (all_min_x + all_max_x) / 2.0
            roof_cy = (all_min_y + all_max_y) / 2.0
            overhang = roof.get("overhang", 50.0)
            roof_type = roof.get("type", "flat").lower()

            if roof_type == "gable":
                pitch = roof.get("pitch", 30.0)
                ra = _make_gable_roof(
                    roof_cx, roof_cy, total_w, total_d, overhang, pitch,
                    base_z, wall_height, f"{name}_Roof")
                all_actors.extend(ra)
            else:
                a = _make_flat_roof(
                    roof_cx, roof_cy, total_w, total_d, overhang, base_z,
                    wall_height, floor_thick, f"{name}_Roof")
                if a:
                    all_actors.append(a)

        unreal.log(f"[arbor.structure] build_from_plan: '{name}' — "
                   f"{len(all_actors)} actors, {len(rooms)} rooms")
        return all_actors

    except Exception as e:
        unreal.log_error(f"[arbor.structure] build_from_plan: {e}")
        return []


# ---------------------------------------------------------------------------
# Convenience builders
# ---------------------------------------------------------------------------

def make_house(width=800.0, depth=600.0, rooms=3, wall_height=300.0,
               roof="gable", location=(0, 0, 0)):
    """Auto-generate a simple house layout.

    Args:
        width: Total exterior width (X) in cm.
        depth: Total exterior depth (Y) in cm.
        rooms: Number of rooms (1-4) to auto-split, or a list of room dicts.
        wall_height: Wall height in cm.
        roof: ``"gable"``, ``"flat"``, or a roof dict.
        location: ``(x, y, z)`` world position.

    Returns:
        List of spawned actors.
    """
    try:
        if isinstance(rooms, (list, tuple)):
            room_list = list(rooms)
        else:
            n = max(1, min(4, int(rooms)))
            wt = 20.0  # wall thickness accounted in plan

            if n == 1:
                room_list = [
                    {"name": "Main", "x": 0, "y": 0,
                     "width": width, "depth": depth,
                     "has_floor": True, "has_ceiling": False},
                ]
            elif n == 2:
                hw = width / 2.0
                room_list = [
                    {"name": "Room1", "x": 0, "y": 0,
                     "width": hw, "depth": depth,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Room2", "x": hw, "y": 0,
                     "width": hw, "depth": depth,
                     "has_floor": True, "has_ceiling": False},
                ]
            elif n == 3:
                # L-shape: left half is one room, right half split top/bottom
                hw = width / 2.0
                hd = depth / 2.0
                room_list = [
                    {"name": "LivingRoom", "x": 0, "y": 0,
                     "width": hw, "depth": depth,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Bedroom", "x": hw, "y": 0,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Kitchen", "x": hw, "y": hd,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                ]
            else:  # 4
                hw = width / 2.0
                hd = depth / 2.0
                room_list = [
                    {"name": "Room1", "x": 0, "y": 0,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Room2", "x": hw, "y": 0,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Room3", "x": 0, "y": hd,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                    {"name": "Room4", "x": hw, "y": hd,
                     "width": hw, "depth": hd,
                     "has_floor": True, "has_ceiling": False},
                ]

        # Auto-add doors between adjacent rooms
        doors = []
        names = [r["name"] for r in room_list]
        if len(room_list) >= 2:
            # Quick adjacency: check every pair
            for i, a in enumerate(room_list):
                for b in room_list[i + 1:]:
                    ax2 = a["x"] + a["width"]
                    bx2 = b["x"] + b["width"]
                    ay2 = a["y"] + a["depth"]
                    by2 = b["y"] + b["depth"]
                    # Shared X edge?
                    if abs(ax2 - b["x"]) < _EPSILON or abs(bx2 - a["x"]) < _EPSILON:
                        oy_s = max(a["y"], b["y"])
                        oy_e = min(ay2, by2)
                        if oy_e - oy_s > 120:
                            doors.append({"between": [a["name"], b["name"]],
                                          "width": 120, "height": 220, "offset": 0.5})
                    # Shared Y edge?
                    if abs(ay2 - b["y"]) < _EPSILON or abs(by2 - a["y"]) < _EPSILON:
                        ox_s = max(a["x"], b["x"])
                        ox_e = min(ax2, bx2)
                        if ox_e - ox_s > 120:
                            doors.append({"between": [a["name"], b["name"]],
                                          "width": 120, "height": 220, "offset": 0.5})

        # Build roof dict
        if isinstance(roof, dict):
            roof_dict = roof
        elif roof == "flat":
            roof_dict = {"type": "flat", "overhang": 50}
        else:
            roof_dict = {"type": "gable", "overhang": 50, "pitch": 30}

        plan = {
            "name": "House",
            "wall_height": wall_height,
            "wall_thickness": 20,
            "floor_thickness": 10,
            "rooms": room_list,
            "doors": doors,
            "roof": roof_dict,
            "floors": 1,
        }
        return build_from_plan(plan, location)

    except Exception as e:
        unreal.log_error(f"[arbor.structure] make_house: {e}")
        return []


def make_tower(radius=300.0, floors=1, wall_height=300.0, sides=8,
               location=(0, 0, 0)):
    """Build an N-sided polygon tower.

    Args:
        radius: Outer radius in cm.
        floors: Number of floors (V1: only 1 floor supported).
        wall_height: Wall height per floor in cm.
        sides: Number of wall segments (8 = octagon).
        location: ``(x, y, z)`` center position.

    Returns:
        List of spawned actors.
    """
    try:
        loc = _to_vector(location)
        if floors > 1:
            unreal.log_warning(
                "[arbor.structure] make_tower: multi-floor not yet supported, "
                "building 1 floor")

        thickness = 20.0
        name = "Tower"
        actors = []
        pts = _polygon_corners(loc.x, loc.y, radius, sides)

        # Walls between each pair of adjacent vertices
        for i in range(sides):
            s = pts[i]
            e = pts[(i + 1) % sides]
            a = make_wall(
                (s[0], s[1], loc.z), (e[0], e[1], loc.z),
                wall_height, thickness, f"{name}_Wall_{i}")
            if a:
                actors.append(a)

        # Floor — inscribed square approximation
        inner_r = radius * math.cos(math.pi / sides)
        side_len = inner_r * math.sqrt(2) * 2
        a = make_floor((loc.x, loc.y, loc.z), side_len, side_len,
                       10.0, f"{name}_Floor")
        if a:
            actors.append(a)

        unreal.log(f"[arbor.structure] make_tower: {sides}-sided, "
                   f"{len(actors)} actors")
        return actors

    except Exception as e:
        unreal.log_error(f"[arbor.structure] make_tower: {e}")
        return []


def make_wall_segment(start, end, height=300.0, thickness=20.0,
                      battlements=False, label=None):
    """Wall between two points, optionally with battlements (crenellations).

    Args:
        start: ``(x, y)`` or ``(x, y, z)`` start point.
        end: ``(x, y)`` or ``(x, y, z)`` end point.
        height: Wall height in cm.
        thickness: Wall thickness in cm.
        battlements: ``True`` for default crenellations, or a dict
                     ``{"width": 60, "height": 40, "gap": 60}``.
        label: Actor label prefix.

    Returns:
        List of spawned actors (wall + merlon cubes).
    """
    try:
        lbl = label or "WallSeg"
        if not battlements:
            a = make_wall(start, end, height, thickness, lbl)
            return [a] if a else []

        # Parse battlement params
        if isinstance(battlements, dict):
            m_width = battlements.get("width", 60.0)
            m_height = battlements.get("height", 40.0)
            m_gap = battlements.get("gap", 60.0)
        else:
            m_width = 60.0
            m_height = 40.0
            m_gap = 60.0

        actors = []

        # Main wall (reduced height)
        main_h = height - m_height
        a = make_wall(start, end, main_h, thickness, lbl)
        if a:
            actors.append(a)

        # Merlons along the top
        s = _to_vector(start) if len(start) >= 3 else unreal.Vector(start[0], start[1], 0)
        e = _to_vector(end) if len(end) >= 3 else unreal.Vector(end[0], end[1], 0)
        dx = e.x - s.x
        dy = e.y - s.y
        wall_len = math.sqrt(dx * dx + dy * dy)
        yaw_deg = math.degrees(math.atan2(dy, dx))

        if wall_len < m_width:
            return actors

        ux = dx / wall_len
        uy = dy / wall_len
        base_z = s.z
        top_z = base_z + main_h + m_height / 2.0

        pos = m_gap / 2.0  # start with half-gap offset
        idx = 0
        while pos + m_width <= wall_len:
            mx = s.x + ux * (pos + m_width / 2.0)
            my = s.y + uy * (pos + m_width / 2.0)
            scale = (m_width / 100.0, thickness / 100.0, m_height / 100.0)
            a = _spawn_cube((mx, my, top_z), scale, (0, yaw_deg, 0),
                            f"{lbl}_Battlement_{idx}")
            if a:
                actors.append(a)
            pos += m_width + m_gap
            idx += 1

        unreal.log(f"[arbor.structure] make_wall_segment: {len(actors)} actors "
                   f"({idx} merlons)")
        return actors

    except Exception as e:
        unreal.log_error(f"[arbor.structure] make_wall_segment: {e}")
        return []


def make_archway(location=(0, 0, 0), width=200.0, height=280.0,
                 depth=40.0, label=None):
    """Spawn a simple archway — two pillars and a top beam.

    Args:
        location: ``(x, y, z)`` center of the archway at ground level.
        width: Opening width in cm.
        height: Total height in cm.
        depth: Pillar/beam depth in cm.
        label: Actor label prefix.

    Returns:
        List of 3 actors ``[left_pillar, right_pillar, beam]``.
    """
    try:
        loc = _to_vector(location)
        lbl = label or "Archway"
        beam_h = max(40.0, height * 0.15)
        pillar_h = height - beam_h
        actors = []

        # Left pillar
        lx = loc.x - width / 2.0
        a = _spawn_cube(
            (lx, loc.y, loc.z + pillar_h / 2.0),
            (depth / 100.0, depth / 100.0, pillar_h / 100.0),
            (0, 0, 0), f"{lbl}_L")
        if a:
            actors.append(a)

        # Right pillar
        rx = loc.x + width / 2.0
        a = _spawn_cube(
            (rx, loc.y, loc.z + pillar_h / 2.0),
            (depth / 100.0, depth / 100.0, pillar_h / 100.0),
            (0, 0, 0), f"{lbl}_R")
        if a:
            actors.append(a)

        # Top beam
        beam_w = width + depth  # span across both pillars
        a = _spawn_cube(
            (loc.x, loc.y, loc.z + pillar_h + beam_h / 2.0),
            (beam_w / 100.0, depth / 100.0, beam_h / 100.0),
            (0, 0, 0), f"{lbl}_Top")
        if a:
            actors.append(a)

        unreal.log(f"[arbor.structure] make_archway: {width:.0f}×{height:.0f}cm")
        return actors

    except Exception as e:
        unreal.log_error(f"[arbor.structure] make_archway: {e}")
        return []


def make_castle(size="small", location=(0, 0, 0)):
    """Build a preset castle layout.

    Args:
        size: ``"small"``, ``"medium"``, or ``"large"``.
        location: ``(x, y, z)`` center position.

    Returns:
        List of all spawned actors.
    """
    try:
        loc = _to_vector(location)
        ox, oy, oz = loc.x, loc.y, loc.z
        actors = []

        if size == "small":
            # 4 corner towers + connecting walls with battlements + floor
            span = 1500.0  # half-distance from center to tower center
            tower_r = 200.0
            wh = 400.0
            corners = [
                (ox - span, oy - span),
                (ox + span, oy - span),
                (ox + span, oy + span),
                (ox - span, oy + span),
            ]

            # Towers
            for i, (tx, ty) in enumerate(corners):
                ta = make_tower(radius=tower_r, wall_height=wh, sides=8,
                                location=(tx, ty, oz))
                for a in ta:
                    a.set_actor_label(f"Castle_Tower_{i}_{a.get_actor_label()}")
                actors.extend(ta)

            # Connecting walls with battlements
            wall_pairs = [(0, 1), (1, 2), (2, 3), (3, 0)]
            for idx, (i, j) in enumerate(wall_pairs):
                wa = make_wall_segment(
                    (corners[i][0], corners[i][1], oz),
                    (corners[j][0], corners[j][1], oz),
                    height=wh, thickness=30, battlements=True,
                    label=f"Castle_Wall_{idx}")
                actors.extend(wa)

            # Courtyard floor
            yard_size = span * 2
            a = make_floor((ox, oy, oz), yard_size, yard_size, 10,
                           "Castle_Courtyard")
            if a:
                actors.append(a)

            # Gate archway on south wall
            ga = make_archway(
                (ox, oy - span, oz), width=250, height=350, depth=40,
                label="Castle_Gate")
            actors.extend(ga)

        elif size == "medium":
            # Outer wall + 4 towers + gatehouse + inner keep
            span = 2000.0
            tower_r = 250.0
            wh = 500.0
            corners = [
                (ox - span, oy - span),
                (ox + span, oy - span),
                (ox + span, oy + span),
                (ox - span, oy + span),
            ]

            for i, (tx, ty) in enumerate(corners):
                ta = make_tower(radius=tower_r, wall_height=wh, sides=8,
                                location=(tx, ty, oz))
                for a in ta:
                    a.set_actor_label(f"Castle_Tower_{i}_{a.get_actor_label()}")
                actors.extend(ta)

            wall_pairs = [(0, 1), (1, 2), (2, 3), (3, 0)]
            for idx, (i, j) in enumerate(wall_pairs):
                wa = make_wall_segment(
                    (corners[i][0], corners[i][1], oz),
                    (corners[j][0], corners[j][1], oz),
                    height=wh, thickness=40, battlements=True,
                    label=f"Castle_Wall_{idx}")
                actors.extend(wa)

            # Courtyard
            a = make_floor((ox, oy, oz), span * 2, span * 2, 10,
                           "Castle_Courtyard")
            if a:
                actors.append(a)

            # Gatehouse
            ga = make_archway(
                (ox, oy - span, oz), width=300, height=420, depth=50,
                label="Castle_Gate")
            actors.extend(ga)

            # Inner keep (room)
            keep = build_from_plan({
                "name": "Castle_Keep",
                "wall_height": 450,
                "wall_thickness": 40,
                "floor_thickness": 15,
                "rooms": [
                    {"name": "Hall", "x": -400, "y": -300,
                     "width": 800, "depth": 600,
                     "has_floor": True, "has_ceiling": False}
                ],
                "roof": {"type": "gable", "overhang": 60, "pitch": 35},
            }, location=(ox, oy + 400, oz))
            actors.extend(keep)

        elif size == "large":
            # Outer wall, inner wall, towers, great hall, gatehouse
            outer = 3000.0
            inner = 1500.0
            wh_outer = 500.0
            wh_inner = 600.0
            tower_r_out = 300.0
            tower_r_in = 200.0

            # Outer wall corners + towers
            out_corners = [
                (ox - outer, oy - outer),
                (ox + outer, oy - outer),
                (ox + outer, oy + outer),
                (ox - outer, oy + outer),
            ]
            for i, (tx, ty) in enumerate(out_corners):
                ta = make_tower(radius=tower_r_out, wall_height=wh_outer, sides=8,
                                location=(tx, ty, oz))
                for a in ta:
                    a.set_actor_label(f"Castle_OuterTower_{i}_{a.get_actor_label()}")
                actors.extend(ta)

            for idx, (i, j) in enumerate([(0, 1), (1, 2), (2, 3), (3, 0)]):
                wa = make_wall_segment(
                    (out_corners[i][0], out_corners[i][1], oz),
                    (out_corners[j][0], out_corners[j][1], oz),
                    height=wh_outer, thickness=50, battlements=True,
                    label=f"Castle_OuterWall_{idx}")
                actors.extend(wa)

            # Inner wall corners + towers
            in_corners = [
                (ox - inner, oy - inner),
                (ox + inner, oy - inner),
                (ox + inner, oy + inner),
                (ox - inner, oy + inner),
            ]
            for i, (tx, ty) in enumerate(in_corners):
                ta = make_tower(radius=tower_r_in, wall_height=wh_inner, sides=8,
                                location=(tx, ty, oz))
                for a in ta:
                    a.set_actor_label(f"Castle_InnerTower_{i}_{a.get_actor_label()}")
                actors.extend(ta)

            for idx, (i, j) in enumerate([(0, 1), (1, 2), (2, 3), (3, 0)]):
                wa = make_wall_segment(
                    (in_corners[i][0], in_corners[i][1], oz),
                    (in_corners[j][0], in_corners[j][1], oz),
                    height=wh_inner, thickness=40, battlements=True,
                    label=f"Castle_InnerWall_{idx}")
                actors.extend(wa)

            # Courtyard floor
            a = make_floor((ox, oy, oz), outer * 2, outer * 2, 10,
                           "Castle_Courtyard")
            if a:
                actors.append(a)

            # Great hall
            hall = build_from_plan({
                "name": "Castle_GreatHall",
                "wall_height": 550,
                "wall_thickness": 50,
                "floor_thickness": 15,
                "rooms": [
                    {"name": "Hall", "x": -600, "y": -400,
                     "width": 1200, "depth": 800,
                     "has_floor": True, "has_ceiling": False}
                ],
                "roof": {"type": "gable", "overhang": 80, "pitch": 35},
            }, location=(ox, oy + 300, oz))
            actors.extend(hall)

            # Outer gatehouse
            ga = make_archway(
                (ox, oy - outer, oz), width=350, height=450, depth=60,
                label="Castle_OuterGate")
            actors.extend(ga)

            # Inner gatehouse
            ga2 = make_archway(
                (ox, oy - inner, oz), width=300, height=420, depth=50,
                label="Castle_InnerGate")
            actors.extend(ga2)

        else:
            unreal.log_warning(
                f"[arbor.structure] make_castle: unknown size '{size}', "
                f"use 'small', 'medium', or 'large'")
            return []

        unreal.log(f"[arbor.structure] make_castle: '{size}' — "
                   f"{len(actors)} actors")
        return actors

    except Exception as e:
        unreal.log_error(f"[arbor.structure] make_castle: {e}")
        return []


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------

def delete_structure(name):
    """Delete all actors whose label starts with *name*.

    Args:
        name: Structure name prefix (e.g. ``"House"``, ``"Castle"``).

    Returns:
        Dict with ``deleted`` list and ``count``.
    """
    try:
        prefix = name.lower()
        actors = _get_all_level_actors()
        deleted = []
        for actor in actors:
            label = actor.get_actor_label()
            if label.lower().startswith(prefix):
                delete_actor(actor)
                deleted.append(label)
        unreal.log(f"[arbor.structure] delete_structure: removed {len(deleted)} "
                   f"actors with prefix '{name}'")
        return {"deleted": deleted, "count": len(deleted)}
    except Exception as e:
        unreal.log_error(f"[arbor.structure] delete_structure: {e}")
        return {"deleted": [], "count": 0}
