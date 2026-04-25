"""Arbor fab — Access Fab.com auth and import via the Fab plugin's internal APIs.

Uses UFabHelper (C++ reflection wrapper) to call the Fab plugin's private
UFabBrowserApi methods without any compile-time dependency.

Typical usage from ue5_run_python::

    import arbor.fab
    result = arbor.fab.get_auth_token()
    # result = {"token": "eyJ...", "logged_in": True}

    arbor.fab.import_asset(
        download_url="https://...",
        asset_id="abc-123",
        asset_name="LavaTexture",
        asset_type="fbx",
    )

    # Poll import status:
    status = arbor.fab.get_import_status("abc-123")
    # status = {"asset_id": "abc-123", "status": "in_progress", ...}

    # Or wait for completion (tick-driven async):
    arbor.fab.import_asset(..., wait_for_completion=True)
"""

import json
import os
import time

import unreal

from arbor.utils import write_result

# ---------------------------------------------------------------------------
# Module-level state for tick-driven import tracking
# ---------------------------------------------------------------------------

_import_tracker = {}       # asset_id -> tracking state dict
_tracker_callback = None   # shared tick callback for all tracked imports

_SETTLE_SECONDS = 5.0      # no new assets for this long -> complete
_TIMEOUT_SECONDS = 300.0   # 5 min with zero activity -> failed
_POLL_INTERVAL = 1.0       # check counter every 1 second


# ---------------------------------------------------------------------------
# Private helpers — tick-driven async (used by import tracking)
# ---------------------------------------------------------------------------

def _capture_result_path():
    """Capture the current UE5_BRIDGE_RESULT_PATH env var.

    For tick-driven async functions, the bridge wrapper clears this env var
    in its finally block after the Python call returns.  We must capture
    it at call time so the tick callback can write directly to it later.
    """
    return os.environ.get("UE5_BRIDGE_RESULT_PATH")


def _write_result_to(data, path):
    """Write *data* as JSON to a specific file path.

    Used by tick callbacks that captured the result path at call time.
    Falls back to write_result() if path is None.
    """
    if path is None:
        write_result(data)
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, default=str)
        unreal.log(f"[arbor.fab] Wrote result to {path}")
    except Exception as e:
        unreal.log_error(f"[arbor.fab] Failed to write result to {path}: {e}")
        write_result(data)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def get_auth_token():
    """Get the EOS Bearer token from the Fab plugin.

    The user must be logged into Fab via the UE5 Editor's Fab window
    (Content Drawer > Fab button, or Window > Fab).

    Returns:
        dict: {"token": str, "logged_in": bool}
              If not logged in, includes "error" with instructions.
    """
    try:
        token = str(unreal.FabHelper.get_fab_auth_token())
    except Exception as e:
        unreal.log_error(f"[arbor.fab] get_auth_token: {e}")
        result = {"token": "", "logged_in": False, "error": str(e)}
        write_result(result)
        return result

    if token:
        result = {"token": token, "logged_in": True}
        write_result(result)
        return result

    result = {
        "token": "",
        "logged_in": False,
        "error": ("Not logged into Fab in UE5. "
                  "Open the Fab window in the UE5 Editor "
                  "(Content Drawer > Fab button, or Window > Fab) "
                  "and sign in with your Epic account, then retry."),
    }
    write_result(result)
    return result


def is_logged_in():
    """Check if user is logged into Fab.

    Returns:
        bool: True if a valid auth token exists.
    """
    try:
        return unreal.FabHelper.is_fab_logged_in()
    except Exception as e:
        unreal.log_error(f"[arbor.fab] is_logged_in: {e}")
        return False


def check_fab_login():
    """Check Fab login status and return actionable instructions if not logged in.

    Designed for Claude to read and relay to the user before Fab import operations.

    Returns:
        dict: {"logged_in": bool, "message": str}
              If not logged in, includes "instructions" with step-by-step guide.
    """
    logged_in = is_logged_in()
    if logged_in:
        result = {"logged_in": True, "message": "Fab session active in UE5."}
    else:
        result = {
            "logged_in": False,
            "message": "Not logged into Fab in UE5.",
            "instructions": (
                "Please open the Fab window in the UE5 Editor "
                "(Content Drawer > Fab button, or Window > Fab) "
                "and sign in with your Epic account. "
                "Once signed in, retry the operation."
            ),
        }
    write_result(result)
    return result


# ---------------------------------------------------------------------------
# Private helpers — import tracking
# ---------------------------------------------------------------------------

def _start_tracking(asset_id, asset_name, result_path=None):
    """Begin tracking a Fab import by asset_id."""
    global _tracker_callback

    now = time.time()

    # Snapshot the current counter
    try:
        counter = int(unreal.ArborRegistryHelper.get_asset_change_counter())
    except Exception:
        counter = 0

    # Drain stale recently-added paths so they don't pollute this import
    try:
        unreal.ArborRegistryHelper.get_recently_added_assets()
    except Exception:
        pass

    _import_tracker[asset_id] = {
        "asset_id": asset_id,
        "asset_name": asset_name,
        "status": "pending",
        "started_at": now,
        "last_activity_at": None,
        "last_counter": counter,
        "assets_added": [],
        "result_path": result_path,
        "last_check": now,
    }

    # Register tick callback if not already running
    if _tracker_callback is None:
        _tracker_callback = unreal.register_slate_pre_tick_callback(_tracker_tick)
        unreal.log("[arbor.fab] Import tracker tick registered")

    unreal.log(f"[arbor.fab] Tracking import: {asset_name} ({asset_id})")


def _tracker_tick(delta_time):
    """Tick callback that monitors asset registry for import completion."""
    global _tracker_callback

    if not _import_tracker:
        _stop_tracker()
        return

    now = time.time()

    # Find active imports and check if any are due for a poll
    active = [s for s in _import_tracker.values()
              if s["status"] in ("pending", "in_progress")]
    if not active:
        _stop_tracker()
        return

    # Throttle: only poll every _POLL_INTERVAL
    earliest_next = min(s["last_check"] + _POLL_INTERVAL for s in active)
    if now < earliest_next:
        return

    # Read current counter and drain recently added paths
    try:
        current_counter = int(unreal.ArborRegistryHelper.get_asset_change_counter())
    except Exception:
        current_counter = 0

    recent_paths = []
    try:
        recent_json = str(unreal.ArborRegistryHelper.get_recently_added_assets())
        if recent_json:
            recent_paths = json.loads(recent_json)
    except Exception:
        pass

    for state in active:
        state["last_check"] = now

        # Detect new asset activity
        if current_counter > state["last_counter"]:
            state["last_counter"] = current_counter
            state["last_activity_at"] = now
            state["assets_added"].extend(recent_paths)

            if state["status"] == "pending":
                state["status"] = "in_progress"
                unreal.log(f"[arbor.fab] Import '{state['asset_name']}' -> in_progress "
                           f"({len(recent_paths)} new assets)")

        # Check for settle (in_progress -> complete)
        if (state["status"] == "in_progress"
                and state["last_activity_at"] is not None
                and now - state["last_activity_at"] >= _SETTLE_SECONDS):
            state["status"] = "complete"
            elapsed = round(now - state["started_at"], 1)
            unreal.log(f"[arbor.fab] Import '{state['asset_name']}' complete "
                       f"({len(state['assets_added'])} assets, {elapsed}s)")
            _maybe_write_async_result(state)

        # Check for timeout (pending -> failed)
        if (state["status"] == "pending"
                and now - state["started_at"] >= _TIMEOUT_SECONDS):
            state["status"] = "failed"
            unreal.log_warning(f"[arbor.fab] Import '{state['asset_name']}' "
                               f"timed out after {_TIMEOUT_SECONDS}s")
            _maybe_write_async_result(state)


def _stop_tracker():
    """Unregister the tracker tick callback."""
    global _tracker_callback
    if _tracker_callback is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(_tracker_callback)
        except Exception:
            pass
        _tracker_callback = None
        unreal.log("[arbor.fab] Import tracker tick unregistered")


def _maybe_write_async_result(state):
    """If this tracked import has a captured result_path, write the final result."""
    result_path = state.get("result_path")
    if result_path is None:
        return
    _write_result_to(_format_status(state), result_path)


def _format_status(state):
    """Format a tracking state dict for external consumption."""
    now = time.time()
    elapsed = round(now - state["started_at"], 1)
    return {
        "asset_id": state["asset_id"],
        "asset_name": state["asset_name"],
        "status": state["status"],
        "elapsed_seconds": elapsed,
        "assets_added_count": len(state["assets_added"]),
        "assets_added": state["assets_added"][:20],
    }


# ---------------------------------------------------------------------------
# Public API — import
# ---------------------------------------------------------------------------

def import_asset(download_url, asset_id, asset_name, asset_type="fbx",
                 listing_type="3d-model", is_quixel=False,
                 asset_namespace="", distribution_urls=None,
                 wait_for_completion=False):
    """Trigger the Fab plugin's native import workflow.

    This calls UFabBrowserApi::AddToProject via reflection, which handles
    downloading and importing the asset using the appropriate workflow
    (Pack, Quixel, or Generic/Interchange).

    The import is automatically tracked.  Poll status with
    ``get_import_status(asset_id)``.

    Args:
        download_url: Signed download URL for the asset.
        asset_id: Fab listing/asset UUID.
        asset_name: Display name for the asset.
        asset_type: Format type — "fbx", "gltf", "glb", or "unreal-engine".
        listing_type: Fab listing type — "3d-model", "material", etc.
        is_quixel: Whether this is a Quixel/Megascans asset.
        asset_namespace: Optional namespace string.
        distribution_urls: List of distribution point base URLs (for UE asset packs).
        wait_for_completion: If True, uses tick-driven async to write the
            final result when the import settles.  The bridge polls for the
            result file automatically.  Default False (fire-and-forget).

    Returns:
        dict: {"success": bool, "message": str, "asset_id": str, "tracking": bool}
    """
    try:
        metadata = {
            "download_url": download_url,
            "asset_id": asset_id,
            "asset_name": asset_name,
            "asset_type": asset_type,
            "listing_type": listing_type,
            "is_quixel": is_quixel,
            "asset_namespace": asset_namespace,
            "distribution_urls": distribution_urls or [],
        }
        success = bool(unreal.FabHelper.fab_import_asset(json.dumps(metadata)))

        if not success:
            result = {
                "success": False,
                "message": f"Failed to dispatch import for '{asset_name}'",
                "asset_id": asset_id,
            }
            write_result(result)
            return result

        # Start tracking regardless of wait_for_completion
        result_path = _capture_result_path() if wait_for_completion else None
        _start_tracking(asset_id, asset_name, result_path=result_path)

        if wait_for_completion:
            # Tick-driven async: do NOT call write_result() — tick callback handles it
            return {"success": True,
                    "message": f"Import tracking started for '{asset_name}'",
                    "asset_id": asset_id, "tracking": True}

        # Default: fire-and-forget (backward compatible)
        result = {
            "success": True,
            "message": f"Import dispatched for '{asset_name}'",
            "asset_id": asset_id,
            "tracking": True,
        }
        write_result(result)
        return result

    except Exception as e:
        unreal.log_error(f"[arbor.fab] import_asset: {e}")
        result = {"success": False, "message": str(e), "asset_id": asset_id}
        write_result(result)
        return result


def get_import_status(asset_id=None):
    """Get the status of one or all tracked Fab imports.

    Args:
        asset_id: Specific asset ID to query.  If None, returns all.

    Returns:
        dict: Status dict with ``status`` (pending/in_progress/complete/failed),
              ``asset_name``, ``elapsed_seconds``, ``assets_added_count``, and
              up to 20 ``assets_added`` content paths.
              If *asset_id* is not tracked, returns ``{"status": "unknown"}``.
    """
    if asset_id is not None:
        state = _import_tracker.get(asset_id)
        if state is None:
            result = {"status": "unknown", "asset_id": asset_id}
        else:
            result = _format_status(state)
        write_result(result)
        return result

    # Return all tracked imports
    result = {aid: _format_status(s) for aid, s in _import_tracker.items()}
    write_result(result)
    return result


def list_api_functions():
    """List all available UFunction names on UFabBrowserApi.

    Useful for runtime discovery of what methods the Fab plugin exposes.
    Results can guide which methods to call for library, search, etc.

    Returns:
        list[str]: Function names available on the Fab browser API class.
    """
    try:
        raw = str(unreal.FabHelper.list_fab_api_functions())
        functions = json.loads(raw) if raw else []
    except Exception as e:
        unreal.log_error(f"[arbor.fab] list_api_functions: {e}")
        functions = []
    write_result({"functions": functions, "count": len(functions)})
    return functions


def fab_library(page=1, per_page=20):
    """Retrieve the user's Fab library (owned/claimed assets).

    Tries the Fab plugin's internal API via C++ reflection first.  If no
    suitable method is found on UFabBrowserApi, falls back to an HTTP
    request to the Fab web API using the EOS Bearer token.

    Args:
        page: Page number for pagination (1-based).
        per_page: Number of results per page (max 100).

    Returns:
        dict: Library data with ``success`` flag, ``items`` list, and
              pagination info.  On error, includes ``error`` message.
    """
    if not is_logged_in():
        result = {
            "success": False,
            "error": "Not logged into Fab.",
            "instructions": (
                "Please open the Fab window in the UE5 Editor "
                "(Content Drawer > Fab button, or Window > Fab) "
                "and sign in with your Epic account, then retry."
            ),
        }
        write_result(result)
        return result

    per_page = max(1, min(per_page, 100))
    page = max(1, page)

    # --- Attempt 1: C++ reflection on UFabBrowserApi ---
    try:
        params = json.dumps({"page": page, "per_page": per_page})
        raw = str(unreal.FabHelper.get_fab_library(params))
        if raw:
            data = json.loads(raw)
            # If C++ found and called a method successfully, return data
            if data.get("success") is not False:
                result = {"success": True, "source": "fab_plugin", "data": data}
                write_result(result)
                return result
            # C++ couldn't find a library method — fall through to HTTP
            unreal.log(f"[arbor.fab] C++ reflection: {data.get('error', 'unknown')}")
    except Exception as e:
        unreal.log(f"[arbor.fab] C++ reflection attempt: {e}")

    # --- Attempt 2: HTTP with EOS Bearer token ---
    return _fab_library_http(page, per_page)


def _fab_library_http(page, per_page):
    """Fetch the user's Fab library via HTTP using the EOS Bearer token."""
    import urllib.request
    import urllib.error

    try:
        token = str(unreal.FabHelper.get_fab_auth_token())
    except Exception as e:
        result = {"success": False, "error": f"Failed to get auth token: {e}"}
        write_result(result)
        return result

    if not token:
        result = {"success": False, "error": "Auth token is empty."}
        write_result(result)
        return result

    # Endpoints to try, in order of likelihood
    endpoints = [
        f"https://www.fab.com/i/users/me/library?page={page}&page_size={per_page}",
        f"https://www.fab.com/i/users/me/acquisitions?page={page}&page_size={per_page}",
        f"https://www.fab.com/i/users/me/owned-listings?page={page}&page_size={per_page}",
    ]

    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
        "X-Requested-With": "XMLHttpRequest",
        "User-Agent": "Arbor-UE5-Plugin/1.0",
    }

    last_error = None
    for url in endpoints:
        try:
            req = urllib.request.Request(url, headers=headers, method="GET")
            with urllib.request.urlopen(req, timeout=15) as resp:
                body = resp.read().decode("utf-8")
                data = json.loads(body)
                result = {
                    "success": True,
                    "source": "fab_api",
                    "endpoint": url.split("?")[0].split("/")[-1],
                    "page": page,
                    "per_page": per_page,
                    "data": data,
                }
                write_result(result)
                return result
        except urllib.error.HTTPError as e:
            last_error = f"{url}: HTTP {e.code}"
            unreal.log(f"[arbor.fab] HTTP attempt failed: {last_error}")
            if e.code == 401 or e.code == 403:
                # Auth rejected — no point trying other endpoints
                break
            continue
        except Exception as e:
            last_error = f"{url}: {e}"
            unreal.log(f"[arbor.fab] HTTP attempt failed: {last_error}")
            continue

    # All attempts failed
    result = {
        "success": False,
        "error": (
            "Could not retrieve Fab library. Neither the Fab plugin API "
            "nor the Fab web API returned data."
        ),
        "last_error": last_error,
        "hint": (
            "Use arbor.fab.list_api_functions() to discover available "
            "methods on the Fab plugin. The Fab web API may require "
            "browser-session cookies rather than EOS Bearer tokens."
        ),
    }
    write_result(result)
    return result


def clear_import_tracking(asset_id=None):
    """Clear completed/failed tracking entries.

    Args:
        asset_id: Specific asset ID to clear.  If None, clears all terminal entries.

    Returns:
        dict: {"cleared": int}
    """
    if asset_id is not None:
        if asset_id in _import_tracker:
            del _import_tracker[asset_id]
            result = {"cleared": 1}
        else:
            result = {"cleared": 0}
    else:
        terminal = [aid for aid, s in _import_tracker.items()
                    if s["status"] in ("complete", "failed")]
        for aid in terminal:
            del _import_tracker[aid]
        result = {"cleared": len(terminal)}

    write_result(result)
    return result
