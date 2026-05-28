"""Dispatch table for catalog operations called from the Slate widget.

The widget builds a JSON command, the C++ side runs:
    from extraction import dispatch; dispatch.run('<json>')
and reads the result back from arbor's standard write_result location.

Operations:
    update       - merge JSON updates into a catalog YAML
    set_status   - flip status to ok/needs_review/bad/deprecated
    accept_proposals - promote proposed_* fields to primary, clear proposals
    reject_proposals - clear proposed_* fields without promoting
    refresh_index - rescan entries/ and rewrite _index.json
    delete       - remove a catalog YAML + thumbnail
"""

import json
import os

try:
    import yaml
except ImportError:
    raise SystemExit("PyYAML required")

from . import _paths, _index


def _load(yaml_path):
    with open(yaml_path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def _save(yaml_path, entry):
    with open(yaml_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)


def _resolve_yaml(args):
    """Args may carry either yaml_path (absolute or relative to entries_dir)
    or entry_id. Return absolute path."""
    yp = args.get("yaml_path")
    if yp:
        if os.path.isabs(yp):
            return yp
        return os.path.join(_paths.entries_dir(), yp)
    eid = args.get("id")
    if eid:
        return os.path.join(_paths.entries_dir(), f"{eid}.yaml")
    raise ValueError("dispatch: missing yaml_path or id")


# -- ops --

def update(args):
    yp = _resolve_yaml(args)
    if not os.path.exists(yp):
        return {"success": False, "error": f"entry not found: {yp}"}
    entry = _load(yp)
    for k, v in (args.get("fields") or {}).items():
        entry[k] = v
    _save(yp, entry)
    _index.refresh_index()
    return {"success": True, "yaml_path": yp}


def set_status(args):
    return update({"yaml_path": args.get("yaml_path"), "id": args.get("id"),
                   "fields": {"status": args["status"]}})


def accept_proposals(args):
    yp = _resolve_yaml(args)
    entry = _load(yp)
    moved = []
    pairs = [("proposed_tags", "tags"),
             ("proposed_visual_traits", "visual_traits"),
             ("proposed_description", "description"),
             ("proposed_mi_compatible", "mi_compatible")]
    for src, dst in pairs:
        if src in entry:
            entry[dst] = entry.pop(src)
            moved.append(dst)
    # After accepting, default status flips back to ok unless the user pinned it
    if entry.get("status") == "needs_review":
        entry["status"] = "ok"
    _save(yp, entry)
    _index.refresh_index()
    return {"success": True, "yaml_path": yp, "moved_fields": moved}


def reject_proposals(args):
    yp = _resolve_yaml(args)
    entry = _load(yp)
    cleared = []
    for k in ("proposed_tags", "proposed_visual_traits",
              "proposed_description", "proposed_mi_compatible"):
        if k in entry:
            del entry[k]
            cleared.append(k)
    _save(yp, entry)
    _index.refresh_index()
    return {"success": True, "yaml_path": yp, "cleared": cleared}


def delete(args):
    yp = _resolve_yaml(args)
    if not os.path.exists(yp):
        return {"success": False, "error": "entry not found"}
    entry = _load(yp)
    eid = entry.get("id") or os.path.splitext(os.path.basename(yp))[0]
    os.remove(yp)
    # Also delete the thumbnail if present
    thumb = os.path.join(_paths.thumbnails_dir(), f"{eid}.png")
    if os.path.exists(thumb):
        os.remove(thumb)
    _index.refresh_index()
    return {"success": True, "deleted": eid}


def refresh_index_op(args):
    idx = _index.refresh_index()
    return {"success": True, "entries": len(idx.get("entries", []))}


def scan_texture_suggestions(args):
    """Walk every MaterialInstanceConstant in /Game; for each TextureParameter
    override, accumulate (param_name -> {texture_path -> usage_count}).

    Caches the result at <catalog_root>/_texture_suggestions.json so the Slate
    widget can read it instantly. Invoke on demand via a "Rescan Suggestions"
    button - it takes ~30s on a project with ~1300 MICs.
    """
    import unreal  # type: ignore
    from collections import Counter

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    filter = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
        package_paths=["/Game"], recursive_paths=True,
    )
    mics = ar.get_assets(filter)

    suggestions = {}
    scanned = 0
    for mic_data in mics:
        mic = unreal.EditorAssetLibrary.load_asset(str(mic_data.package_name))
        if not mic:
            continue
        scanned += 1
        tv = mic.get_editor_property("texture_parameter_values") or []
        for v in tv:
            info = v.get_editor_property("parameter_info")
            pname = str(info.get_editor_property("name"))
            tex = v.get_editor_property("parameter_value")
            if not tex or not pname:
                continue
            tex_path = tex.get_path_name().split(".")[0]
            counter = suggestions.setdefault(pname, Counter())
            counter[tex_path] += 1

    # Sort each param's candidates by usage descending, dedupe, then keep the
    # top 50 (more is rarely useful in a picker).
    out = {}
    for pname, counter in suggestions.items():
        ranked = sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))[:50]
        out[pname] = [{"path": p, "count": c} for p, c in ranked]

    cache_path = os.path.join(_paths.resolve_catalog_root(), "_texture_suggestions.json")
    payload = {
        "version": 1,
        "scanned_mics": scanned,
        "suggestions": out,
    }
    with open(cache_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    return {
        "success": True,
        "scanned_mics": scanned,
        "param_count": len(out),
        "cache_path": cache_path,
    }


def retag_from_preview(args):
    """Render the entry's thumbnail using the user's texture-picker overrides,
    then re-run AI auto-tag so the proposed tags follow the new visual.

    Pipeline:
      1. Create a temp MIC at /Game/_ArborSandbox/_retag/<id> parented to the
         entry's source, with the override textures applied.
      2. Render it to the catalog's thumbnail path (overwrites the cached one).
      3. Invoke the ai_autotag script on this entry.
      4. Delete the temp MIC.
    """
    import subprocess
    import shutil
    import unreal  # type: ignore
    import arbor.materials as mat

    yp = _resolve_yaml(args)
    if not os.path.exists(yp):
        return {"success": False, "error": "entry not found"}
    entry = _load(yp)
    eid = entry.get("id") or os.path.splitext(os.path.basename(yp))[0]
    src = entry.get("source", "")
    if not src:
        return {"success": False, "error": "entry has no source"}

    overrides = args.get("texture_overrides") or {}
    temp_path = f"/Game/_ArborSandbox/_retag/MI_retag_{eid}"

    # Wipe any prior temp from a previous run so CreateAsset doesn't refuse.
    if unreal.EditorAssetLibrary.does_asset_exist(temp_path):
        unreal.EditorAssetLibrary.delete_asset(temp_path)

    # Build the temp MIC with the texture overrides
    create_params = {k: v for k, v in overrides.items()}
    mi_path = f"{temp_path.rsplit('/', 1)[0]}"
    mi_name = temp_path.rsplit("/", 1)[-1]
    create_result = mat.create_material_instance(
        parent_path=src, name=mi_name, content_path=mi_path,
        params=create_params,
    )
    if not create_result:
        return {"success": False, "error": "create_material_instance returned None"}

    # Render the temp MIC to the entry's catalog thumbnail
    catalog_root = _paths.resolve_catalog_root()
    out_path = os.path.join(catalog_root, "thumbnails", f"{eid}.png")
    render_result = mat.render_thumbnail(temp_path, out_path)
    if not render_result.get("success"):
        unreal.EditorAssetLibrary.delete_asset(temp_path)
        return {"success": False, "error": f"thumbnail render failed: {render_result.get('error')}"}

    # Mark the entry's thumbnail_source so the catalog records this is a
    # user-picked variant.
    entry["thumbnail_source"] = temp_path
    entry["thumbnail_source_kind"] = "user_override"
    entry["thumbnail_overrides"] = overrides
    _save(yp, entry)

    # Run the AI auto-tagger on just this entry. Use UE's bundled Python so
    # the subprocess inherits the PyYAML / anthropic SDK packages we installed
    # there. Path derived from the live editor's engine dir; falls back to
    # whatever `python` is on PATH if engine_dir isn't available.
    skill_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ai_script = os.path.join(skill_dir, "scripts", "ai_autotag.py")
    py_exe = None
    try:
        engine_dir = unreal.Paths.engine_dir()
        candidate = os.path.join(engine_dir, "Binaries", "ThirdParty", "Python3", "Win64", "python.exe")
        if os.path.exists(candidate):
            py_exe = candidate
    except Exception:
        pass
    if not py_exe:
        py_exe = shutil.which("python") or "python"
    proc = subprocess.run(
        [py_exe, ai_script, "--ids", eid, "--force"],
        capture_output=True, text=True, timeout=120,
    )
    ai_ok = (proc.returncode == 0)

    # Clean up the temp MIC
    unreal.EditorAssetLibrary.delete_asset(temp_path)

    _index.refresh_index()
    return {
        "success": True,
        "entry_id": eid,
        "thumbnail_overwritten": out_path,
        "ai_ok": ai_ok,
        "ai_stderr_tail": (proc.stderr or "")[-200:] if not ai_ok else "",
    }


OPS = {
    "update": update,
    "set_status": set_status,
    "accept_proposals": accept_proposals,
    "reject_proposals": reject_proposals,
    "delete": delete,
    "refresh_index": refresh_index_op,
    "scan_texture_suggestions": scan_texture_suggestions,
    "retag_from_preview": retag_from_preview,
}


def run(command_json):
    """Entry point. command_json is a JSON string with at least an 'op' key.

    Writes the result via arbor.utils.write_result so the Slate widget can
    pick it up via Saved/Arbor/last_result.json. Returns the result dict.
    """
    try:
        cmd = json.loads(command_json) if isinstance(command_json, str) else command_json
    except Exception as e:
        result = {"success": False, "error": f"invalid json: {e}"}
    else:
        op = cmd.get("op")
        handler = OPS.get(op)
        if not handler:
            result = {"success": False, "error": f"unknown op: {op}", "ops": sorted(OPS.keys())}
        else:
            try:
                result = handler(cmd)
            except Exception as e:
                result = {"success": False, "error": str(e), "op": op}

    # Forward to arbor's result file so the editor side can read it back.
    try:
        import arbor.utils as u
        u.write_result(result)
    except Exception:
        pass
    return result
