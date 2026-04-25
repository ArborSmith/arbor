"""Arbor variations — AI text variation review for GameCodex.

Bridges the MCP-generated text variations with the UE5 editor:
  - ``show_text_variations(variations_json)`` — opens the review window
  - ``get_text_variation_result()`` — reads the user's selection
"""

import base64
import json
import os

import unreal

from arbor.utils import write_result


# ---------------------------------------------------------------------------
# Review window bridge
# ---------------------------------------------------------------------------

def show_text_variations(variations_json):
    """Open the Arbor Text Variation Review editor window with generated variations.

    Args:
        variations_json: JSON string (or dict) describing variations to display.
            Format::

                {
                    "variations": [
                        {
                            "label": "Variation A",
                            "fields": {"FieldName": "value", ...}
                        },
                        ...
                    ],
                    "category": "context",
                    "asset_path": "/Game/GameCodex/...",
                    "prompt": "original prompt text",
                    "locked_fields": ["FieldName"],
                    "field_order": ["Field1", "Field2", ...]
                }
    """
    # Normalize input to a dict
    if isinstance(variations_json, str):
        variations_json = json.loads(variations_json)

    if not isinstance(variations_json, dict):
        raise TypeError(
            f"show_text_variations: expected dict or str, got {type(variations_json).__name__}. "
            f"Format: {{\"variations\": [{{\"label\": ..., \"fields\": {{...}}}}]}}"
        )

    if "variations" not in variations_json:
        raise ValueError(
            "show_text_variations: missing 'variations' key. "
            "Format: {\"variations\": [{\"label\": ..., \"fields\": {...}}]}"
        )

    if not variations_json["variations"]:
        raise ValueError("show_text_variations: 'variations' array is empty")

    encoded = base64.b64encode(json.dumps(variations_json).encode("utf-8")).decode("ascii")
    unreal.SystemLibrary.execute_console_command(
        None, f"Arbor.TextVariation {encoded}"
    )
    unreal.log("[arbor.variations] Opened text variation review window")


def get_text_variation_result():
    """Read the user's selection from the text variation review window.

    Writes the result via ``write_result()`` for the MCP bridge to read.
    Returns ``{"status": "pending"}`` if no selection has been made yet.

    Returns:
        The result dict (also written to ``Saved/Arbor/last_result.json``).
    """
    project_dir = unreal.Paths.project_dir()
    result_path = os.path.join(project_dir, "Saved", "Arbor",
                               "text_variation_result.json")

    if not os.path.exists(result_path):
        result = {"status": "pending"}
        write_result(result)
        return result

    with open(result_path, "r", encoding="utf-8") as f:
        result = json.load(f)

    # Clean up after reading so we don't read stale results
    os.remove(result_path)

    write_result(result)
    return result
