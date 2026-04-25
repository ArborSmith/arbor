"""Arbor viewport capture — take screenshots from the UE5 editor viewport.

Provides functions to capture the active viewport, move the camera to
specific positions, and take bird's-eye or orbit screenshots.  Useful for
visual verification of level layout during AI-assisted building.

Uses SceneCapture2D + synchronous JPEG export for fast captures (~50-100ms)
instead of the async AutomationLibrary pipeline.
"""

import math
import os
import time

import unreal

from arbor.utils import _to_vector, _to_rotator, make_rotator, write_result

# Capture resolution — 960x540 is sufficient for verification screenshots.
_CAPTURE_WIDTH = 960
_CAPTURE_HEIGHT = 540


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------

def _ensure_screenshot_dir(output_path=None):
    """Return (and create) the screenshot output directory.

    If *output_path* is given, use it directly.  Otherwise default to
    ``{ProjectSavedDir}/Arbor/Screenshots/``.

    Always returns an absolute path — ``project_saved_dir()`` can return a
    path relative to the engine binary, which is unusable from Node.js.
    """
    if output_path:
        output_path = os.path.abspath(output_path)
        os.makedirs(output_path, exist_ok=True)
        return output_path
    saved = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir()
    )
    out_dir = os.path.join(saved, "Arbor", "Screenshots")
    os.makedirs(out_dir, exist_ok=True)
    return out_dir


def _default_filename(prefix="screenshot"):
    """Return a timestamped filename like ``screenshot_20260225_143022.jpg``."""
    ts = time.strftime("%Y%m%d_%H%M%S")
    return f"{prefix}_{ts}.jpg"


def _wait_for_file(path, timeout=5.0, poll=0.25):
    """Poll until *path* exists on disk (or timeout).  Returns ``True``/``False``."""
    start = time.time()
    while time.time() - start < timeout:
        if os.path.isfile(path):
            return True
        time.sleep(poll)
    return False


def _get_editor_subsystem():
    """Return ``UnrealEditorSubsystem`` if available, else ``None``."""
    try:
        return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    except Exception:
        return None


def _get_viewport_camera():
    """Return ``(location, rotation)`` of the active viewport camera.

    Returns ``(None, None)`` if the camera info cannot be retrieved.
    """
    subsystem = _get_editor_subsystem()
    if subsystem is not None:
        try:
            loc, rot = subsystem.get_level_viewport_camera_info()
            return loc, rot
        except Exception:
            pass

    try:
        loc, rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
        return loc, rot
    except Exception:
        pass

    return None, None


def _move_viewport_camera(location, rotation):
    """Move the active level-viewport camera to *location* / *rotation*.

    Uses ``UnrealEditorSubsystem.set_level_viewport_camera_info`` when
    available.  Falls back to a console-command approach.
    """
    loc = _to_vector(location)
    rot = _to_rotator(rotation)

    subsystem = _get_editor_subsystem()
    if subsystem is not None:
        try:
            subsystem.set_level_viewport_camera_info(loc, rot)
            unreal.log(
                f"[arbor.capture] Moved viewport camera to "
                f"({loc.x}, {loc.y}, {loc.z})"
            )
            return True
        except Exception as e:
            unreal.log_warning(
                f"[arbor.capture] set_level_viewport_camera_info failed: {e}"
            )

    # Fallback: try EditorLevelLibrary (deprecated but still works)
    try:
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)
        unreal.log(
            f"[arbor.capture] Moved viewport camera (fallback) to "
            f"({loc.x}, {loc.y}, {loc.z})"
        )
        return True
    except Exception as e:
        unreal.log_warning(
            f"[arbor.capture] EditorLevelLibrary fallback also failed: {e}"
        )

    return False


def _fast_capture(location, rotation, full_path):
    """Capture the scene using SceneCapture2D + synchronous JPEG export.

    Spawns a temporary SceneCapture2D actor, renders the scene into a small
    render target, and writes JPEG synchronously.  The file exists on disk
    when this function returns — no polling needed.

    Args:
        location: ``unreal.Vector`` — camera position.
        rotation: ``unreal.Rotator`` — camera orientation.
        full_path: Absolute path for the output file (should end in ``.jpg``).

    Returns:
        *full_path* on success, ``None`` on error.
    """
    full_path = full_path.replace("\\", "/")

    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        # Create render target
        rt = unreal.RenderingLibrary.create_render_target2d(
            world,
            width=_CAPTURE_WIDTH,
            height=_CAPTURE_HEIGHT,
            format=unreal.TextureRenderTargetFormat.RTF_RGBA8,
        )

        # Spawn SceneCapture2D actor
        capture_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SceneCapture2D,
            location,
        )
        comp = capture_actor.get_component_by_class(
            unreal.SceneCaptureComponent2D
        )
        comp.texture_target = rt
        comp.set_editor_property("capture_every_frame", False)
        comp.set_editor_property("always_persist_rendering_state", True)

        # Use final color with tone curve so exposure/post-processing matches
        # the viewport (default SCS_SceneColorHDR is pre-tonemapping = dark).
        comp.set_editor_property(
            "capture_source",
            unreal.SceneCaptureSource.SCS_FINAL_TONE_CURVE_HDR,
        )

        # Boost exposure — SceneCapture has no exposure history so auto-exposure
        # starts cold and settles dark.  A +2 bias compensates.
        pp = comp.post_process_settings
        pp.set_editor_property("override_auto_exposure_bias", True)
        pp.set_editor_property("auto_exposure_bias", 2.0)
        comp.post_process_settings = pp

        # Set camera orientation
        capture_actor.set_actor_rotation(rotation, False)

        # Render the scene synchronously into the render target
        comp.capture_scene()

        # Write JPEG synchronously — file is on disk when this returns
        options = unreal.ImageWriteOptions()
        options.format = unreal.DesiredImageFormat.JPG
        options.compression_quality = 85
        options.overwrite_file = True
        options.async_ = False

        unreal.ImageWriteBlueprintLibrary.export_to_disk(rt, full_path, options)

        # Cleanup
        capture_actor.destroy_actor()

        unreal.log(f"[arbor.capture] Fast capture → {full_path}")
        return full_path

    except Exception as e:
        unreal.log_warning(f"[arbor.capture] Fast capture failed: {e}")
        # Clean up actor if it was created
        try:
            capture_actor.destroy_actor()
        except Exception:
            pass
        return None


def _take_screenshot_impl(full_path):
    """Capture the active viewport to *full_path* (legacy async fallback).

    Uses ``AutomationLibrary.take_high_res_screenshot`` which is async —
    the file is written after this function returns and the engine ticks.
    We must NOT block the game thread with ``time.sleep()`` or the
    screenshot will never render.

    Returns *full_path* optimistically on success, ``None`` on error.
    """
    # Normalise to forward slashes (UE5 console expects them)
    full_path = full_path.replace("\\", "/")

    # --- AutomationLibrary.take_high_res_screenshot (async) ---
    try:
        lib = unreal.AutomationLibrary
        if hasattr(lib, "take_high_res_screenshot"):
            lib.take_high_res_screenshot(1920, 1080, full_path)
            unreal.log(
                f"[arbor.capture] Screenshot requested (async) → {full_path}"
            )
            return full_path
    except Exception as e:
        unreal.log_warning(
            f"[arbor.capture] AutomationLibrary unavailable: {e}"
        )

    # --- Fallback: HighResShot console command (also async) ---
    try:
        cmd = f'HighResShot 1920x1080 filename="{full_path}"'
        unreal.log(f"[arbor.capture] Running console command: {cmd}")
        try:
            world = unreal.EditorLevelLibrary.get_editor_world()
            unreal.KismetSystemLibrary.execute_console_command(world, cmd)
        except Exception:
            unreal.EditorLevelLibrary.execute_console_command(cmd)
        unreal.log(
            f"[arbor.capture] Screenshot requested (HighResShot) → {full_path}"
        )
        return full_path
    except Exception as e:
        unreal.log_warning(
            f"[arbor.capture] HighResShot console command failed: {e}"
        )

    unreal.log_error(
        f"[arbor.capture] All screenshot methods failed. "
        f"Expected file at: {full_path}"
    )
    return None


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def take_screenshot(output_path=None, filename=None):
    """Capture the active editor viewport.

    Args:
        output_path: Directory to save into.  Defaults to
                     ``{Project}/Saved/Arbor/Screenshots/``.
        filename: File name (e.g. ``"shot.jpg"``).  Defaults to a
                  timestamped name.

    Returns:
        Absolute path of the saved screenshot, or ``None`` on failure.
    """
    out_dir = _ensure_screenshot_dir(output_path)
    fname = filename or _default_filename("screenshot")
    full_path = os.path.join(out_dir, fname)

    # Get current viewport camera and use fast capture
    loc, rot = _get_viewport_camera()
    if loc is not None:
        result = _fast_capture(loc, rot, full_path)
        if result:
            write_result({"success": True, "screenshot_path": result})
            return result
        unreal.log_warning("[arbor.capture] Fast capture failed, trying legacy method")

    # Legacy fallback (async — file may not exist immediately)
    legacy_path = full_path.replace(".jpg", ".png")
    result = _take_screenshot_impl(legacy_path)

    if result:
        write_result({"success": True, "screenshot_path": result})
    else:
        write_result({"success": False, "error": "All screenshot methods failed"})

    return result


def take_screenshot_from(location, rotation, output_path=None, filename=None):
    """Move the viewport camera then capture a screenshot.

    Args:
        location: ``(x, y, z)`` or ``unreal.Vector`` — camera position.
        rotation: ``(pitch, yaw, roll)`` or ``unreal.Rotator`` — camera
                  orientation.
        output_path: Directory to save into.
        filename: File name override.

    Returns:
        Absolute path of the saved screenshot, or ``None`` on failure.
    """
    loc = _to_vector(location)
    rot = _to_rotator(rotation)

    out_dir = _ensure_screenshot_dir(output_path)
    fname = filename or _default_filename("from")
    full_path = os.path.join(out_dir, fname)

    # Try fast capture directly at the target position (no viewport move needed)
    result = _fast_capture(loc, rot, full_path)
    if result:
        write_result({"success": True, "screenshot_path": result})
        return result

    unreal.log_warning("[arbor.capture] Fast capture failed, trying legacy method")

    # Legacy fallback: move viewport camera then async capture
    if not _move_viewport_camera(location, rotation):
        unreal.log_error("[arbor.capture] Could not move viewport camera")
        write_result({
            "success": False,
            "error": "Failed to move viewport camera",
        })
        return None

    legacy_path = full_path.replace(".jpg", ".png")
    result = _take_screenshot_impl(legacy_path)

    if result:
        write_result({"success": True, "screenshot_path": result})
    else:
        write_result({"success": False, "error": "All screenshot methods failed"})

    return result


def take_screenshot_top_down(center=(0, 0, 0), height=5000, output_path=None):
    """Bird's-eye view looking straight down at *center*.

    Args:
        center: ``(x, y, z)`` — point to look down at.
        height: How far above *center* to place the camera (cm).
        output_path: Directory to save into.

    Returns:
        Absolute path of the saved screenshot, or ``None`` on failure.
    """
    c = _to_vector(center)
    location = (c.x, c.y, c.z + height)
    rotation = (-90, 0, 0)  # pitch -90 = looking straight down
    return take_screenshot_from(
        location, rotation,
        output_path=output_path,
        filename=_default_filename("top_down"),
    )


def take_screenshot_orbit(target=(0, 0, 0), distance=2000, angle=45,
                          output_path=None):
    """Orbit view around *target* at a given distance and elevation.

    The camera is placed at *distance* from *target*, elevated by *angle*
    degrees, and pointed back at the target.

    Args:
        target: ``(x, y, z)`` — look-at point.
        distance: Distance from target in cm.
        angle: Elevation angle in degrees (0 = level, 90 = directly above).
        output_path: Directory to save into.

    Returns:
        Absolute path of the saved screenshot, or ``None`` on failure.
    """
    t = _to_vector(target)
    angle_rad = math.radians(angle)

    # Camera position: offset along X-axis, elevated by angle
    horizontal = distance * math.cos(angle_rad)
    vertical = distance * math.sin(angle_rad)

    cam_x = t.x + horizontal
    cam_y = t.y
    cam_z = t.z + vertical

    # Camera rotation: look back at target
    # pitch = -angle (look down), yaw = 180 (face back toward target)
    cam_pitch = -angle
    cam_yaw = 180.0

    return take_screenshot_from(
        (cam_x, cam_y, cam_z),
        (cam_pitch, cam_yaw, 0),
        output_path=output_path,
        filename=_default_filename("orbit"),
    )


def show_image(image_path):
    """Open a single image in the in-editor image viewer tab.

    Uses the ``Arbor.ShowImage`` console command to display the image
    in a dockable Slate tab with a dark background.
    Supports JPEG, PNG, and BMP.

    Args:
        image_path: Absolute path to the image file.

    Returns:
        ``True`` if the viewer was opened, ``False`` on error.
    """
    image_path = os.path.abspath(image_path.replace("/", "\\"))

    if not os.path.isfile(image_path):
        unreal.log_error(f"[arbor.capture] show_image: file not found: {image_path}")
        return False

    try:
        unreal.SystemLibrary.execute_console_command(
            None, f'Arbor.ShowImage {image_path}')
        unreal.log(f"[arbor.capture] show_image: opened viewer for {os.path.basename(image_path)}")
        write_result({"success": True, "image_path": image_path})
        return True
    except Exception as e:
        unreal.log_error(f"[arbor.capture] show_image: {e}")
        write_result({"success": False, "error": str(e)})
        return False


def show_last_screenshot():
    """Open the most recent screenshot in a custom viewer window.

    Looks in the default screenshot directory
    (``{Project}/Saved/Arbor/Screenshots/``) and shows the newest file.

    Returns:
        ``True`` if a screenshot was found and the viewer launched,
        ``False`` otherwise.
    """
    out_dir = _ensure_screenshot_dir()
    files = [
        os.path.join(out_dir, f)
        for f in os.listdir(out_dir)
        if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))
    ]
    if not files:
        unreal.log_warning("[arbor.capture] show_last_screenshot: no screenshots found")
        return False

    latest = max(files, key=os.path.getmtime)
    return show_image(latest)
