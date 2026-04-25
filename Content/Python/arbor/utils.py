"""Arbor shared utilities — foundation layer for all other modules.

Provides type coercion, actor discovery, asset operations, spawning,
deletion, and MCP bridge JSON output.
"""

import json
import functools
import os

import unreal


# ---------------------------------------------------------------------------
# Error handling
# ---------------------------------------------------------------------------

def _safe_arbor_call(func):
    """Decorator that ensures arbor functions never return bare None.

    On failure, returns {"success": False, "error": "..."} instead of None,
    making errors visible to Claude Code instead of silently passing.
    """
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        try:
            result = func(*args, **kwargs)
            if result is None:
                return {"success": False, "error": f"{func.__name__} returned None — check UE5 Output Log"}
            return result
        except Exception as e:
            unreal.log_error(f"[arbor] {func.__name__}: {e}")
            return {"success": False, "error": str(e)}
    return wrapper


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _to_vector(v):
    """Coerce *v* to ``unreal.Vector``.

    Accepts ``unreal.Vector``, ``(x, y, z)`` tuple/list,
    or ``{"X": x, "Y": y, "Z": z}`` dict.
    """
    if isinstance(v, unreal.Vector):
        return v
    if isinstance(v, dict):
        return unreal.Vector(
            v.get("X", v.get("x", 0.0)),
            v.get("Y", v.get("y", 0.0)),
            v.get("Z", v.get("z", 0.0)),
        )
    if isinstance(v, (list, tuple)):
        return unreal.Vector(*v)
    return unreal.Vector(0.0, 0.0, 0.0)


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    """Create an ``unreal.Rotator`` with correct axis mapping.

    UE5's Python ``unreal.Rotator(a, b, c)`` constructor maps to
    ``roll=a, pitch=b, yaw=c``.  This helper accepts intuitive
    ``(pitch, yaw, roll)`` and reorders the arguments so callers
    never have to think about it.

    Args:
        pitch: Pitch in degrees (look up/down).
        yaw: Yaw in degrees (turn left/right).
        roll: Roll in degrees.

    Returns:
        A correctly-oriented ``unreal.Rotator``.
    """
    return unreal.Rotator(roll, pitch, yaw)


def _to_rotator(r):
    """Coerce *r* to ``unreal.Rotator``.

    Accepts ``unreal.Rotator``, ``(pitch, yaw, roll)`` tuple/list,
    or ``{"Pitch": p, "Yaw": y, "Roll": r}`` dict.
    """
    if isinstance(r, unreal.Rotator):
        return r
    if isinstance(r, dict):
        return make_rotator(
            r.get("Pitch", r.get("pitch", 0.0)),
            r.get("Yaw", r.get("yaw", 0.0)),
            r.get("Roll", r.get("roll", 0.0)),
        )
    if isinstance(r, (list, tuple)):
        return make_rotator(*r)
    return make_rotator(0.0, 0.0, 0.0)


def _to_linear_color(c):
    """Coerce *c* to ``unreal.LinearColor``.

    Accepts ``unreal.LinearColor``, ``(R, G, B)`` or ``(R, G, B, A)``
    tuple/list with float components in 0-1 range.
    """
    if isinstance(c, unreal.LinearColor):
        return c
    if isinstance(c, (list, tuple)):
        if len(c) >= 4:
            return unreal.LinearColor(c[0], c[1], c[2], c[3])
        return unreal.LinearColor(c[0], c[1], c[2], 1.0)
    return unreal.LinearColor(1.0, 1.0, 1.0, 1.0)


def _get_actor_subsystem():
    """Return ``EditorActorSubsystem`` if available, else ``None``."""
    try:
        return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    except Exception:
        return None


def _get_all_level_actors():
    """Return all actors in the current level using the best available API."""
    sub = _get_actor_subsystem()
    if sub:
        return sub.get_all_level_actors()
    return unreal.EditorLevelLibrary.get_all_level_actors()


def _resolve_actor(name_or_ref):
    """Resolve an actor from a name string or direct reference."""
    if isinstance(name_or_ref, str):
        return find_actor_by_name(name_or_ref)
    return name_or_ref


# ---------------------------------------------------------------------------
# Actor discovery
# ---------------------------------------------------------------------------

def find_actor_by_name(name):
    """Find the first actor whose editor label matches *name* (case-insensitive).

    Args:
        name: Actor label to search for.

    Returns:
        The matching ``unreal.Actor``, or ``None`` if not found.
    """
    try:
        target = name.lower()
        for actor in _get_all_level_actors():
            if actor.get_actor_label().lower() == target:
                return actor
        return None
    except Exception as e:
        unreal.log_error(f"[arbor.utils] find_actor_by_name: {e}")
        return None


def find_actors_by_class(class_name):
    """Return all level actors of the given class.

    Args:
        class_name: Unreal class name string (e.g. ``"StaticMeshActor"``)
                    or an ``unreal.Class`` object.

    Returns:
        List of matching actors, or empty list on failure.
    """
    try:
        if isinstance(class_name, str):
            cls = unreal.EditorAssetLibrary.load_asset(f"/Script/Engine.{class_name}")
            if cls is None:
                cls = getattr(unreal, class_name, None)
            if cls is None:
                unreal.log_warning(f"[arbor.utils] find_actors_by_class: class '{class_name}' not found")
                return []
        else:
            cls = class_name
        return unreal.GameplayStatics.get_all_actors_of_class(
            unreal.EditorLevelLibrary.get_editor_world(), cls
        )
    except Exception as e:
        unreal.log_error(f"[arbor.utils] find_actors_by_class: {e}")
        return []


def get_all_actors():
    """Return info dicts for every actor in the current level.

    Returns:
        List of dicts with keys: ``name``, ``class``, ``position``,
        ``rotation``, ``scale``.
    """
    try:
        result = []
        for actor in _get_all_level_actors():
            loc = actor.get_actor_location()
            rot = actor.get_actor_rotation()
            scl = actor.get_actor_scale3d()
            result.append({
                "name": actor.get_actor_label(),
                "class": actor.get_class().get_name(),
                "position": {"X": loc.x, "Y": loc.y, "Z": loc.z},
                "rotation": {"Pitch": rot.pitch, "Yaw": rot.yaw, "Roll": rot.roll},
                "scale": {"X": scl.x, "Y": scl.y, "Z": scl.z},
            })
        return result
    except Exception as e:
        unreal.log_error(f"[arbor.utils] get_all_actors: {e}")
        return []


# ---------------------------------------------------------------------------
# Asset operations
# ---------------------------------------------------------------------------

def load_asset(path):
    """Load an asset by content path (e.g. ``/Game/Materials/M_Foo``).

    Returns:
        The loaded ``UObject``, or ``None`` on failure.
    """
    try:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            unreal.log_warning(f"[arbor.utils] load_asset: '{path}' not found")
        return asset
    except Exception as e:
        unreal.log_error(f"[arbor.utils] load_asset: {e}")
        return None


def save_asset(path):
    """Save a loaded asset by its content path.

    Args:
        path: Content path string, or a ``UObject`` whose path will be resolved.

    Returns:
        ``True`` on success, ``False`` on failure.
    """
    try:
        if not isinstance(path, str):
            path = path.get_path_name()
        return unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    except Exception as e:
        unreal.log_error(f"[arbor.utils] save_asset: {e}")
        return False


# ---------------------------------------------------------------------------
# MCP bridge output
# ---------------------------------------------------------------------------

def write_result(data, path=None):
    """Write *data* as JSON for the MCP bridge to read.

    When called from the ue5-bridge wrapper (which sets the
    ``UE5_BRIDGE_RESULT_PATH`` env var), writes to that path so the
    bridge can poll it.  Otherwise falls back to
    ``<ProjectSavedDir>/Arbor/last_result.json``.

    Args:
        data: Any JSON-serializable object.
        path: Output file path.  When *None*, uses the bridge env var
              if set, otherwise the project-relative default.
    """
    try:
        if path is None:
            bridge_path = os.environ.get("UE5_BRIDGE_RESULT_PATH")
            if bridge_path:
                path = bridge_path
            else:
                saved = unreal.Paths.project_saved_dir()
                out_dir = os.path.join(saved, "Arbor")
                os.makedirs(out_dir, exist_ok=True)
                path = os.path.join(out_dir, "last_result.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, default=str)
        unreal.log(f"[arbor.utils] write_result: wrote {path}")
    except Exception as e:
        unreal.log_error(f"[arbor.utils] write_result: {e}")


def get_settings():
    """Read Arbor settings from the ``UArborSettings`` CDO.

    Returns:
        Dict of setting key-value pairs.  Empty dict on failure.
    """
    try:
        settings = unreal.ArborSettings.get_default_object()
        return {
            "fab_search_thumbnails": settings.get_editor_property("fab_search_thumbnails"),
            "show_anchor_debug": settings.get_editor_property("show_anchor_debug"),
            "anchor_debug_duration": settings.get_editor_property("anchor_debug_duration"),
        }
    except Exception as e:
        unreal.log_warning(f"[arbor.utils] get_settings: {e}")
    return {}


def live_compile():
    """Trigger a Live Coding (C++ hot reload) compile.

    Executes the ``Arbor.LiveCompile`` console command, which calls
    ``ILiveCodingModule::Compile()`` on the game thread.  The compile
    itself is asynchronous — this function returns immediately after
    triggering it.

    Returns:
        Dict with ``success`` key.  Always ``True`` if the command was
        sent; actual compile errors appear in the UE5 Output Log.
    """
    try:
        unreal.SystemLibrary.execute_console_command(None, "Arbor.LiveCompile")
        unreal.log("[arbor.utils] live_compile: triggered Live Coding compile")
        return {"success": True, "message": "Live Coding compile triggered"}
    except Exception as e:
        unreal.log_error(f"[arbor.utils] live_compile: {e}")
        return {"success": False, "error": str(e)}


def run_arbor_command(command_name, json_data, asset_path):
    """Safely run an Arbor console command with JSON data.

    Base64-encodes the JSON to avoid spaces-in-paths issues with
    console command argument parsing.

    Prefer the direct Python wrappers (``build_bt``, ``build_bp``, etc.)
    over this function — they use C++ reflection which returns asset
    objects and has better error reporting.

    Args:
        command_name: Console command (e.g. ``"Arbor.BuildBT"``).
        json_data: Dict to serialize as JSON.
        asset_path: UE content path for the output asset.
    """
    import base64
    encoded = base64.b64encode(json.dumps(json_data).encode()).decode()
    cmd = f'{command_name} {encoded} {asset_path}'
    unreal.EditorLevelLibrary.execute_console_command(cmd)


def safe_run(fn, *args, **kwargs):
    """Call *fn* and auto-write result to MCP bridge (success or error).

    On success writes ``{"success": True, "result": <return_value>}``.
    On error writes ``{"success": False, "error": <msg>, "traceback": ...}``
    and logs the error.  Always writes to ``last_result.json`` so the
    bridge never reads stale data.

    Args:
        fn: Callable to invoke.
        *args: Positional arguments for *fn*.
        **kwargs: Keyword arguments for *fn*.

    Returns:
        The return value of *fn*, or ``None`` on error.
    """
    try:
        result = fn(*args, **kwargs)
        write_result({"success": True, "result": result})
        return result
    except Exception as e:
        import traceback
        msg = f"{fn.__name__}: {e}" if hasattr(fn, "__name__") else str(e)
        unreal.log_error(f"[arbor.utils] safe_run: {msg}")
        write_result({"success": False, "error": msg,
                       "traceback": traceback.format_exc()})
        return None


# ---------------------------------------------------------------------------
# Spawning
# ---------------------------------------------------------------------------

def spawn_actor(asset_path_or_class, location=(0, 0, 0), rotation=(0, 0, 0),
                scale=(1, 1, 1), label=None):
    """Spawn an actor in the current level.

    *asset_path_or_class* can be:
    - A content path to a StaticMesh (e.g. ``/Engine/BasicShapes/Cube.Cube``)
      → spawns a ``StaticMeshActor`` with that mesh.
    - A content path to a Blueprint (e.g. ``/Game/BP/BP_Enemy``)
      → spawns an instance of that blueprint.
    - An ``unreal`` class (e.g. ``unreal.PointLight``)
      → spawns that actor class directly.

    Args:
        asset_path_or_class: See above.
        location: ``(x, y, z)`` or ``unreal.Vector``.
        rotation: ``(pitch, yaw, roll)`` or ``unreal.Rotator``.
        scale: ``(x, y, z)`` or ``unreal.Vector``.
        label: Optional display name in editor.

    Returns:
        The spawned ``unreal.Actor``, or ``None`` on failure.
    """
    try:
        loc = _to_vector(location)
        rot = _to_rotator(rotation)
        scl = _to_vector(scale)

        actor = None

        if isinstance(asset_path_or_class, str):
            asset = load_asset(asset_path_or_class)
            if asset is None:
                return None

            if isinstance(asset, unreal.StaticMesh):
                actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                    unreal.StaticMeshActor, loc, rot
                )
                if actor:
                    actor.static_mesh_component.set_static_mesh(asset)
            elif isinstance(asset, unreal.Blueprint):
                actor = unreal.EditorLevelLibrary.spawn_actor_from_object(
                    asset, loc, rot
                )
            else:
                # Try as a class object
                actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                    asset, loc, rot
                )
        else:
            # Direct class reference
            actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                asset_path_or_class, loc, rot
            )

        if actor is None:
            unreal.log_error("[arbor.utils] spawn_actor: failed to spawn")
            return None

        actor.set_actor_scale3d(scl)
        if label:
            actor.set_actor_label(label)

        unreal.log(f"[arbor.utils] spawn_actor: spawned '{actor.get_actor_label()}' at ({loc.x}, {loc.y}, {loc.z})")
        return actor
    except Exception as e:
        unreal.log_error(f"[arbor.utils] spawn_actor: {e}")
        return None


# ---------------------------------------------------------------------------
# Deletion
# ---------------------------------------------------------------------------

def delete_actor(name_or_ref):
    """Destroy a single actor by name or reference.

    Args:
        name_or_ref: Actor label string or ``unreal.Actor`` reference.

    Returns:
        ``True`` if destroyed, ``False`` otherwise.
    """
    try:
        actor = _resolve_actor(name_or_ref)
        if actor is None:
            unreal.log_warning(f"[arbor.utils] delete_actor: actor not found — {name_or_ref}")
            return False
        sub = _get_actor_subsystem()
        if sub:
            sub.destroy_actor(actor)
        else:
            actor.destroy_actor()
        unreal.log(f"[arbor.utils] delete_actor: destroyed '{name_or_ref}'")
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.utils] delete_actor: {e}")
        return False


def batch_delete(names):
    """Destroy multiple actors by name.

    Args:
        names: List of actor label strings.

    Returns:
        Dict with ``deleted`` and ``not_found`` lists.
    """
    deleted = []
    not_found = []
    for name in names:
        if delete_actor(name):
            deleted.append(name)
        else:
            not_found.append(name)
    return {"deleted": deleted, "not_found": not_found}
