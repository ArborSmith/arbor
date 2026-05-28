"""Extract a single UMaterial asset into a catalog YAML entry.

Calls arbor.materials.query_material() to read the graph, converts the
result into a BuildMaterial spec, and writes a YAML stub for hand-tagging.

Usage (from UE5 Python or ue5_run_python):

    # Add the skill dir to sys.path first:
    #   sys.path.insert(0, "<plugin>/.claude/skills/material-authoring")
    from extraction import extract_material
    extract_material.extract(
        "/Game/StarterContent/Materials/M_Brick_Clay_Old",
        out_dir="/path/to/skill/catalog/entries/"
    )
"""

import os
import re

try:
    import yaml
except ImportError:
    raise SystemExit("PyYAML is required. Install via: pip install pyyaml")

import arbor.materials as materials


_SLUG_RE = re.compile(r'[^a-z0-9]+')


def slugify(asset_name: str) -> str:
    """Convert an asset name like M_Brick_Clay_Old to brick_clay_old."""
    name = asset_name
    if name.startswith("M_"):
        name = name[2:]
    name = _SLUG_RE.sub("_", name.lower()).strip("_")
    return name or "material"


def asset_name_from_path(asset_path: str) -> str:
    """Extract M_Brick_Clay_Old from /Game/.../M_Brick_Clay_Old."""
    last = asset_path.rsplit("/", 1)[-1]
    # Handle /Game/Path/Name.Name format too.
    return last.rsplit(".", 1)[0]


_PLACEHOLDER_PREFIXES = ("<struct:", "<unsupported:", "<")


def _normalize_pin(name: str) -> str:
    """Strip the FName::None sentinel.

    An unnamed default output reflects back as the string `"None"` (the
    FName::None sentinel). `ConnectMaterialExpressions` treats an empty
    string as "first output" - which is what we want here.

    MaterialFunctionCall input names come back with display annotations
    like `"In (V3)"`. Those are kept as-is; UE uses the same annotated
    name for both `GetInputName()` and connection-by-name lookup, so the
    round-trip works after MaterialFunctionCall's post-property hook
    runs `SetMaterialFunction()` to populate `FunctionInputs[]`.
    """
    if not name or name == "None":
        return ""
    return name


def _clean_properties(props: dict | None) -> dict:
    """Drop placeholder values that QueryMaterial couldn't serialize properly.

    The C++ reader returns sentinel strings like `<struct:LinearColor>` or
    `<unsupported:EnumProperty>` for types it can't represent. Those would
    break build_material on the way back in, so we drop them. The resulting
    spec uses CDO defaults for those fields; the user can fill them in by
    hand if they matter for the catalog entry.
    """
    if not props:
        return {}
    out = {}
    for k, v in props.items():
        if v is None:
            continue
        if isinstance(v, str) and (v == "" or v.startswith(_PLACEHOLDER_PREFIXES)):
            continue
        if isinstance(v, list) and not v:
            continue
        out[k] = v
    return out


def query_to_spec(query_result: dict, asset_path: str) -> dict:
    """Convert query_material() output into a BuildMaterial spec.

    Uses expression `idx` (always reliable) as the stable mapping when the
    source material wasn't Arbor-authored (no sentinel IDs). Synthetic IDs
    of the form `<class>_<idx>` are minted, then used consistently across
    expressions, connections, and outputs.
    """
    flags = query_result.get("flags", {})
    expressions = query_result.get("expressions", [])
    connections = query_result.get("connections", [])
    outputs = query_result.get("outputs", []) or []

    # idx -> synthesised (or pre-existing) ID
    idx_to_id: dict[int, str] = {}
    spec_expressions = []
    for e in expressions:
        idx = e.get("idx")
        eid = e.get("id") or ""
        if not eid:
            base = (e.get("class", "expr") or "expr").replace("MaterialExpression", "").lower() or "expr"
            eid = f"{base}_{idx if idx is not None else len(idx_to_id)}"
        if idx is not None:
            idx_to_id[idx] = eid

        spec_expressions.append({
            "id": eid,
            "class": e["class"],
            "x": int(e.get("x", 0)),
            "y": int(e.get("y", 0)),
            "properties": _clean_properties(e.get("properties")),
        })

    def _resolve(idx, id_):
        """Resolve a connection endpoint to a spec ID."""
        if id_:
            return id_
        if idx is not None and idx in idx_to_id:
            return idx_to_id[idx]
        return None

    spec_connections = []
    for c in connections:
        f = _resolve(c.get("from_idx"), c.get("from_id"))
        t = _resolve(c.get("to_idx"), c.get("to_id"))
        if not f or not t:
            continue
        spec_connections.append({
            "from": f,
            "from_output": _normalize_pin(c.get("from_output", "")),
            "to": t,
            "to_input": _normalize_pin(c.get("to_input", "")),
        })

    spec_outputs = []
    for o in outputs:
        f = _resolve(o.get("from_idx"), o.get("from_id"))
        if not f:
            continue
        spec_outputs.append({
            "from": f,
            "from_output": _normalize_pin(o.get("from_output", "")),
            "property": o["property"],
        })

    spec = {
        "path": asset_path,
        "parent_class": "Material",
        "flags": {
            "shading_model": flags.get("shading_model", "DefaultLit"),
            "blend_mode": flags.get("blend_mode", "BLEND_Opaque").replace("BLEND_", ""),
            "two_sided": bool(flags.get("two_sided", False)),
        },
        "expressions": spec_expressions,
        "connections": spec_connections,
        "outputs": spec_outputs,
    }
    return spec


def extract(asset_path: str, out_dir: str | None = None, provenance: str = "") -> dict:
    """Extract one material to a YAML file.

    If `out_dir` is None, uses the resolved catalog's entries/ directory.
    Returns a dict with success + path + entry_id.
    """
    if out_dir is None:
        from . import _paths
        out_dir = _paths.entries_dir()
    query_result = materials.query_material(asset_path)
    if not query_result.get("success"):
        return {"success": False, "error": query_result.get("error", "query failed"),
                "asset_path": asset_path}

    asset_name = asset_name_from_path(asset_path)
    entry_id = slugify(asset_name)

    spec = query_to_spec(query_result, asset_path)

    flags = query_result.get("flags", {})
    entry = {
        "id": entry_id,
        "source": asset_path,
        "status": "needs_review",
        "shading_model": flags.get("shading_model", "DefaultLit"),
        "tags": [],
        "visual_traits": [],
        "description": "",
        "spec": spec,
        "provenance": provenance,
        "quality": {
            "compile": "unknown",
            "roundtrip": "unknown",
        },
        "mi_compatible": False,
    }

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{entry_id}.yaml")
    # Preserve hand-tagged fields (tags / visual_traits / description / status)
    # from any pre-existing entry so re-extracting a material doesn't wipe
    # curated metadata. Only the spec needs to reflect the latest source.
    if os.path.exists(out_path):
        try:
            with open(out_path, "r", encoding="utf-8") as f:
                prior = yaml.safe_load(f) or {}
            for k in ("status", "tags", "visual_traits", "description", "mi_compatible"):
                if prior.get(k):
                    entry[k] = prior[k]
        except Exception:
            pass

    with open(out_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)

    # Refresh the index so the Slate widget picks up the change.
    try:
        from . import _index
        _index.refresh_index(out_dir)
    except Exception:
        pass

    return {"success": True, "yaml_path": out_path, "entry_id": entry_id,
            "expression_count": len(spec["expressions"])}
