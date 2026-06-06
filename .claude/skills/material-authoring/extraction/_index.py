"""Catalog index — fast JSON view of all YAML entries.

The Slate widget in UE5 reads `catalog/_index.json` to populate its grid
without parsing 50+ YAML files. The index is regenerated automatically
after extract / validate / save operations.

Schema:
{
  "version": 1,
  "generated_at": "<ISO8601>",
  "entries": [
    {
      "id": "concrete",
      "yaml_path": "concrete.yaml",
      "source": "/Game/.../M_Concrete",
      "status": "ok" | "needs_review" | "bad" | "deprecated",
      "tags": [...],
      "visual_traits": [...],
      "description": "...",
      "expression_count": 5,
      "connection_count": 2,
      "output_count": 4,
      "shading_model": "MSM_DefaultLit",
      "blend_mode": "Opaque",
      "thumbnail_path": "thumbnails/concrete.png",  // relative; may not exist
      "quality": {"roundtrip": "ok", "compile": "unknown"}
    }
  ]
}
"""

import datetime
import json
import os

try:
    import yaml
except ImportError:
    raise SystemExit("PyYAML required - install via: pip install pyyaml")


INDEX_FILENAME = "_index.json"


def catalog_root_from_entries_dir(entries_dir: str) -> str:
    """Given .../catalog/entries/, return .../catalog/."""
    return os.path.dirname(os.path.abspath(entries_dir))


def index_path_for_entries_dir(entries_dir: str) -> str:
    return os.path.join(catalog_root_from_entries_dir(entries_dir), INDEX_FILENAME)


def _resolve_entries_dir(entries_dir: str | None) -> str:
    """Allow callers to pass either an entries_dir or use the path resolver."""
    if entries_dir:
        return entries_dir
    from . import _paths
    return _paths.entries_dir()


def entry_to_index_record(entry: dict, yaml_filename: str) -> dict:
    """Project a full YAML entry down to the index's flat record."""
    spec = entry.get("spec") or {}
    flags = (spec.get("flags") or {})
    expr_count = len(spec.get("expressions") or [])
    conn_count = len(spec.get("connections") or [])
    output_count = len(spec.get("outputs") or [])
    eid = entry.get("id") or os.path.splitext(yaml_filename)[0]
    thumb_rel = os.path.join("thumbnails", f"{eid}.png").replace("\\", "/")
    return {
        "id": eid,
        "yaml_path": yaml_filename,
        # type: "reference_material" (a UMaterial in `source`) or "pattern"
        # (a UMaterialFunction in `mf_path`). Missing -> reference_material.
        "type": entry.get("type", "reference_material"),
        "source": entry.get("source", ""),
        "mf_path": entry.get("mf_path", ""),
        "status": entry.get("status", "ok"),
        "tags": entry.get("tags") or [],
        "visual_traits": entry.get("visual_traits") or [],
        "description": entry.get("description", ""),
        "proposed_tags": entry.get("proposed_tags") or [],
        "proposed_visual_traits": entry.get("proposed_visual_traits") or [],
        "proposed_description": entry.get("proposed_description", ""),
        "proposed_mi_compatible": entry.get("proposed_mi_compatible", False),
        "expression_count": expr_count,
        "connection_count": conn_count,
        "output_count": output_count,
        "shading_model": entry.get("shading_model") or flags.get("shading_model", ""),
        "blend_mode": flags.get("blend_mode", ""),
        "thumbnail_path": thumb_rel,
        "thumbnail_issue": entry.get("thumbnail_issue", ""),
        "quality": entry.get("quality") or {},
    }


def _asset_resolves(asset_path: str) -> bool:
    """Best-effort check that an asset path resolves. Only callable inside UE.

    Outside UE (CLI tooling), we have no way to know if /Game/X exists, so
    we assume it does and let runtime fail loudly if not. UE-side this is a
    fast asset-registry lookup, no asset load involved.
    """
    if not asset_path:
        return False
    try:
        import unreal  # type: ignore
    except ImportError:
        return True  # can't check; assume ok
    try:
        return bool(unreal.EditorAssetLibrary.does_asset_exist(asset_path))
    except Exception:
        return True  # any failure here is non-fatal; don't flip statuses


def refresh_index(entries_dir: str | None = None,
                  detect_broken_sources: bool = True) -> dict:
    """Scan every YAML in entries_dir, write _index.json, return the index dict.

    If `detect_broken_sources` is True and we're inside UE, entries whose
    `source` path no longer resolves get their status flipped to "broken"
    (preserving prior status by saving it as `prior_status`). Reversed when
    the source is restored.
    """
    entries_dir = _resolve_entries_dir(entries_dir)
    entries = []
    broken_flipped = []
    if os.path.isdir(entries_dir):
        for fname in sorted(os.listdir(entries_dir)):
            if not fname.endswith(".yaml"):
                continue
            path = os.path.join(entries_dir, fname)
            try:
                with open(path, "r", encoding="utf-8") as f:
                    entry = yaml.safe_load(f)
            except Exception:
                continue
            if not entry:
                continue

            if detect_broken_sources:
                # Pattern entries point at a Material Function (mf_path); all
                # other entries at a Material (source). Check whichever applies.
                if entry.get("type") == "pattern":
                    src = entry.get("mf_path", "")
                else:
                    src = entry.get("source", "")
                ok = _asset_resolves(src) if src else True
                current = entry.get("status", "ok")
                changed = False
                if not ok and current != "broken":
                    # Flip to broken, remember what it was before
                    entry["prior_status"] = current
                    entry["status"] = "broken"
                    broken_flipped.append({"id": entry.get("id"), "was": current})
                    changed = True
                elif ok and current == "broken":
                    # Source came back; restore prior status
                    entry["status"] = entry.pop("prior_status", "ok")
                    broken_flipped.append({"id": entry.get("id"), "restored_to": entry["status"]})
                    changed = True
                if changed:
                    with open(path, "w", encoding="utf-8") as f:
                        yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)

            entries.append(entry_to_index_record(entry, fname))

    index = {
        "version": 1,
        "generated_at": datetime.datetime.utcnow().isoformat(timespec="seconds") + "Z",
        "entries": entries,
        "broken_changes": broken_flipped,
    }
    out_path = index_path_for_entries_dir(entries_dir)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(index, f, indent=2)
    return index


def read_index(entries_dir: str | None = None) -> dict:
    entries_dir = _resolve_entries_dir(entries_dir)
    path = index_path_for_entries_dir(entries_dir)
    if not os.path.exists(path):
        return refresh_index(entries_dir)
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)
