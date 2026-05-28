"""Round-trip validate a catalog YAML entry.

Builds the entry's spec into a sandbox material, re-extracts, structurally
diffs the two. Writes quality.roundtrip = "ok" or "failed" back into the
YAML so the catalog tracks which entries are reliable building blocks.
"""

try:
    import yaml
except ImportError:
    raise SystemExit("PyYAML is required. Install via: pip install pyyaml")

import arbor.materials as materials
from . import extract_material


SANDBOX_ROOT = "/Game/_ArborSandbox/MaterialCatalog"


def _structural_diff(spec_a: dict, spec_b: dict) -> list[str]:
    """Return a list of diff strings. Empty list = structurally identical.

    Ignores position (x, y) and minor property formatting; cares about
    expression IDs, classes, connection graph, and output bindings.
    """
    diffs = []

    a_exprs = {e["id"]: e for e in spec_a.get("expressions", [])}
    b_exprs = {e["id"]: e for e in spec_b.get("expressions", [])}

    missing_in_b = set(a_exprs) - set(b_exprs)
    missing_in_a = set(b_exprs) - set(a_exprs)
    for mid in missing_in_b:
        diffs.append(f"expression missing after roundtrip: {mid} ({a_exprs[mid].get('class')})")
    for mid in missing_in_a:
        diffs.append(f"expression appeared after roundtrip: {mid} ({b_exprs[mid].get('class')})")

    for eid, a in a_exprs.items():
        b = b_exprs.get(eid)
        if not b:
            continue
        if a.get("class") != b.get("class"):
            diffs.append(f"class mismatch on {eid}: {a.get('class')} -> {b.get('class')}")

    a_conns = {(c.get("from"), c.get("to"), c.get("to_input", ""))
               for c in spec_a.get("connections", [])}
    b_conns = {(c.get("from"), c.get("to"), c.get("to_input", ""))
               for c in spec_b.get("connections", [])}
    for missing in a_conns - b_conns:
        diffs.append(f"connection lost: {missing[0]} -> {missing[1]}.{missing[2]}")
    for extra in b_conns - a_conns:
        diffs.append(f"connection appeared: {extra[0]} -> {extra[1]}.{extra[2]}")

    return diffs


def validate(yaml_path: str, sandbox_root: str = SANDBOX_ROOT) -> dict:
    """Build, re-extract, diff. Updates the YAML in place with the verdict.

    Returns dict with success + diffs.
    """
    with open(yaml_path, "r", encoding="utf-8") as f:
        entry = yaml.safe_load(f)

    original_spec = entry["spec"]
    sandbox_path = f"{sandbox_root}/SB_{entry['id']}"
    sandbox_spec = dict(original_spec)
    sandbox_spec["path"] = sandbox_path

    build_result = materials.build_material(sandbox_spec)
    if not build_result.get("success"):
        entry["quality"]["roundtrip"] = "failed"
        entry["quality"]["roundtrip_error"] = build_result.get("error", "build failed")
        _write_back(yaml_path, entry)
        return {"success": False, "error": build_result.get("error"),
                "diffs": [], "yaml_path": yaml_path}

    extract_result = materials.query_material(sandbox_path)
    if not extract_result.get("success"):
        entry["quality"]["roundtrip"] = "failed"
        entry["quality"]["roundtrip_error"] = "re-query failed"
        _write_back(yaml_path, entry)
        return {"success": False, "error": "re-query failed", "diffs": [], "yaml_path": yaml_path}

    reextracted_spec = extract_material.query_to_spec(extract_result, sandbox_path)
    diffs = _structural_diff(original_spec, reextracted_spec)

    if not diffs:
        entry["quality"]["roundtrip"] = "ok"
        entry["quality"].pop("roundtrip_error", None)
    else:
        entry["quality"]["roundtrip"] = "failed"
        entry["quality"]["roundtrip_diff"] = diffs[:20]

    _write_back(yaml_path, entry)
    return {"success": not diffs, "diffs": diffs, "yaml_path": yaml_path}


def _write_back(yaml_path: str, entry: dict) -> None:
    with open(yaml_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)
    # Keep the index in sync (cheap; same scan we'd do anyway).
    try:
        from . import _index
        _index.refresh_index(os.path.dirname(os.path.abspath(yaml_path)))
    except Exception:
        pass
