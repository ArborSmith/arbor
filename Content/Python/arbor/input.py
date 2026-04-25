"""Arbor input — simulate player input during PIE sessions.

Provides functions to press/release keyboard keys, rotate the camera,
and perform timed input sequences (hold key for N seconds, smooth look).

Synchronous functions (5) execute immediately and write results via
``write_result()``.  Tick-driven async functions (4: ``tap_key``,
``hold_key``, ``smooth_look``, ``move_direction``) register a Slate
pre-tick callback and write results when the operation completes.
"""

import json
import math
import os
import time

import unreal

from arbor.utils import write_result, make_rotator, _to_vector
from arbor.playtest import _get_pie_player, _capture_result_path, _write_result_to


# ---------------------------------------------------------------------------
# Key name mapping: friendly names → UE5 FKey names
# ---------------------------------------------------------------------------

KEY_MAP = {
    # Letters
    "a": "A", "b": "B", "c": "C", "d": "D", "e": "E", "f": "F",
    "g": "G", "h": "H", "i": "I", "j": "J", "k": "K", "l": "L",
    "m": "M", "n": "N", "o": "O", "p": "P", "q": "Q", "r": "R",
    "s": "S", "t": "T", "u": "U", "v": "V", "w": "W", "x": "X",
    "y": "Y", "z": "Z",
    # Numbers
    "0": "Zero", "1": "One", "2": "Two", "3": "Three", "4": "Four",
    "5": "Five", "6": "Six", "7": "Seven", "8": "Eight", "9": "Nine",
    # Special keys
    "space": "SpaceBar", "spacebar": "SpaceBar",
    "enter": "Enter", "return": "Enter",
    "escape": "Escape", "esc": "Escape",
    "tab": "Tab",
    "backspace": "BackSpace",
    # Modifiers
    "shift": "LeftShift", "leftshift": "LeftShift", "rightshift": "RightShift",
    "ctrl": "LeftControl", "leftctrl": "LeftControl", "rightctrl": "RightControl",
    "alt": "LeftAlt", "leftalt": "LeftAlt", "rightalt": "RightAlt",
    # Arrows
    "up": "Up", "down": "Down", "left": "Left", "right": "Right",
    # Mouse buttons
    "lmb": "LeftMouseButton", "leftmousebutton": "LeftMouseButton",
    "rmb": "RightMouseButton", "rightmousebutton": "RightMouseButton",
    "mmb": "MiddleMouseButton", "middlemousebutton": "MiddleMouseButton",
    # Function keys
    "f1": "F1", "f2": "F2", "f3": "F3", "f4": "F4",
    "f5": "F5", "f6": "F6", "f7": "F7", "f8": "F8",
    "f9": "F9", "f10": "F10", "f11": "F11", "f12": "F12",
}

# Direction → key combinations
DIRECTION_KEYS = {
    "forward": ["w"],
    "backward": ["s"],
    "left": ["a"],
    "right": ["d"],
    "forward_left": ["w", "a"],
    "forward_right": ["w", "d"],
    "backward_left": ["s", "a"],
    "backward_right": ["s", "d"],
}


# ---------------------------------------------------------------------------
# Module-level state
# ---------------------------------------------------------------------------

_held_keys = set()           # Currently held FKey names
_timed_actions = []          # Active timed actions
_input_callback = None       # Shared tick callback handle
_navigate_state = None       # Current navigate_path state (or None)
_navigate_callback = None    # Registered tick callback handle for navigate_path


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _resolve_key(key):
    """Resolve a friendly key name to a UE5 FKey name.

    Accepts both friendly names (``"space"``, ``"w"``) and raw FKey names
    (``"SpaceBar"``, ``"W"``).  Case-insensitive for friendly names.
    """
    lower = key.lower().strip()
    if lower in KEY_MAP:
        return KEY_MAP[lower]
    # Already a valid FKey name — pass through as-is
    return key


def _get_game_world():
    """Get the PIE game world for console command execution."""
    try:
        world = unreal.EditorLevelLibrary.get_game_world()
        if world is not None:
            return world
    except Exception:
        pass
    return None


def _exec_input_cmd(cmd):
    """Execute an input console command in the PIE game world."""
    world = _get_game_world()
    unreal.SystemLibrary.execute_console_command(world, cmd, None)


def _ensure_tick_callback():
    """Register the shared tick callback if not already registered."""
    global _input_callback
    if _input_callback is None:
        _input_callback = unreal.register_slate_pre_tick_callback(_input_tick)


def _ease_in_out(t):
    """Smooth ease-in-out interpolation (cubic)."""
    if t < 0.5:
        return 4.0 * t * t * t
    return 1.0 - (-2.0 * t + 2.0) ** 3 / 2.0


# ---------------------------------------------------------------------------
# Tick callback
# ---------------------------------------------------------------------------

def _input_tick(delta_time):
    """Shared tick callback for all timed input actions."""
    global _timed_actions, _input_callback

    if not _timed_actions:
        if _input_callback is not None:
            try:
                unreal.unregister_slate_pre_tick_callback(_input_callback)
            except Exception:
                pass
            _input_callback = None
        return

    dt = delta_time if delta_time and delta_time > 0 else 0.016
    completed = []

    for action in _timed_actions:
        action["elapsed"] += dt

        if action["type"] == "key_hold":
            if action["elapsed"] >= action["duration"]:
                # Release all keys for this action
                for fkey in action["keys"]:
                    _exec_input_cmd(f"Input.-key {fkey}")
                    _held_keys.discard(fkey)
                completed.append(action)
                _write_result_to({
                    "success": True,
                    "action": "key_hold_complete",
                    "keys": action["keys"],
                    "duration": action["duration"],
                }, action.get("result_path"))

        elif action["type"] == "smooth_look":
            t = min(action["elapsed"] / action["duration"], 1.0)
            t_eased = _ease_in_out(t)

            new_yaw = action["start_yaw"] + action["yaw_delta"] * t_eased
            new_pitch = action["start_pitch"] + action["pitch_delta"] * t_eased
            # Clamp pitch to avoid gimbal issues
            new_pitch = max(-89.0, min(89.0, new_pitch))

            controller = action["controller"]
            rot = make_rotator(new_pitch, new_yaw, 0.0)
            controller.set_control_rotation(rot)

            if action["elapsed"] >= action["duration"]:
                completed.append(action)
                final_rot = controller.get_control_rotation()
                _write_result_to({
                    "success": True,
                    "action": "smooth_look_complete",
                    "rotation": {
                        "Pitch": final_rot.pitch,
                        "Yaw": final_rot.yaw,
                        "Roll": final_rot.roll,
                    },
                }, action.get("result_path"))

        elif action["type"] == "move_to":
            pawn, controller = _get_pie_player()

            if pawn is None:
                _exec_input_cmd("Input.-key W")
                _held_keys.discard("W")
                completed.append(action)
                _write_result_to({
                    "success": False,
                    "error": "PIE stopped during move_to",
                }, action.get("result_path"))
                continue

            if action["elapsed"] >= action["timeout"]:
                _exec_input_cmd("Input.-key W")
                _held_keys.discard("W")
                completed.append(action)
                _write_result_to({
                    "success": False,
                    "timed_out": True,
                    "error": f"move_to timed out after {action['timeout']}s",
                }, action.get("result_path"))
                continue

            pawn_loc = pawn.get_actor_location()
            target = action["target"]
            dx = target.x - pawn_loc.x
            dy = target.y - pawn_loc.y
            dist_2d = math.sqrt(dx * dx + dy * dy)

            # Smooth course-correct heading each tick
            if dist_2d > 10.0 and controller is not None:
                target_yaw = math.degrees(math.atan2(dy, dx))
                current_yaw = controller.get_control_rotation().yaw
                diff = (target_yaw - current_yaw + 180.0) % 360.0 - 180.0
                turn_speed = 5.0
                new_yaw = current_yaw + diff * min(1.0, dt * turn_speed)
                controller.set_control_rotation(make_rotator(0.0, new_yaw, 0.0))

            if dist_2d <= action["arrive_radius"]:
                _exec_input_cmd("Input.-key W")
                _held_keys.discard("W")
                completed.append(action)

                final_loc = pawn.get_actor_location()
                result_data = {
                    "success": True,
                    "action": "move_to_complete",
                    "location": {"X": final_loc.x, "Y": final_loc.y, "Z": final_loc.z},
                    "distance_remaining": round(dist_2d, 1),
                }

                if action.get("screenshot_on_arrive") and controller is not None:
                    from arbor.playtest import _capture_from_pawn
                    path = _capture_from_pawn(pawn, controller, prefix="move_to")
                    result_data["screenshot_path"] = path

                _write_result_to(result_data, action.get("result_path"))

    for c in completed:
        _timed_actions.remove(c)


# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------

def _cancel_all_input():
    """Cancel all timed input actions and unregister the tick callback."""
    global _timed_actions, _input_callback
    _timed_actions.clear()
    if _input_callback is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(_input_callback)
        except Exception:
            pass
        _input_callback = None


# ---------------------------------------------------------------------------
# Synchronous public API
# ---------------------------------------------------------------------------

def press_key(key):
    """Press and hold a key.

    The key stays pressed until ``release_key()`` or ``release_all_keys()``
    is called.  Uses the UE5 ``Input.+key`` console command which works
    with both legacy and Enhanced Input systems.

    Args:
        key: Key name — friendly (``"w"``, ``"space"``, ``"shift"``) or
            raw UE5 FKey name (``"W"``, ``"SpaceBar"``).

    Returns:
        dict: ``{"success": True, "key": str, "fkey": str, "action": "pressed"}``
    """
    pawn, _ = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE not running or no player pawn"}
        write_result(result)
        return result

    fkey = _resolve_key(key)
    _exec_input_cmd(f"Input.+key {fkey}")
    _held_keys.add(fkey)

    result = {"success": True, "key": key, "fkey": fkey, "action": "pressed"}
    unreal.log(f"[arbor.input] Pressed {fkey}")
    write_result(result)
    return result


def release_key(key):
    """Release a held key.

    Args:
        key: Key name — friendly or raw UE5 FKey name.

    Returns:
        dict: ``{"success": True, "key": str, "fkey": str, "action": "released"}``
    """
    fkey = _resolve_key(key)
    _exec_input_cmd(f"Input.-key {fkey}")
    _held_keys.discard(fkey)

    result = {"success": True, "key": key, "fkey": fkey, "action": "released"}
    unreal.log(f"[arbor.input] Released {fkey}")
    write_result(result)
    return result


def release_all_keys():
    """Release all currently held keys.

    Safe to call even if no keys are held.

    Returns:
        dict: ``{"success": True, "released": list}``
    """
    released = list(_held_keys)
    for fkey in released:
        try:
            _exec_input_cmd(f"Input.-key {fkey}")
        except Exception:
            pass
    _held_keys.clear()

    result = {"success": True, "released": released}
    if released:
        unreal.log(f"[arbor.input] Released all keys: {released}")
    write_result(result)
    return result


def look_direction(yaw_delta=0.0, pitch_delta=0.0):
    """Adjust the camera by a relative offset.

    Delegates to ``smooth_look`` with a short duration so the rotation is
    applied across multiple ticks.  A single-frame ``set_control_rotation``
    gets overridden by most third-person camera systems, so this ensures
    the rotation actually sticks.

    Positive yaw = turn right, positive pitch = look up.

    Args:
        yaw_delta: Horizontal rotation in degrees (default 0).
        pitch_delta: Vertical rotation in degrees (default 0).

    Returns:
        dict: ``{"success": True, "rotation": {"Pitch", "Yaw", "Roll"}}``
    """
    unreal.log(f"[arbor.input] Look direction: yaw={yaw_delta}, pitch={pitch_delta}")
    return smooth_look(yaw_delta=yaw_delta, pitch_delta=pitch_delta, duration=0.05)


def get_held_keys():
    """Return the list of currently held keys.

    Returns:
        dict: ``{"success": True, "held_keys": list}``
    """
    result = {"success": True, "held_keys": sorted(_held_keys)}
    write_result(result)
    return result


# ---------------------------------------------------------------------------
# Tick-driven async public API
# ---------------------------------------------------------------------------

def tap_key(key, duration=0.1):
    """Press a key, hold for *duration* seconds, then release.

    This is a tick-driven async function — it returns immediately and the
    result is written when the tap completes.

    Args:
        key: Key name — friendly or raw UE5 FKey name.
        duration: How long to hold the key in seconds (default 0.1).
            Minimum effective duration is ~1 frame (8-33ms).

    Returns:
        dict: Immediately returns info dict.  Final result written async.
    """
    pawn, _ = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE not running or no player pawn"}
        write_result(result)
        return result

    result_path = _capture_result_path()
    fkey = _resolve_key(key)

    _exec_input_cmd(f"Input.+key {fkey}")
    _held_keys.add(fkey)

    _timed_actions.append({
        "type": "key_hold",
        "keys": [fkey],
        "duration": duration,
        "elapsed": 0.0,
        "result_path": result_path,
    })
    _ensure_tick_callback()

    unreal.log(f"[arbor.input] Tap {fkey} for {duration}s")


def hold_key(key, duration):
    """Press and hold a key for exactly *duration* seconds, then release.

    Identical to ``tap_key`` but intended for longer holds.

    Args:
        key: Key name — friendly or raw UE5 FKey name.
        duration: How long to hold the key in seconds.
    """
    tap_key(key, duration)


def smooth_look(yaw_delta=0.0, pitch_delta=0.0, duration=1.0):
    """Smoothly rotate the camera over *duration* seconds.

    Uses cubic ease-in-out interpolation for natural-looking movement.
    This is a tick-driven async function.

    Args:
        yaw_delta: Total horizontal rotation in degrees (positive = right).
        pitch_delta: Total vertical rotation in degrees (positive = up).
        duration: Time in seconds for the rotation (default 1.0).
    """
    pawn, controller = _get_pie_player()
    if controller is None:
        result = {"success": False, "error": "PIE not running or no player controller"}
        write_result(result)
        return result

    result_path = _capture_result_path()
    rot = controller.get_control_rotation()

    _timed_actions.append({
        "type": "smooth_look",
        "controller": controller,
        "start_yaw": rot.yaw,
        "start_pitch": rot.pitch,
        "yaw_delta": yaw_delta,
        "pitch_delta": pitch_delta,
        "duration": duration,
        "elapsed": 0.0,
        "result_path": result_path,
    })
    _ensure_tick_callback()

    unreal.log(f"[arbor.input] Smooth look: yaw={yaw_delta}, pitch={pitch_delta} over {duration}s")


def move_direction(direction, duration):
    """Move the player in a direction for *duration* seconds.

    Presses the appropriate WASD keys for the direction, holds them for
    the specified duration, then releases.  This is a tick-driven async
    function.

    Args:
        direction: One of ``"forward"``, ``"backward"``, ``"left"``,
            ``"right"``, ``"forward_left"``, ``"forward_right"``,
            ``"backward_left"``, ``"backward_right"``.
        duration: How long to move in seconds.
    """
    pawn, _ = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE not running or no player pawn"}
        write_result(result)
        return result

    keys = DIRECTION_KEYS.get(direction.lower().strip())
    if keys is None:
        result = {
            "success": False,
            "error": f"Unknown direction '{direction}'. "
                     f"Valid: {', '.join(DIRECTION_KEYS.keys())}",
        }
        write_result(result)
        return result

    result_path = _capture_result_path()
    fkeys = [_resolve_key(k) for k in keys]

    for fkey in fkeys:
        _exec_input_cmd(f"Input.+key {fkey}")
        _held_keys.add(fkey)

    _timed_actions.append({
        "type": "key_hold",
        "keys": fkeys,
        "duration": duration,
        "elapsed": 0.0,
        "result_path": result_path,
    })
    _ensure_tick_callback()

    unreal.log(f"[arbor.input] Move {direction} ({fkeys}) for {duration}s")


def move_to(target_location, arrive_radius=100.0, timeout=30.0,
            screenshot_on_arrive=False):
    """Walk toward a target location using WASD input.

    Uses the game's movement system (CharacterMovementComponent) — respects
    gravity, collisions, and triggers animations.  Each tick the controller
    yaw is set to face the target and the W key is held down.

    This is a tick-driven async function.

    Args:
        target_location: ``(x, y, z)`` or ``unreal.Vector`` — world position.
        arrive_radius: Horizontal distance threshold in cm (default 100).
        timeout: Max seconds before giving up (default 30).
        screenshot_on_arrive: Take a screenshot on arrival (default False).
    """
    pawn, controller = _get_pie_player()
    if pawn is None:
        result = {"success": False, "error": "PIE not running or no player pawn"}
        write_result(result)
        return result

    result_path = _capture_result_path()
    target = _to_vector(target_location)

    # Cancel any existing move_to action
    _timed_actions[:] = [a for a in _timed_actions if a["type"] != "move_to"]

    # Press W — the tick lerp will smoothly turn toward target
    _exec_input_cmd("Input.+key W")
    _held_keys.add("W")

    _timed_actions.append({
        "type": "move_to",
        "target": target,
        "arrive_radius": arrive_radius,
        "timeout": timeout,
        "screenshot_on_arrive": screenshot_on_arrive,
        "elapsed": 0.0,
        "result_path": result_path,
    })
    _ensure_tick_callback()

    unreal.log(f"[arbor.input] move_to ({target.x:.0f}, {target.y:.0f}, {target.z:.0f})"
               f" radius={arrive_radius} timeout={timeout}s")


# ---------------------------------------------------------------------------
# Convenience wrappers
# ---------------------------------------------------------------------------

def jump():
    """Tap the space bar to jump.

    Equivalent to ``tap_key("space", 0.15)``.
    """
    tap_key("space", 0.15)


def interact():
    """Tap the E key to interact.

    Equivalent to ``tap_key("e", 0.1)``.
    """
    tap_key("e", 0.1)


# ---------------------------------------------------------------------------
# navigate_path — input-based multi-waypoint movement
# ---------------------------------------------------------------------------

def navigate_path(waypoints, arrive_radius=100.0, timeout_per_waypoint=30.0,
                  screenshot_at_waypoints=False):
    """Walk through a series of waypoints using WASD input.

    Input-based alternative to ``playtest.walk_path`` — uses the game's
    movement system (CharacterMovementComponent) so gravity, collisions,
    and animations are fully active.

    This is a tick-driven async function.

    Args:
        waypoints: List of ``(x, y, z)`` positions to walk through.
        arrive_radius: Horizontal distance threshold per waypoint in cm
            (default 100).
        timeout_per_waypoint: Max seconds per waypoint before giving up
            (default 30).
        screenshot_at_waypoints: Take a screenshot at each waypoint
            (default False).
    """
    global _navigate_state, _navigate_callback

    if not waypoints or len(waypoints) == 0:
        write_result({"success": False, "error": "No waypoints provided"})
        return

    pawn, controller = _get_pie_player()
    if pawn is None:
        write_result({"success": False, "error": "PIE not running or no player pawn"})
        return

    _cancel_navigate()

    result_path = _capture_result_path()
    wp_vectors = [_to_vector(wp) for wp in waypoints]

    # Press W — the MOVING phase lerp will smoothly turn toward first waypoint
    _exec_input_cmd("Input.+key W")
    _held_keys.add("W")

    _navigate_state = {
        "phase": "MOVING",
        "waypoints": wp_vectors,
        "arrive_radius": arrive_radius,
        "timeout": timeout_per_waypoint,
        "screenshot_at_waypoints": screenshot_at_waypoints,
        "current_index": 0,
        "waypoint_data": [],
        "wp_elapsed": 0.0,
        "result_path": result_path,
        "start_time": time.time(),
    }

    _navigate_callback = unreal.register_slate_pre_tick_callback(_navigate_tick)
    unreal.log(f"[arbor.input] navigate_path started: {len(wp_vectors)} waypoints")


def _navigate_tick(delta_time):
    """Tick callback driving the navigate_path state machine."""
    global _navigate_state, _navigate_callback

    if _navigate_state is None:
        if _navigate_callback is not None:
            try:
                unreal.unregister_slate_pre_tick_callback(_navigate_callback)
            except Exception:
                pass
            _navigate_callback = None
        return

    state = _navigate_state
    phase = state["phase"]
    dt = delta_time if delta_time and delta_time > 0 else 0.016
    now = time.time()

    if phase == "MOVING":
        state["wp_elapsed"] += dt

        pawn, controller = _get_pie_player()
        if pawn is None:
            _exec_input_cmd("Input.-key W")
            _held_keys.discard("W")
            _finish_navigate({
                "success": True,
                "interrupted": True,
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            })
            return

        # Per-waypoint timeout
        if state["wp_elapsed"] >= state["timeout"]:
            _exec_input_cmd("Input.-key W")
            _held_keys.discard("W")
            _finish_navigate({
                "success": False,
                "timed_out": True,
                "error": f"Timed out at waypoint {state['current_index']}",
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            })
            return

        target = state["waypoints"][state["current_index"]]
        pawn_loc = pawn.get_actor_location()
        dx = target.x - pawn_loc.x
        dy = target.y - pawn_loc.y
        dist_2d = math.sqrt(dx * dx + dy * dy)

        # Smooth course-correct heading
        if dist_2d > 10.0 and controller is not None:
            target_yaw = math.degrees(math.atan2(dy, dx))
            current_yaw = controller.get_control_rotation().yaw
            diff = (target_yaw - current_yaw + 180.0) % 360.0 - 180.0
            turn_speed = 5.0
            new_yaw = current_yaw + diff * min(1.0, dt * turn_speed)
            controller.set_control_rotation(make_rotator(0.0, new_yaw, 0.0))

        # Arrival check
        if dist_2d <= state["arrive_radius"]:
            if state["screenshot_at_waypoints"]:
                state["phase"] = "SCREENSHOT"
            else:
                idx = state["current_index"]
                state["waypoint_data"].append({
                    "index": idx,
                    "location": {"X": target.x, "Y": target.y, "Z": target.z},
                    "arrived": True,
                    "elapsed": round(state["wp_elapsed"], 2),
                    "screenshot_path": None,
                })
                state["phase"] = "NEXT"

    elif phase == "SCREENSHOT":
        pawn, controller = _get_pie_player()
        idx = state["current_index"]
        target = state["waypoints"][idx]

        screenshot_path = None
        if pawn is not None and controller is not None:
            from arbor.playtest import _capture_from_pawn
            screenshot_path = _capture_from_pawn(pawn, controller,
                                                  prefix=f"nav_wp{idx}")

        state["waypoint_data"].append({
            "index": idx,
            "location": {"X": target.x, "Y": target.y, "Z": target.z},
            "arrived": True,
            "elapsed": round(state["wp_elapsed"], 2),
            "screenshot_path": screenshot_path,
        })
        state["phase"] = "NEXT"

    elif phase == "NEXT":
        idx = state["current_index"] + 1
        if idx >= len(state["waypoints"]):
            # All waypoints visited
            _exec_input_cmd("Input.-key W")
            _held_keys.discard("W")
            _finish_navigate({
                "success": True,
                "action": "navigate_path_complete",
                "waypoints_visited": len(state["waypoint_data"]),
                "waypoint_data": state["waypoint_data"],
                "total_time": round(now - state["start_time"], 2),
            })
        else:
            # Face next waypoint
            state["current_index"] = idx
            state["wp_elapsed"] = 0.0
            state["phase"] = "MOVING"

            # No hard snap here — the MOVING phase lerp handles smooth turning


def _cancel_navigate():
    """Cancel any active navigate_path operation and release keys."""
    global _navigate_state, _navigate_callback
    if _navigate_callback is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(_navigate_callback)
        except Exception:
            pass
        _navigate_callback = None
    _navigate_state = None
    if "W" in _held_keys:
        _exec_input_cmd("Input.-key W")
        _held_keys.discard("W")


def _finish_navigate(result_data):
    """Write the navigate result and clean up."""
    global _navigate_state, _navigate_callback

    result_path = None
    if _navigate_state is not None:
        result_path = _navigate_state.get("result_path")

    _cancel_navigate()
    _write_result_to(result_data, result_path)
    unreal.log(f"[arbor.input] navigate_path finished: "
               f"{result_data.get('waypoints_visited', 0)} waypoints")
