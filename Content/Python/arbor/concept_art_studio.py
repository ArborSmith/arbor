"""Arbor concept art studio — bridge between MCP and the studio Slate widget.

Provides the file-based IPC layer for the Concept Art Studio workflow:
  - ``open_studio(codex_asset_path)`` — opens with codex context
  - ``set_prompt(prompt)`` — sends Claude's prompt for user review
  - ``get_approval()`` — reads user's approved prompt + settings
  - ``set_results(images)`` — sends generated images
  - ``get_selection()`` — reads user's image selection
"""

import base64
import json
import os
import time

import unreal

from arbor.utils import write_result


STATE_FILENAME = "concept_art_studio_state.json"


def _state_path():
    saved = str(unreal.Paths.project_saved_dir())
    out_dir = os.path.join(saved, "Arbor")
    os.makedirs(out_dir, exist_ok=True)
    return os.path.join(out_dir, STATE_FILENAME)


def _read_state():
    path = _state_path()
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _write_state(state):
    path = _state_path()
    with open(path, "w", encoding="utf-8") as f:
        json.dump(state, f, indent=2, default=str)


def _send_to_widget(state):
    """Push state to the Slate widget via console command."""
    encoded = base64.b64encode(
        json.dumps(state, default=str).encode("utf-8")
    ).decode("ascii")
    unreal.SystemLibrary.execute_console_command(
        None, f"Arbor.ConceptArtStudio {encoded}"
    )


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def open_studio(codex_asset_path):
    """Open the Concept Art Studio pre-filled with codex entry context."""
    # Load codex entry data
    entry_json = unreal.ArborCodexSearch.get_codex_entry(codex_asset_path)
    entry = json.loads(entry_json)

    if not entry.get("success", True):
        result = {"success": False, "error": f"Codex entry not found: {codex_asset_path}"}
        write_result(result)
        return result

    images_json = unreal.ArborCodexImageTools.get_codex_images(codex_asset_path)
    images = json.loads(images_json)

    # Resolve style images from game context
    style_images = []
    game_ctx_path = entry.get("GameContext", "")
    if game_ctx_path:
        try:
            ctx_json = unreal.ArborCodexSearch.get_codex_entry(game_ctx_path)
            ctx = json.loads(ctx_json)
            style_images = ctx.get("StyleImages", [])
        except Exception:
            pass

    # Determine name from entry (varies by category)
    codex_name = (
        entry.get("GameTitle")
        or entry.get("LocationName")
        or entry.get("CharacterName")
        or entry.get("FeatureName")
        or entry.get("PillarName")
        or entry.get("ContextTitle")
        or ""
    )

    state = {
        "step": "context",
        "codex_asset_path": codex_asset_path,
        "codex_category": entry.get("_category", ""),
        "codex_name": codex_name,
        "codex_description": entry.get("Description", ""),
        "style_images": style_images,
        "num_images": 4,
        "prompt": images.get("prompt", ""),
        "user_feedback": "",
        "images": [],
        "selected_index": -1,
        "action": "",
        "timestamp": int(time.time()),
    }
    _write_state(state)
    _send_to_widget(state)

    result = {"success": True, "step": "context", "codex_asset_path": codex_asset_path}
    write_result(result)
    return result


def set_prompt(prompt):
    """Send Claude's generated prompt to the studio for user review."""
    state = _read_state()
    if not state:
        result = {"success": False, "error": "Studio not open"}
        write_result(result)
        return result

    state["prompt"] = prompt
    state["step"] = "prompt_review"
    state["action"] = ""
    state["timestamp"] = int(time.time())
    _write_state(state)
    _send_to_widget(state)

    result = {"success": True, "step": "prompt_review"}
    write_result(result)
    return result


def get_approval():
    """Poll for user's prompt approval. Returns state with action field."""
    state = _read_state()
    if not state:
        result = {"success": False, "error": "Studio not open"}
        write_result(result)
        return result

    result = {
        "success": True,
        "step": state["step"],
        "action": state.get("action", ""),
        "prompt": state.get("prompt", ""),
        "num_images": state.get("num_images", 4),
        "style_images": state.get("style_images", []),
        "user_feedback": state.get("user_feedback", ""),
    }
    write_result(result)
    return result


def set_results(images):
    """Send generated images to the studio for review."""
    state = _read_state()
    if not state:
        result = {"success": False, "error": "Studio not open"}
        write_result(result)
        return result

    if isinstance(images, str):
        images = json.loads(images)

    image_list = images if isinstance(images, list) else images.get("images", [])

    state["images"] = image_list
    state["step"] = "results"
    state["action"] = ""
    state["selected_index"] = -1
    state["timestamp"] = int(time.time())
    _write_state(state)
    _send_to_widget(state)

    result = {"success": True, "step": "results", "image_count": len(image_list)}
    write_result(result)
    return result


def get_selection():
    """Poll for user's image selection."""
    state = _read_state()
    if not state:
        result = {"success": False, "error": "Studio not open"}
        write_result(result)
        return result

    result = {
        "success": True,
        "step": state["step"],
        "action": state.get("action", ""),
        "selected_index": state.get("selected_index", -1),
        "user_feedback": state.get("user_feedback", ""),
    }

    idx = state.get("selected_index", -1)
    imgs = state.get("images", [])
    if state.get("action") == "select" and 0 <= idx < len(imgs):
        result["selected_image"] = imgs[idx]

    write_result(result)
    return result


def get_state():
    """Read the full studio state."""
    state = _read_state()
    if not state:
        result = {"success": False, "error": "Studio not open"}
        write_result(result)
        return result

    write_result(state)
    return state
