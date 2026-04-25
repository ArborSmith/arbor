"""Arbor playtest — automated PIE testing tools.

Provides functions to start/stop Play-In-Editor sessions, teleport and
control the player, take screenshots from the player's viewpoint, check
framerate and nav reachability, and walk automated paths through the level.

Synchronous functions (9) execute immediately and write results via
``write_result()``.  Tick-driven async functions (2: ``walk_path``,
``run_playtest``) register a Slate pre-tick callback and write results
when the operation completes across multiple frames.
"""

import json
import math
import os
import time

import unreal

from arbor.utils import _to_vector, _to_rotator, make_rotator, write_result
from arbor.capture import _fast_capture, _ensure_screenshot_dir, _default_filename


# ---------------------------------------------------------------------------
# Module-level state for tick-driven async operations
# ---------------------------------------------------------------------------

_walk_state = None        # Current walk_path state dict (or None)
_walk_callback = None     # Registered tick callback handle
_playtest_state = None    # Current run_playtest state dict (or None)
_playtest_callback = None


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _vec_to_dict(v):
    """Convert an ``unreal.Vector`` to a JSON-friendly dict."""
    return {"X": v.x, "Y": v.y, "Z": v.z}


def _rot_to_dict(r):
    """Convert an ``unreal.Rotator`` to a JSON-friendly dict."""
    return {"Pitch": r.pitch, "Yaw": r.yaw, "Roll": r.roll}


def _get_pie_player():
    """Return ``(pawn, controller)`` for player 0 in the PIE world.

    Returns ``(None, None)`` if PIE is not running or pawn not ready.
    """
    # During PIE, get_editor_world() returns None — use get_game_world()
    try:
        world = unreal.EditorLevelLibrary.get_game_world()
        if world is not None:
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            if pawn is not None:
                return pawn, controller
    except Exception:
        pass

    # Fallback: try editor world (for SIE or non-PIE contexts)
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world is not None:
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            if pawn is not None:
                return pawn, controller
    except Exception:
        pass

    # Last resort: try None world context
    try:
        pawn = unreal.GameplayStatics.get_player_pawn(None, 0)
        controller = unreal.GameplayStatics.get_player_controller(None, 0)
        return pawn, controller
    except Exception:
        return None, None


def _cpp_playtest_available():
    """Check if C++ ArborPlaytestTools is available."""
    try:
        _ = unreal.ArborPlaytestTools
        return True
    except AttributeError:
        return False


def _is_pie_active():
    """Check if PIE is currently running."""
    # Try C++ first — reliable across UE versions
    if _cpp_playtest_available():
        import json as _json
        try:
            r = _json.loads(unreal.ArborPlaytestTools.is_pierunning())
            return r.get("running", False)
        except Exception:
            pass

    try:
        sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if sub and hasattr(sub, "is_in_play_in_editor"):
            return sub.is_in_play_in_editor()
    except Exception:
        pass

    # Fallback: check if a player pawn exists
    pawn, _ = _get_pie_player()
    return pawn is not None


def _capture_from_pawn(pawn, controller, prefix="playtest"):
    """Take a screenshot from the player's camera viewpoint.

    Uses the controller's view point if available, otherwise falls back
    to the pawn location with an eye-height offset.

    Returns the file path on success, ``None`` on failure.
    """
    out_dir = _ensure_screenshot_dir()
    fname = _default_filename(prefix)
    full_path = os.path.join(out_dir, fname)

    loc = None
    rot = None

    # Try controller.get_player_view_point()
    if controller is not None:
        try:
            loc, rot = controller.get_player_view_point()
        except Exception:
            pass

    # Fallback: pawn location + eye height offset
    if loc is None:
        loc = pawn.get_actor_location()
        # Approximate eye height (~88 cm for default Character)
        loc = unreal.Vector(loc.x, loc.y, loc.z + 88.0)
        rot = pawn.get_actor_rotation()

    return _fast_capture(loc, rot, full_path)


def _get_current_fps():
    """Return instantaneous FPS from world delta seconds."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        dt = unreal.GameplayStatics.get_world_delta_seconds(world)
        if dt > 0:
            return round(1.0 / dt, 1)
    except Exception:
        pass

    try:
        dt = unreal.GameplayStatics.get_world_delta_seconds(None)
        if dt and dt > 0:
            return round(1.0 / dt, 1)
    except Exception:
        pass

    return 0.0


def _compute_look_rotation(from_loc, to_loc):
    """Compute a rotation looking from *from_loc* toward *to_loc*.

    Returns an ``unreal.Rotator`` with correct pitch and yaw.
    """
    dx = to_loc.x - from_loc.x
    dy = to_loc.y - from_loc.y
    dz = to_loc.z - from_loc.z

    dist_xy = math.sqrt(dx * dx + dy * dy)
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, dist_xy)) if dist_xy > 0.01 else 0.0

    return make_rotator(pitch, yaw, 0.0)


def _capture_result_path():
    """Capture the current ``UE5_BRIDGE_RESULT_PATH`` env var.

    For tick-driven async functions, the bridge wrapper clears this env var
    in its ``finally`` block after the Python call returns.  We must capture
    it at call time so the tick callback can write directly to it later.

    Returns the path string, or ``None`` if not set.
    """
    return os.environ.get("UE5_BRIDGE_RESULT_PATH")


def _write_result_to(data, path):
    """Write *data* as JSON to a specific file path.

    Used by tick callbacks that captured the result path at call time.
    Falls back to ``write_result()`` if *path* is ``None``.
    """
    if path is None:
        write_result(data)
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, default=str)
        unreal.log(f"[arbor.playtest] Wrote result to {path}")
    except Exception as e:
        unreal.log_error(f"[arbor.playtest] Failed to write result to {path}: {e}")
        # Last resort fallback
        write_result(data)


def _cancel_walk():
    """Cancel any active walk_path operation."""
    global _walk_state, _walk_callback
    if _walk_callback is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(_walk_callback)
        except Exception:
            pass
        _walk_callback = None
    _walk_state = None


def _cancel_playtest():
    """Cancel any active run_playtest operation."""
    global _playtest_state, _playtest_callback
    if _playtest_callback is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(_playtest_callback)
        except Exception:
            pass
        _playtest_callback = None
    _playtest_state = None


# ---------------------------------------------------------------------------
# Synchronous public API
# ---------------------------------------------------------------------------

def start_pie():
    """Start a Play-In-Editor session.

    Delegates to C++ ``ArborPlaytestTools.StartPIE`` when available.
    Falls back to multiple Python approaches.

    PIE needs a few frames to initialize — use ``is_pie_running()``
    to poll for readiness.

    Returns:
        dict: ``{"success": True, "mode": "PIE"|"SIE"}``
    """
    # --- Try C++ backend first ---
    if _cpp_playtest_available():
        import json as _json
        try:
            result = _json.loads(unreal.ArborPlaytestTools.start_pie())
            write_result(result)
            return result
        except Exception as e:
            unreal.log_warning(f"[arbor.playtest] C++ StartPIE failed: {e}")

    # Already running?
    if _is_pie_active():
        pawn, _ = _get_pie_player()
        mode = "PIE" if pawn is not None else "SIE"
        result = {"success": True, "mode": mode, "note": "PIE already running"}
        write_result(result)
        return result

    # Approach 1: LevelEditorSubsystem
    try:
        sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if sub and hasattr(sub, "editor_request_begin_play"):
            sub.editor_request_begin_play()
            unreal.log("[arbor.playtest] PIE started via LevelEditorSubsystem")
            result = {"success": True, "mode": "PIE"}
            write_result(result)
            return result
    except Exception as e:
        unreal.log_warning(f"[arbor.playtest] LevelEditorSubsystem approach failed: {e}")

    # Approach 2: Console command
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "pie", None)
        unreal.log("[arbor.playtest] PIE started via console command")
        result = {"success": True, "mode": "PIE"}
        write_result(result)
        return result
    except Exception as e:
        unreal.log_warning(f"[arbor.playtest] Console command approach failed: {e}")

    # Approach 3: SIE fallback
    try:
        unreal.EditorLevelLibrary.editor_play_simulate()
        unreal.log("[arbor.playtest] SIE started via EditorLevelLibrary")
        result = {"success": True, "mode": "SIE"}
        write_result(result)
        return result
    except Exception as e:
        unreal.log_warning(f"[arbor.playtest] SIE fallback failed: {e}")

    result = {"success": False, "error": "All PIE start methods failed"}
    write_result(result)
    return result


def stop_pie():
    """Stop the current PIE/SIE session.

    Also cancels any active ``walk_path`` or ``run_playtest`` operations.

    Returns:
        dict: ``{"success": True}``
    """
    _cancel_walk()
    _cancel_playtest()

    # Release any held input keys and cancel timed input actions
    try:
        from arbor.input import release_all_keys as _release_keys, _cancel_all_input, _cancel_navigate
        _release_keys()
        _cancel_all_input()
        _cancel_navigate()
    except Exception:
        pass

    try:
        unreal.EditorLevelLibrary.editor_end_play()
        unreal.log("[arbor.playtest] PIE stopped")
        result = {"success": True}
        write_result(result)
        return result
    except Exception as e:
        result = {"success": False, "error": f"Failed to stop PIE: {e}"}
        write_result(result)
        return result


def is_pie_running():
    """Check if PIE is running and a player pawn exists.

    Delegates to C++ ``ArborPlaytestTools.IsPIERunning`` when available.

    Returns:
        dict: ``{"running": bool, "has_player": bool, "player_location": dict|null}``
    """
    # --- Try C++ backend ---
    if _cpp_playtest_available():
        import json as _json
        try:
            result = _json.loads(unreal.ArborPlaytestTools.is_pierunning())
            # Normalize key names for Python API compatibility
            result["player_location"] = result.pop("player_location", None)
            write_result(result)
            return result
        except Exception:
            pass

    running = _is_pie_active()
    pawn, _ = _get_pie_player()
    has_player = pawn is not None

    player_loc = None
    if has_player:
        loc = pawn.get_actor_location()
        player_loc = _vec_to_dict(loc)

    result = {
        "running": running,
        "has_player": has_player,
        "player_location": player_loc,
    }
    write_result(result)
    return result


def teleport_player(location, rotation=None):
    """Teleport the player pawn to a specific location.

    Uses ``set_actor_location`` with ``teleport=True`` to bypass physics.

    Args:
        location: ``(x, y, z)`` or ``unreal.Vector``.
        rotation: ``(pitch, yaw, roll)`` or ``unreal.Rotator`` (optional).

    Returns:
        dict: ``{"success": True, "location": dict}``
    """
    pawn, controller = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE is not running. Call start_pie() first."}
        write_result(result)
        return result

    loc = _to_vector(location)

    # Teleport with sweep=False, teleport=True to bypass physics
    pawn.set_actor_location(loc, sweep=False, teleport=True)

    if rotation is not None:
        rot = _to_rotator(rotation)
        if controller is not None:
            try:
                controller.set_control_rotation(rot)
            except Exception:
                pawn.set_actor_rotation(rot, teleport_physics=True)
        else:
            pawn.set_actor_rotation(rot, teleport_physics=True)

    final_loc = pawn.get_actor_location()
    result = {"success": True, "location": _vec_to_dict(final_loc)}
    write_result(result)
    return result


def look_at(target_location):
    """Rotate the player to look at a target location.

    Computes pitch/yaw from the player's current position to the target
    and applies the rotation to the player controller.

    Args:
        target_location: ``(x, y, z)`` or ``unreal.Vector``.

    Returns:
        dict: ``{"success": True, "rotation": dict}``
    """
    pawn, controller = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE is not running. Call start_pie() first."}
        write_result(result)
        return result

    target = _to_vector(target_location)
    pawn_loc = pawn.get_actor_location()
    rot = _compute_look_rotation(pawn_loc, target)

    if controller is not None:
        try:
            controller.set_control_rotation(rot)
        except Exception:
            pawn.set_actor_rotation(rot, teleport_physics=True)
    else:
        pawn.set_actor_rotation(rot, teleport_physics=True)

    result = {"success": True, "rotation": _rot_to_dict(rot)}
    write_result(result)
    return result


def screenshot_from_player():
    """Take a screenshot from the player's current viewpoint during PIE.

    Uses the controller's camera view point and ``_fast_capture`` for a
    synchronous JPEG capture.

    Returns:
        str: Absolute file path of the screenshot.
    """
    pawn, controller = _get_pie_player()
    if pawn is None:
        write_result({"success": False, "error": "PIE is not running. Call start_pie() first."})
        return None

    path = _capture_from_pawn(pawn, controller, prefix="playtest")
    if path:
        write_result({"success": True, "screenshot_path": path})
    else:
        write_result({"success": False, "error": "Screenshot capture failed"})
    return path


def get_player_location():
    """Get the player pawn's current location, rotation, and velocity.

    Returns:
        dict: ``{"location": dict, "rotation": dict, "velocity": dict}``
    """
    pawn, _ = _get_pie_player()
    if pawn is None:
        write_result({"success": False, "error": "PIE is not running. Call start_pie() first."})
        return None

    loc = pawn.get_actor_location()
    rot = pawn.get_actor_rotation()

    vel_dict = {"X": 0, "Y": 0, "Z": 0}
    try:
        vel = pawn.get_velocity()
        vel_dict = _vec_to_dict(vel)
    except Exception:
        pass

    result = {
        "success": True,
        "location": _vec_to_dict(loc),
        "rotation": _rot_to_dict(rot),
        "velocity": vel_dict,
    }
    write_result(result)
    return result


def get_framerate():
    """Get the current instantaneous framerate.

    Returns:
        dict: ``{"fps": float}``
    """
    fps = _get_current_fps()
    result = {"success": True, "fps": fps}
    write_result(result)
    return result


def check_player_can_reach(from_loc, to_loc):
    """Check if a path exists between two locations via the navigation mesh.

    Delegates to C++ ``ArborPlaytestTools.CheckPlayerCanReach`` when available.

    Args:
        from_loc: ``(x, y, z)`` start location.
        to_loc: ``(x, y, z)`` destination location.

    Returns:
        dict: ``{"reachable": bool, "partial": bool, "path_length": float, "path_points": list}``
    """
    # --- Try C++ backend ---
    if _cpp_playtest_available():
        import json as _json
        try:
            s = _to_vector(from_loc)
            e = _to_vector(to_loc)
            result = _json.loads(unreal.ArborPlaytestTools.check_player_can_reach(
                _json.dumps({"from": [s.x, s.y, s.z], "to": [e.x, e.y, e.z]})))
            write_result(result)
            return result
        except Exception:
            pass

    start = _to_vector(from_loc)
    end = _to_vector(to_loc)

    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        nav_sys = unreal.NavigationSystemV1.get_navigation_system(world)
        if nav_sys is None:
            write_result({
                "success": False,
                "error": "No NavigationSystem found. Ensure a NavMeshBoundsVolume exists and nav mesh is built.",
            })
            return None

        nav_path = nav_sys.find_path_to_location_synchronously(
            world, start, end
        )

        if nav_path is None:
            write_result({
                "success": True,
                "reachable": False,
                "partial": False,
                "path_length": 0.0,
                "path_points": [],
                "note": "Navigation query returned null — nav mesh may not be built.",
            })
            return None

        is_valid = nav_path.is_valid()
        is_partial = nav_path.is_partial()
        path_points = []
        path_length = 0.0

        try:
            points = nav_path.path_points
            for pt in points:
                path_points.append(_vec_to_dict(pt))

            # Calculate total path length
            for i in range(1, len(points)):
                dx = points[i].x - points[i - 1].x
                dy = points[i].y - points[i - 1].y
                dz = points[i].z - points[i - 1].z
                path_length += math.sqrt(dx * dx + dy * dy + dz * dz)
        except Exception:
            pass

        result = {
            "success": True,
            "reachable": is_valid and not is_partial,
            "partial": is_partial,
            "path_length": round(path_length, 1),
            "path_points": path_points,
        }
        write_result(result)
        return result

    except Exception as e:
        write_result({
            "success": False,
            "error": f"Navigation query failed: {e}",
        })
        return None


# ---------------------------------------------------------------------------
# Tick-driven async: walk_path
# ---------------------------------------------------------------------------

def walk_path(waypoints, speed=600, screenshot_at_waypoints=True):
    """Walk the player through a series of waypoints, taking screenshots.

    Movement uses ``set_actor_location(sweep=True)`` — straight lines
    between waypoints with collision.  This tests collision, not navmesh
    pathfinding (use ``check_player_can_reach`` for nav queries).

    This is a tick-driven async function.  It returns immediately without
    writing a result.  A Slate pre-tick callback drives the walk across
    frames and writes the result when complete.

    Args:
        waypoints: List of ``(x, y, z)`` positions to walk through.
        speed: Movement speed in cm/s (default 600).
        screenshot_at_waypoints: Take a screenshot at each waypoint (default True).

    Returns:
        dict: Immediately returns info dict.  Final result written async:
            ``{waypoint_data: [...], total_time, avg_fps}``
    """
    global _walk_state, _walk_callback

    if not waypoints or len(waypoints) == 0:
        write_result({"success": False, "error": "No waypoints provided"})
        return

    # Cancel any previous walk
    _cancel_walk()

    # Capture the result path before the bridge clears it
    result_path = _capture_result_path()

    # Convert waypoints to Vectors
    wp_vectors = [_to_vector(wp) for wp in waypoints]

    _walk_state = {
        "phase": "INIT",
        "waypoints": wp_vectors,
        "speed": speed,
        "screenshot_at_waypoints": screenshot_at_waypoints,
        "current_index": 0,
        "waypoint_data": [],
        "start_time": time.time(),
        "last_time": time.time(),
        "fps_samples": [],
        "init_wait_start": time.time(),
        "result_path": result_path,
    }

    _walk_callback = unreal.register_slate_pre_tick_callback(_walk_tick)
    unreal.log(f"[arbor.playtest] walk_path started: {len(wp_vectors)} waypoints, speed={speed}")


def _walk_tick(delta_time):
    """Tick callback that drives the walk_path state machine."""
    global _walk_state, _walk_callback

    if _walk_state is None:
        # Shouldn't happen, but safety net
        if _walk_callback is not None:
            try:
                unreal.unregister_slate_pre_tick_callback(_walk_callback)
            except Exception:
                pass
            _walk_callback = None
        return

    state = _walk_state
    phase = state["phase"]

    # Use delta_time if provided and valid, else compute from wall clock
    dt = delta_time if delta_time and delta_time > 0 else 0.016
    now = time.time()

    if phase == "INIT":
        # Wait for pawn to be available (up to ~2s)
        pawn, controller = _get_pie_player()
        if pawn is not None:
            state["phase"] = "MOVING"
            state["last_time"] = now
            unreal.log("[arbor.playtest] walk_path: pawn found, starting walk")
            return

        if now - state["init_wait_start"] > 2.0:
            _finish_walk({
                "success": False,
                "error": "Player pawn not ready after 2s. Is PIE running?",
            })
            return

    elif phase == "MOVING":
        pawn, controller = _get_pie_player()
        if pawn is None:
            # PIE was stopped mid-walk
            _finish_walk({
                "success": True,
                "interrupted": True,
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            })
            return

        # Record FPS
        fps = _get_current_fps()
        if fps > 0:
            state["fps_samples"].append(fps)

        current_loc = pawn.get_actor_location()
        target = state["waypoints"][state["current_index"]]

        # Direction to target
        dx = target.x - current_loc.x
        dy = target.y - current_loc.y
        dz = target.z - current_loc.z
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)

        step = state["speed"] * dt

        if dist <= step or dist < 5.0:
            # Arrived at waypoint
            pawn.set_actor_location(target, sweep=True, teleport=False)
            state["phase"] = "ARRIVED"
        else:
            # Move toward waypoint
            ratio = step / dist
            new_loc = unreal.Vector(
                current_loc.x + dx * ratio,
                current_loc.y + dy * ratio,
                current_loc.z + dz * ratio,
            )
            pawn.set_actor_location(new_loc, sweep=True, teleport=False)

    elif phase == "ARRIVED":
        pawn, controller = _get_pie_player()
        if pawn is None:
            _finish_walk({
                "success": True,
                "interrupted": True,
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            })
            return

        idx = state["current_index"]
        target = state["waypoints"][idx]

        wp_data = {
            "index": idx,
            "location": _vec_to_dict(target),
            "fps": _get_current_fps(),
            "arrived": True,
            "screenshot_path": None,
        }

        # Screenshot at waypoint
        if state["screenshot_at_waypoints"] and controller is not None:
            path = _capture_from_pawn(pawn, controller, prefix=f"walk_wp{idx}")
            wp_data["screenshot_path"] = path

        state["waypoint_data"].append(wp_data)
        state["phase"] = "NEXT"

    elif phase == "NEXT":
        idx = state["current_index"] + 1
        if idx >= len(state["waypoints"]):
            # All waypoints visited — done
            avg_fps = 0.0
            if state["fps_samples"]:
                avg_fps = round(sum(state["fps_samples"]) / len(state["fps_samples"]), 1)

            _finish_walk({
                "success": True,
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
                "avg_fps": avg_fps,
                "waypoints_visited": len(state["waypoint_data"]),
            })
        else:
            state["current_index"] = idx
            state["phase"] = "MOVING"


def _finish_walk(result_data):
    """Write the walk result and clean up."""
    global _walk_state, _walk_callback

    result_path = None
    if _walk_state is not None:
        result_path = _walk_state.get("result_path")

    _cancel_walk()
    _write_result_to(result_data, result_path)
    unreal.log(f"[arbor.playtest] walk_path finished: {result_data.get('waypoints_visited', 0)} waypoints")


# ---------------------------------------------------------------------------
# Tick-driven async: run_playtest
# ---------------------------------------------------------------------------

def run_playtest(waypoints=None, auto_generate_path=False, speed=600):
    """Run a full playtest: start PIE, walk waypoints, stop PIE.

    Higher-level orchestrator that handles the full lifecycle:
    ``START_PIE → WAIT_PIE_READY → WALKING → STOP_PIE → WRITE_REPORT``

    If ``auto_generate_path=True`` and no waypoints are given, samples
    ~8 random reachable points from the nav mesh.

    This is a tick-driven async function — returns immediately, writes
    result when complete.

    Args:
        waypoints: List of ``(x, y, z)`` positions, or ``None`` for auto.
        auto_generate_path: Sample walkable points from nav mesh (default False).
        speed: Movement speed in cm/s (default 600).
    """
    global _playtest_state, _playtest_callback

    _cancel_playtest()
    _cancel_walk()

    # Capture the result path before the bridge clears it
    result_path = _capture_result_path()

    # Auto-generate waypoints from nav mesh if requested
    resolved_waypoints = None
    if waypoints:
        resolved_waypoints = [_to_vector(wp) for wp in waypoints]
    elif auto_generate_path:
        resolved_waypoints = _auto_generate_waypoints()
        if not resolved_waypoints:
            _write_result_to({
                "success": False,
                "error": "auto_generate_path failed — no nav mesh or no reachable points found.",
            }, result_path)
            return

    if not resolved_waypoints or len(resolved_waypoints) == 0:
        _write_result_to({
            "success": False,
            "error": "No waypoints provided and auto_generate_path is False.",
        }, result_path)
        return

    _playtest_state = {
        "phase": "START_PIE",
        "waypoints": resolved_waypoints,
        "speed": speed,
        "start_time": time.time(),
        "pie_wait_start": None,
        "walk_result": None,
        "result_path": result_path,
    }

    _playtest_callback = unreal.register_slate_pre_tick_callback(_playtest_tick)
    unreal.log(f"[arbor.playtest] run_playtest started: {len(resolved_waypoints)} waypoints")


def _auto_generate_waypoints(count=8, radius=5000.0):
    """Sample random reachable points from the navigation mesh.

    Returns a list of ``unreal.Vector`` or empty list on failure.
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        nav_sys = unreal.NavigationSystemV1.get_navigation_system(world)
        if nav_sys is None:
            return []

        # Get a starting point — use the first PlayerStart or world origin
        origin = unreal.Vector(0, 0, 0)
        try:
            starts = unreal.GameplayStatics.get_all_actors_of_class(
                world, unreal.PlayerStart
            )
            if starts and len(starts) > 0:
                origin = starts[0].get_actor_location()
        except Exception:
            pass

        points = []
        for _ in range(count * 3):  # Over-sample to account for failures
            try:
                pt, success = nav_sys.get_random_reachable_point_in_navigable_radius(
                    world, origin, radius
                )
                if success:
                    # Check we haven't already got a point too close
                    too_close = False
                    for existing in points:
                        dx = pt.x - existing.x
                        dy = pt.y - existing.y
                        if math.sqrt(dx * dx + dy * dy) < 200:
                            too_close = True
                            break
                    if not too_close:
                        points.append(pt)
                        if len(points) >= count:
                            break
            except Exception:
                continue

        return points

    except Exception as e:
        unreal.log_warning(f"[arbor.playtest] Auto-generate waypoints failed: {e}")
        return []


def _playtest_tick(delta_time):
    """Tick callback that drives the run_playtest state machine."""
    global _playtest_state, _playtest_callback, _walk_state

    if _playtest_state is None:
        if _playtest_callback is not None:
            try:
                unreal.unregister_slate_pre_tick_callback(_playtest_callback)
            except Exception:
                pass
            _playtest_callback = None
        return

    state = _playtest_state
    phase = state["phase"]
    now = time.time()

    if phase == "START_PIE":
        # Start PIE if not already running
        if not _is_pie_active():
            # Call start_pie logic directly (without write_result)
            try:
                sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
                if sub and hasattr(sub, "editor_request_play_session"):
                    sub.editor_request_play_session()
                    unreal.log("[arbor.playtest] run_playtest: PIE start requested")
            except Exception:
                try:
                    world = unreal.EditorLevelLibrary.get_editor_world()
                    unreal.SystemLibrary.execute_console_command(world, "pie", None)
                except Exception:
                    try:
                        unreal.EditorLevelLibrary.editor_play_simulate()
                    except Exception as e:
                        _finish_playtest({
                            "success": False,
                            "error": f"Failed to start PIE: {e}",
                        })
                        return

        state["phase"] = "WAIT_PIE_READY"
        state["pie_wait_start"] = now

    elif phase == "WAIT_PIE_READY":
        pawn, _ = _get_pie_player()
        if pawn is not None:
            state["phase"] = "WALKING"
            unreal.log("[arbor.playtest] run_playtest: PIE ready, starting walk")
            # Start the walk — but without writing result (we'll read it from _walk_state)
            _start_walk_for_playtest(state["waypoints"], state["speed"])
            return

        if now - state["pie_wait_start"] > 5.0:
            _finish_playtest({
                "success": False,
                "error": "PIE did not start within 5 seconds.",
            })
            return

    elif phase == "WALKING":
        # Check if walk is done (walk_state is None means it finished)
        if _walk_state is None:
            state["phase"] = "STOP_PIE"

    elif phase == "STOP_PIE":
        try:
            unreal.EditorLevelLibrary.editor_end_play()
            unreal.log("[arbor.playtest] run_playtest: PIE stopped")
        except Exception:
            pass

        state["phase"] = "WRITE_REPORT"

    elif phase == "WRITE_REPORT":
        walk_result = state.get("walk_result", {})
        report = {
            "success": True,
            "total_time": round(now - state["start_time"], 2),
            "walk_result": walk_result,
        }
        _finish_playtest(report)


def _start_walk_for_playtest(waypoints, speed):
    """Start a walk_path as part of run_playtest (no bridge result write)."""
    global _walk_state, _walk_callback

    _cancel_walk()

    _walk_state = {
        "phase": "MOVING",  # Skip INIT since we know pawn exists
        "waypoints": waypoints,
        "speed": speed,
        "screenshot_at_waypoints": True,
        "current_index": 0,
        "waypoint_data": [],
        "start_time": time.time(),
        "last_time": time.time(),
        "fps_samples": [],
        "init_wait_start": time.time(),
        "result_path": "__playtest__",  # Sentinel: don't write, store for playtest
    }

    _walk_callback = unreal.register_slate_pre_tick_callback(_walk_tick_for_playtest)


def _walk_tick_for_playtest(delta_time):
    """Walk tick variant for run_playtest — stores result instead of writing it."""
    global _walk_state, _walk_callback, _playtest_state

    if _walk_state is None:
        if _walk_callback is not None:
            try:
                unreal.unregister_slate_pre_tick_callback(_walk_callback)
            except Exception:
                pass
            _walk_callback = None
        return

    state = _walk_state
    phase = state["phase"]
    dt = delta_time if delta_time and delta_time > 0 else 0.016
    now = time.time()

    if phase == "MOVING":
        pawn, controller = _get_pie_player()
        if pawn is None:
            # PIE stopped
            result = {
                "success": True,
                "interrupted": True,
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            }
            if _playtest_state is not None:
                _playtest_state["walk_result"] = result
            _cancel_walk()
            return

        fps = _get_current_fps()
        if fps > 0:
            state["fps_samples"].append(fps)

        current_loc = pawn.get_actor_location()
        target = state["waypoints"][state["current_index"]]

        dx = target.x - current_loc.x
        dy = target.y - current_loc.y
        dz = target.z - current_loc.z
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        step = state["speed"] * dt

        if dist <= step or dist < 5.0:
            pawn.set_actor_location(target, sweep=True, teleport=False)
            state["phase"] = "ARRIVED"
        else:
            ratio = step / dist
            new_loc = unreal.Vector(
                current_loc.x + dx * ratio,
                current_loc.y + dy * ratio,
                current_loc.z + dz * ratio,
            )
            pawn.set_actor_location(new_loc, sweep=True, teleport=False)

    elif phase == "ARRIVED":
        pawn, controller = _get_pie_player()
        if pawn is None:
            result = {
                "success": True,
                "interrupted": True,
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            }
            if _playtest_state is not None:
                _playtest_state["walk_result"] = result
            _cancel_walk()
            return

        idx = state["current_index"]
        target = state["waypoints"][idx]
        wp_data = {
            "index": idx,
            "location": _vec_to_dict(target),
            "fps": _get_current_fps(),
            "arrived": True,
            "screenshot_path": None,
        }

        if state["screenshot_at_waypoints"] and controller is not None:
            path = _capture_from_pawn(pawn, controller, prefix=f"playtest_wp{idx}")
            wp_data["screenshot_path"] = path

        state["waypoint_data"].append(wp_data)
        state["phase"] = "NEXT"

    elif phase == "NEXT":
        idx = state["current_index"] + 1
        if idx >= len(state["waypoints"]):
            avg_fps = 0.0
            if state["fps_samples"]:
                avg_fps = round(sum(state["fps_samples"]) / len(state["fps_samples"]), 1)

            result = {
                "success": True,
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
                "avg_fps": avg_fps,
                "waypoints_visited": len(state["waypoint_data"]),
            }
            if _playtest_state is not None:
                _playtest_state["walk_result"] = result
            _cancel_walk()
        else:
            state["current_index"] = idx
            state["phase"] = "MOVING"


def _finish_playtest(result_data):
    """Write the playtest result and clean up."""
    global _playtest_state, _playtest_callback

    result_path = None
    if _playtest_state is not None:
        result_path = _playtest_state.get("result_path")

    _cancel_playtest()
    _cancel_walk()
    _write_result_to(result_data, result_path)
    unreal.log(f"[arbor.playtest] run_playtest finished")
