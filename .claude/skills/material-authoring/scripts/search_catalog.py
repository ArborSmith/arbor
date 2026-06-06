"""Tag-overlap retrieval over the catalog.

Score = 2 * trait_overlap + 1 * tag_overlap. Returns top N entries as JSON.

By default skips entries with status=bad or status=deprecated. Use
--include-bad to include them.

Usage:
    python search_catalog.py --tags "brick weathered outdoor" \
                             --traits "high_roughness dielectric" \
                             --top 3
"""

import argparse
import json
import os
import sys

try:
    import yaml
except ImportError:
    print(json.dumps({"error": "PyYAML required - install via: pip install pyyaml"}))
    sys.exit(1)


# Make the sibling `extraction` package importable so we can reuse _paths.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SKILL_DIR = os.path.dirname(_HERE)
if _SKILL_DIR not in sys.path:
    sys.path.insert(0, _SKILL_DIR)

EXCLUDED_STATUSES_DEFAULT = {"bad", "deprecated", "broken"}


def _default_catalog_dir() -> str:
    try:
        from extraction import _paths
        return _paths.entries_dir()
    except Exception:
        return ""


CATALOG_DIR = _default_catalog_dir()


def load_catalog(catalog_dir: str, excluded_statuses: set[str] | None = None) -> list[dict]:
    excluded = excluded_statuses if excluded_statuses is not None else EXCLUDED_STATUSES_DEFAULT
    entries = []
    if not os.path.isdir(catalog_dir):
        return entries
    for fname in sorted(os.listdir(catalog_dir)):
        if not fname.endswith(".yaml"):
            continue
        path = os.path.join(catalog_dir, fname)
        try:
            with open(path, "r", encoding="utf-8") as f:
                entry = yaml.safe_load(f)
                if not entry:
                    continue
                if entry.get("status", "ok") in excluded:
                    continue
                entry["_path"] = path
                entries.append(entry)
        except Exception as e:
            print(f"warning: failed to load {path}: {e}", file=sys.stderr)
    return entries


def score(entry: dict, tags: set[str], traits: set[str]) -> int:
    e_tags = set((entry.get("tags") or []))
    e_traits = set((entry.get("visual_traits") or []))
    return 2 * len(e_traits & traits) + 1 * len(e_tags & tags)


def search(tags: list[str], traits: list[str],
           top: int = 3, catalog_dir: str = CATALOG_DIR,
           include_bad: bool = False, type_filter: str | None = None) -> list[dict]:
    excluded = set() if include_bad else EXCLUDED_STATUSES_DEFAULT
    entries = load_catalog(catalog_dir, excluded_statuses=excluded)
    # Optional filter by entry type ("reference_material" | "pattern"). Entries
    # with no `type` field are treated as "reference_material".
    if type_filter:
        entries = [e for e in entries
                   if e.get("type", "reference_material") == type_filter]
    tags_set = set(tags or [])
    traits_set = set(traits or [])
    scored = [(score(e, tags_set, traits_set), e) for e in entries]
    scored.sort(key=lambda x: -x[0])
    results = []
    for s, e in scored[:top]:
        if s == 0:
            continue
        etype = e.get("type", "reference_material")
        results.append({
            "id": e.get("id"),
            "score": s,
            "type": etype,
            "status": e.get("status", "ok"),
            "tags": e.get("tags") or [],
            "visual_traits": e.get("visual_traits") or [],
            "description": e.get("description", ""),
            "mi_compatible": e.get("mi_compatible", False),
            # For pattern entries, surface the MF asset + its IO so the caller
            # can compose it via a MaterialFunctionCall without re-reading YAML.
            "mf_path": e.get("mf_path", "") if etype == "pattern" else "",
            "inputs": e.get("inputs") or [] if etype == "pattern" else [],
            "outputs": e.get("outputs") or [] if etype == "pattern" else [],
            "yaml_path": e.get("_path"),
        })
    return results


def main():
    ap = argparse.ArgumentParser(description="Tag-overlap catalog retrieval.")
    ap.add_argument("--tags", default="", help="Space-separated tags")
    ap.add_argument("--traits", default="", help="Space-separated visual traits")
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("--catalog", default=None,
                    help="Path to catalog entries/ dir. Defaults to <project>/MaterialCatalog/entries/.")
    ap.add_argument("--include-bad", action="store_true",
                    help="Include entries with status=bad/deprecated/broken (default: skip)")
    ap.add_argument("--type", default=None, dest="type_filter",
                    choices=["reference_material", "pattern"],
                    help="Restrict results to one entry type (default: all types)")
    args = ap.parse_args()

    catalog_dir = args.catalog or _default_catalog_dir()
    if not catalog_dir:
        print(json.dumps({"error": "could not resolve catalog dir; set ARBOR_MATERIAL_CATALOG_ROOT"}))
        sys.exit(1)

    results = search(
        tags=[t for t in args.tags.split() if t],
        traits=[t for t in args.traits.split() if t],
        top=args.top,
        catalog_dir=catalog_dir,
        include_bad=args.include_bad,
        type_filter=args.type_filter,
    )
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
