"""Extract a UMaterialFunction asset into a `pattern` catalog entry.

A pattern entry references a Material Function by `mf_path` rather than carrying
an inline BuildMaterial spec. To use one, a material wires a
MaterialExpressionMaterialFunctionCall at `mf_path` and connects its outputs;
the function is composed, not inlined.

Calls arbor.materials.query_material_function() to read the function's
inputs/outputs, then writes a YAML stub for hand-tagging.

Usage (from UE5 Python or ue5_run_python):

    from extraction import extract_function
    extract_function.extract(
        "/Game/Assets/Materials/Functions/Procedural/MF_SDF_Circle")
"""

import os

try:
    import yaml
except ImportError:
    raise SystemExit("PyYAML is required. Install via: pip install pyyaml")

import arbor.materials as materials

from .extract_material import asset_name_from_path, _SLUG_RE


def slugify_function(asset_name: str) -> str:
    """Convert MF_SDF_Circle -> sdf_circle (strips the MF_ prefix)."""
    name = asset_name
    if name.startswith("MF_"):
        name = name[3:]
    name = _SLUG_RE.sub("_", name.lower()).strip("_")
    return name or "function"


def extract(mf_path: str, out_dir: str | None = None, provenance: str = "") -> dict:
    """Extract one Material Function to a `pattern` YAML entry.

    If `out_dir` is None, uses the resolved catalog's entries/ directory.
    Returns a dict with success + path + entry_id.
    """
    if out_dir is None:
        from . import _paths
        out_dir = _paths.entries_dir()

    query = materials.query_material_function(mf_path)
    if not query.get("success"):
        return {"success": False, "error": query.get("error", "query failed"),
                "mf_path": mf_path}

    asset_name = asset_name_from_path(mf_path)
    entry_id = slugify_function(asset_name)

    # Strip the verbose per-node `properties`/`connections` from the IO summary:
    # a pattern entry only needs the public interface (names, types) so the skill
    # can compose it. The full graph lives in the .uasset (and is rebuildable via
    # build_material_function from a hand-authored spec).
    inputs = [{"name": i.get("name", ""), "type": i.get("type", "")}
              for i in (query.get("inputs") or [])]
    outputs = [{"name": o.get("name", "")} for o in (query.get("outputs") or [])]

    entry = {
        "id": entry_id,
        "type": "pattern",
        "mf_path": mf_path,
        "status": "needs_review",
        "material_system": "any",   # pure-math primitives are system-agnostic
        "tags": [],
        "visual_traits": [],
        "description": query.get("description", "") or "",
        "inputs": inputs,
        "outputs": outputs,
        "library_categories": query.get("library_categories") or [],
        "provenance": provenance,
        "quality": {
            "compile": "unknown",
            "roundtrip": "unknown",
        },
    }

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{entry_id}.yaml")
    # Preserve hand-tagged fields from any pre-existing entry so re-extracting a
    # function doesn't wipe curated metadata. Only the interface is re-read.
    if os.path.exists(out_path):
        try:
            with open(out_path, "r", encoding="utf-8") as f:
                prior = yaml.safe_load(f) or {}
            for k in ("status", "tags", "visual_traits", "description"):
                if prior.get(k):
                    entry[k] = prior[k]
        except Exception:
            pass

    with open(out_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)

    # Refresh the index so the Slate widget picks up the new pattern entry.
    try:
        from . import _index
        _index.refresh_index(out_dir)
    except Exception:
        pass

    return {"success": True, "yaml_path": out_path, "entry_id": entry_id,
            "input_count": len(inputs), "output_count": len(outputs)}
