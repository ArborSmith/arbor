"""Arbor actor management — find, inspect, transform, and batch-operate on actors."""

import unreal

from arbor.utils import (
    _to_vector,
    _to_rotator,
    _resolve_actor,
    _get_actor_subsystem,
    _get_all_level_actors,
    find_actor_by_name,
    load_asset,
)


def get_actor_info(name):
    """Return a dict describing the named actor.

    Args:
        name: Actor label string or ``unreal.Actor`` reference.

    Returns:
        Dict with keys ``name``, ``class``, ``position``, ``rotation``,
        ``scale``, ``components``.  ``None`` if not found.
    """
    try:
        actor = _resolve_actor(name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] get_actor_info: '{name}' not found")
            return None
        loc = actor.get_actor_location()
        rot = actor.get_actor_rotation()
        scl = actor.get_actor_scale3d()
        comps = [c.get_name() for c in actor.get_components_by_class(unreal.ActorComponent)]
        return {
            "name": actor.get_actor_label(),
            "class": actor.get_class().get_name(),
            "position": {"X": loc.x, "Y": loc.y, "Z": loc.z},
            "rotation": {"Pitch": rot.pitch, "Yaw": rot.yaw, "Roll": rot.roll},
            "scale": {"X": scl.x, "Y": scl.y, "Z": scl.z},
            "components": comps,
        }
    except Exception as e:
        unreal.log_error(f"[arbor.actors] get_actor_info: {e}")
        return None


def set_actor_transform(name, position=None, rotation=None, scale=None):
    """Set any combination of position, rotation, and scale on an actor.

    Args:
        name: Actor label string or ``unreal.Actor`` reference.
        position: ``(x, y, z)`` or ``unreal.Vector``, or ``None`` to skip.
        rotation: ``(pitch, yaw, roll)`` or ``unreal.Rotator``, or ``None``.
        scale: ``(x, y, z)`` or ``unreal.Vector``, or ``None``.
    """
    try:
        actor = _resolve_actor(name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] set_actor_transform: '{name}' not found")
            return
        if position is not None:
            actor.set_actor_location(_to_vector(position), False, False)
        if rotation is not None:
            actor.set_actor_rotation(_to_rotator(rotation), False)
        if scale is not None:
            actor.set_actor_scale3d(_to_vector(scale))
        unreal.log(f"[arbor.actors] set_actor_transform: updated '{actor.get_actor_label()}'")
    except Exception as e:
        unreal.log_error(f"[arbor.actors] set_actor_transform: {e}")


def rename_actor(old_name, new_name):
    """Rename an actor's editor label.

    Args:
        old_name: Current label string or ``unreal.Actor`` reference.
        new_name: New label string.

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        actor = _resolve_actor(old_name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] rename_actor: '{old_name}' not found")
            return False
        actor.set_actor_label(new_name)
        unreal.log(f"[arbor.actors] rename_actor: '{old_name}' → '{new_name}'")
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.actors] rename_actor: {e}")
        return False


def duplicate_actor(name, new_location=None, new_label=None):
    """Duplicate an actor, optionally placing the copy elsewhere.

    Args:
        name: Actor label string or ``unreal.Actor`` reference.
        new_location: ``(x, y, z)`` for the copy, or ``None`` to keep same.
        new_label: Optional label for the duplicate.

    Returns:
        The duplicated ``unreal.Actor``, or ``None``.
    """
    try:
        actor = _resolve_actor(name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] duplicate_actor: '{name}' not found")
            return None
        sub = _get_actor_subsystem()
        if sub:
            dup = sub.duplicate_actor(actor)
        else:
            # Fallback: manually copy via spawn + mesh copy for StaticMeshActors
            loc = actor.get_actor_location()
            rot = actor.get_actor_rotation()
            scl = actor.get_actor_scale3d()
            dup = unreal.EditorLevelLibrary.spawn_actor_from_class(
                actor.get_class(), loc, rot
            )
            if dup:
                dup.set_actor_scale3d(scl)
        if dup is None:
            unreal.log_error("[arbor.actors] duplicate_actor: duplication failed")
            return None
        if new_location is not None:
            dup.set_actor_location(_to_vector(new_location), False, False)
        if new_label:
            dup.set_actor_label(new_label)
        unreal.log(f"[arbor.actors] duplicate_actor: duplicated '{actor.get_actor_label()}'")
        return dup
    except Exception as e:
        unreal.log_error(f"[arbor.actors] duplicate_actor: {e}")
        return None


def set_actor_mobility(name, mobility):
    """Set an actor's root component mobility.

    Args:
        name: Actor label string or ``unreal.Actor`` reference.
        mobility: One of ``"static"``, ``"stationary"``, ``"movable"``.
    """
    try:
        actor = _resolve_actor(name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] set_actor_mobility: '{name}' not found")
            return
        mobility_map = {
            "static": unreal.ComponentMobility.STATIC,
            "stationary": unreal.ComponentMobility.STATIONARY,
            "movable": unreal.ComponentMobility.MOVABLE,
        }
        mob = mobility_map.get(mobility.lower())
        if mob is None:
            unreal.log_error(f"[arbor.actors] set_actor_mobility: unknown mobility '{mobility}'")
            return
        root = actor.get_editor_property("root_component")
        if root:
            root.set_editor_property("mobility", mob)
            unreal.log(f"[arbor.actors] set_actor_mobility: '{actor.get_actor_label()}' → {mobility}")
    except Exception as e:
        unreal.log_error(f"[arbor.actors] set_actor_mobility: {e}")


def group_actors(names, group_label):
    """Select the named actors in the editor and log a grouping note.

    Args:
        names: List of actor label strings.
        group_label: Descriptive label for the group.

    Note:
        True actor grouping (``GroupActor``) is not well-exposed in UE5 Python.
        This function selects the actors so you can manually group them
        (Ctrl+G in the viewport).
    """
    try:
        actors = []
        for n in names:
            a = find_actor_by_name(n)
            if a:
                actors.append(a)
        if actors:
            select_actors_list(actors)
            unreal.log(f"[arbor.actors] group_actors: selected {len(actors)} actors as '{group_label}' — press Ctrl+G to group")
    except Exception as e:
        unreal.log_error(f"[arbor.actors] group_actors: {e}")


def get_actor_bounds(name):
    """Return the axis-aligned bounding box of an actor.

    Args:
        name: Actor label string or ``unreal.Actor`` reference.

    Returns:
        Dict ``{"min": {"X":…, "Y":…, "Z":…}, "max": {…}}``, or ``None``.
    """
    try:
        actor = _resolve_actor(name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] get_actor_bounds: '{name}' not found")
            return None
        origin, extent = actor.get_actor_bounds(False)
        return {
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
        }
    except Exception as e:
        unreal.log_error(f"[arbor.actors] get_actor_bounds: {e}")
        return None


def select_actors(names):
    """Set the editor viewport selection to the named actors.

    Args:
        names: List of actor label strings.
    """
    try:
        actors = []
        for n in names:
            a = find_actor_by_name(n) if isinstance(n, str) else n
            if a:
                actors.append(a)
        select_actors_list(actors)
    except Exception as e:
        unreal.log_error(f"[arbor.actors] select_actors: {e}")


def select_actors_list(actors):
    """Set the editor viewport selection to a list of actor references.

    Args:
        actors: List of ``unreal.Actor`` objects.
    """
    try:
        sub = _get_actor_subsystem()
        if sub:
            sub.set_selected_level_actors(actors)
        else:
            unreal.EditorLevelLibrary.set_selected_level_actors(actors)
        unreal.log(f"[arbor.actors] select_actors: selected {len(actors)} actors")
    except Exception as e:
        unreal.log_error(f"[arbor.actors] select_actors_list: {e}")


def get_selected_actors():
    """Return info dicts for the currently selected actors in the editor.

    Uses ``EditorActorSubsystem.get_selected_level_actors()`` (not
    ``EditorUtilityLibrary`` which hangs inside ``ue5_run_python``).

    Returns:
        List of dicts with keys ``name``, ``class``, ``position``.
        Empty list if nothing is selected or on error.
    """
    try:
        sub = _get_actor_subsystem()
        if sub:
            selected = list(sub.get_selected_level_actors())
        else:
            selected = list(unreal.EditorLevelLibrary.get_selected_level_actors())

        result = []
        for actor in selected:
            loc = actor.get_actor_location()
            result.append({
                "name": actor.get_actor_label(),
                "class": actor.get_class().get_name(),
                "position": {"X": loc.x, "Y": loc.y, "Z": loc.z},
            })
        unreal.log(f"[arbor.actors] get_selected_actors: {len(result)} actors")
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.actors] get_selected_actors: {e}")
        return []


# ---------------------------------------------------------------------------
# Ground snapping
# ---------------------------------------------------------------------------

def _line_trace_ground_z(actor, ignore_actors=None):
    """Trace straight down from above *actor* and return the ground Z, or None.

    Delegates to C++ ``ArborActorTools.TraceGroundZ`` for version-safe tracing.

    Args:
        actor: The actor to trace from.
        ignore_actors: List of actors to ignore in the trace (prevents
            hitting other props when batch-snapping).
    """
    import json as _json

    loc = actor.get_actor_location()

    # Build ignore list from actor labels.
    ignore_labels = []
    if ignore_actors:
        for a in ignore_actors:
            try:
                ignore_labels.append(a.get_actor_label())
            except Exception:
                pass
    # Always ignore self.
    try:
        self_label = actor.get_actor_label()
        if self_label and self_label not in ignore_labels:
            ignore_labels.append(self_label)
    except Exception:
        pass

    try:
        result_str = unreal.ArborActorTools.trace_ground_z(_json.dumps({
            "x": loc.x, "y": loc.y,
            "start_z": loc.z + 10000.0,
            "trace_distance": 60000.0,
            "ignore_actors": ignore_labels,
        }))
        result = _json.loads(result_str)
        if result.get("hit"):
            return result["z"]
    except Exception:
        pass

    return None


def snap_to_ground(actor_or_name, offset=0.0, preserve_rotation=True, ignore_actors=None):
    """Snap a single actor so its bottom sits on the ground below it.

    Fires a line trace straight down to find the ground, then positions the
    actor so that its bounding-box bottom touches the hit point.

    Args:
        actor_or_name: Actor label string or ``unreal.Actor`` reference.
        offset: Extra Z offset after snapping (positive = higher).
        preserve_rotation: If ``True`` (default), restore the actor's original
            rotation after snapping so only the Z position changes.
        ignore_actors: Optional list of additional actors to ignore in the
            trace (useful to avoid hitting other props in a batch).

    Returns:
        Dict ``{"name", "old_z", "new_z", "ground_z"}`` on success, or
        ``None`` if the actor was not found or no ground was hit.
    """
    try:
        actor = _resolve_actor(actor_or_name)
        if actor is None:
            unreal.log_warning(f"[arbor.actors] snap_to_ground: '{actor_or_name}' not found")
            return None

        label = actor.get_actor_label()
        loc = actor.get_actor_location()
        original_rot = actor.get_actor_rotation()
        old_z = loc.z

        ground_z = _line_trace_ground_z(actor, ignore_actors=ignore_actors)
        if ground_z is None:
            unreal.log_warning(f"[arbor.actors] snap_to_ground: no ground hit for '{label}'")
            return None

        # Account for bounding box: move so the bottom of the mesh touches ground
        origin, extent = actor.get_actor_bounds(False)
        half_height = extent.z
        bottom_offset = (origin.z - half_height) - loc.z  # how far below pivot the bottom is

        new_z = ground_z - bottom_offset + offset
        actor.set_actor_location(unreal.Vector(loc.x, loc.y, new_z), False, False)
        if preserve_rotation:
            actor.set_actor_rotation(original_rot, False)

        unreal.log(f"[arbor.actors] snap_to_ground: '{label}' Z {old_z:.1f} → {new_z:.1f} (ground={ground_z:.1f})")
        return {"name": label, "old_z": round(old_z, 2), "new_z": round(new_z, 2), "ground_z": round(ground_z, 2)}
    except Exception as e:
        unreal.log_error(f"[arbor.actors] snap_to_ground: {e}")
        return None


def snap_all_to_ground(filter_labels=None, offset=0.0):
    """Snap actors in the level to the ground below them.

    All matching actors are ignored in each other's traces so that props
    don't land on top of neighbouring props instead of the real ground.

    Args:
        filter_labels: Optional list of substrings. Only actors whose label
            contains one of these strings will be snapped.  ``None`` snaps all.
        offset: Extra Z offset after snapping.

    Returns:
        Dict ``{"snapped": [...], "failed": [...]}``.
    """
    snapped = []
    failed = []
    try:
        all_actors = _get_all_level_actors()

        # Collect all actors that will be snapped so we can ignore them all
        targets = []
        for actor in all_actors:
            label = actor.get_actor_label()
            if filter_labels is not None:
                if not any(f in label for f in filter_labels):
                    continue
            targets.append(actor)

        for actor in targets:
            result = snap_to_ground(actor, offset=offset, ignore_actors=targets)
            if result:
                snapped.append(result)
            else:
                failed.append(actor.get_actor_label())
    except Exception as e:
        unreal.log_error(f"[arbor.actors] snap_all_to_ground: {e}")

    unreal.log(f"[arbor.actors] snap_all_to_ground: snapped {len(snapped)}, failed {len(failed)}")
    return {"snapped": snapped, "failed": failed}


def snap_selected_to_ground(offset=0.0):
    """Snap currently selected actors to the ground below them.

    All selected actors are ignored in each other's traces.

    Args:
        offset: Extra Z offset after snapping.

    Returns:
        Dict ``{"snapped": [...], "failed": [...]}``.
    """
    snapped = []
    failed = []
    try:
        sub = _get_actor_subsystem()
        if sub:
            selected = list(sub.get_selected_level_actors())
        else:
            selected = list(unreal.EditorLevelLibrary.get_selected_level_actors())

        for actor in selected:
            result = snap_to_ground(actor, offset=offset, ignore_actors=selected)
            if result:
                snapped.append(result)
            else:
                failed.append(actor.get_actor_label())
    except Exception as e:
        unreal.log_error(f"[arbor.actors] snap_selected_to_ground: {e}")

    unreal.log(f"[arbor.actors] snap_selected_to_ground: snapped {len(snapped)}, failed {len(failed)}")
    return {"snapped": snapped, "failed": failed}
