"""AI auto-tag: feed each catalog entry's thumbnail + spec summary to the
local `claude` CLI, get proposed tags / traits / description / mi_compatible.

Reuses the user's existing Claude Code installation and auth - no separate
API key required.

Proposals land in `proposed_*` fields on the entry, not the primary fields,
so a human (or the Slate widget) can review before accepting. Entries also
get flipped to `status: needs_review` until accepted.

Skips entries that:
  - Have `status: bad` or `deprecated` (won't tag dead entries)
  - Have `thumbnail_source_kind: placeholder` (no visual for the model)
  - Already have proposed_* unless --force

Usage:
    # Dry run on first 3 entries (writes nothing):
    python ai_autotag.py --limit 3 --dry-run

    # Tag all entries needing review:
    python ai_autotag.py

    # Re-tag everything regardless of existing proposals:
    python ai_autotag.py --force

    # Tag specific entries only:
    python ai_autotag.py --ids concrete metal floor
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

try:
    import yaml
except ImportError:
    print(json.dumps({"error": "PyYAML required"}))
    sys.exit(1)

_HERE = os.path.dirname(os.path.abspath(__file__))
_SKILL_DIR = os.path.dirname(_HERE)
if _SKILL_DIR not in sys.path:
    sys.path.insert(0, _SKILL_DIR)

from extraction import _paths, _index


SKIP_STATUSES = {"bad", "deprecated"}
CLAUDE_TIMEOUT_SEC = 90


def _read_vocabulary(catalog_root: str) -> str:
    p = os.path.join(catalog_root, "vocabulary.md")
    if not os.path.exists(p):
        return ""
    with open(p, "r", encoding="utf-8") as f:
        return f.read()


def _build_spec_summary(entry: dict) -> str:
    spec = entry.get("spec", {}) or {}
    flags = spec.get("flags", {}) or {}
    exprs = spec.get("expressions", []) or []
    outs = spec.get("outputs", []) or []

    texture_params, scalar_params, vector_params = [], [], []
    for e in exprs:
        cls = e.get("class", "")
        pname = (e.get("properties") or {}).get("ParameterName", "")
        if not pname:
            continue
        if "TextureSampleParameter" in cls or "TextureObjectParameter" in cls:
            texture_params.append(pname)
        elif "ScalarParameter" in cls:
            scalar_params.append(pname)
        elif "VectorParameter" in cls:
            vector_params.append(pname)

    return (
        f"Source asset: {entry.get('source', '?')}\n"
        f"Shading model: {flags.get('shading_model', '?')}\n"
        f"Blend mode: {flags.get('blend_mode', '?')}\n"
        f"Two-sided: {flags.get('two_sided', False)}\n"
        f"Expression count: {len(exprs)}\n"
        f"Wired outputs: {[o.get('property') for o in outs]}\n"
        f"Texture parameters: {texture_params}\n"
        f"Scalar parameters: {scalar_params}\n"
        f"Vector parameters: {vector_params}\n"
        f"Thumbnail rendered from: {entry.get('thumbnail_source', entry.get('source'))}"
    )


PROMPT_TEMPLATE = """You are a material catalog tagger for a UE5 game project.

I'm going to show you a material's thumbnail image and a technical summary of
its node graph. Propose:
  - tags: 3-6 free-form lowercase_with_underscores keywords (subject, state, aesthetic)
  - visual_traits: 3-6 traits picked from the controlled vocabulary below
  - description: 1-3 sentences focused on surface character, intended use, key parameters
  - mi_compatible: true if the visual is achievable purely as a MaterialInstance
    of /Game/Materials/M_PBR_Parameterized (basic PBR with Albedo/Normal/Roughness/
    Metallic/AO/Tiling parameters); false otherwise

Return ONLY valid JSON in exactly this shape, no prose, no markdown fence:
{{
  "tags": ["..."],
  "visual_traits": ["..."],
  "description": "...",
  "mi_compatible": true
}}

## Controlled vocabulary

{vocabulary}

## The material

Entry id: {entry_id}
Thumbnail: @{thumbnail_path}

Spec summary:
{spec_summary}

Now return the JSON.
"""


def _parse_response(text: str) -> dict | None:
    text = (text or "").strip()
    if text.startswith("```"):
        text = text.split("```", 2)[1]
        if text.startswith("json"):
            text = text[4:]
        text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        i, j = text.find("{"), text.rfind("}")
        if i >= 0 and j > i:
            try:
                return json.loads(text[i:j + 1])
            except json.JSONDecodeError:
                return None
        return None


def autotag_entry(entry: dict, yaml_path: str, thumbnail_path: str,
                   vocabulary: str, claude_exe: str,
                   dry_run: bool = False) -> dict:
    prompt = PROMPT_TEMPLATE.format(
        vocabulary=vocabulary,
        entry_id=entry.get("id", "?"),
        thumbnail_path=os.path.abspath(thumbnail_path),
        spec_summary=_build_spec_summary(entry),
    )

    try:
        result = subprocess.run(
            [claude_exe, "-p", prompt, "--output-format", "text"],
            capture_output=True, text=True, timeout=CLAUDE_TIMEOUT_SEC,
            encoding="utf-8", errors="replace",
        )
    except subprocess.TimeoutExpired:
        return {"id": entry.get("id"), "ok": False, "err": "timeout"}
    except FileNotFoundError:
        return {"id": entry.get("id"), "ok": False, "err": f"claude exe not found at {claude_exe}"}

    if result.returncode != 0:
        return {"id": entry.get("id"), "ok": False,
                "err": f"exit {result.returncode}", "stderr": (result.stderr or "")[-200:]}

    parsed = _parse_response(result.stdout)
    if not parsed:
        return {"id": entry.get("id"), "ok": False, "raw": (result.stdout or "")[:300]}

    if not dry_run:
        entry["proposed_tags"] = parsed.get("tags", [])
        entry["proposed_visual_traits"] = parsed.get("visual_traits", [])
        entry["proposed_description"] = parsed.get("description", "")
        entry["proposed_mi_compatible"] = bool(parsed.get("mi_compatible", False))
        entry["status"] = "needs_review"
        with open(yaml_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(entry, f, sort_keys=False, default_flow_style=False)

    return {
        "id": entry.get("id"), "ok": True,
        "tags": parsed.get("tags", []),
        "traits": parsed.get("visual_traits", []),
        "description": parsed.get("description", "")[:80],
        "mi_compatible": parsed.get("mi_compatible", False),
    }


def _find_claude() -> str:
    """Locate the `claude` CLI. Honour env override; else PATH lookup."""
    if (env := os.environ.get("CLAUDE_CLI")) and os.path.exists(env):
        return env
    exe = shutil.which("claude")
    if exe:
        return exe
    # Fallback: common npm-global location on Windows
    candidates = [
        os.path.expandvars(r"%APPDATA%\npm\claude.cmd"),
        os.path.expandvars(r"%USERPROFILE%\AppData\Roaming\npm\claude.cmd"),
        "/usr/local/bin/claude",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return ""


def main():
    ap = argparse.ArgumentParser(description="AI-tag catalog entries via the local `claude` CLI.")
    ap.add_argument("--catalog-root", default=None)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="Re-tag entries that already have proposed_* fields.")
    ap.add_argument("--ids", nargs="*")
    ap.add_argument("--claude-exe", default=None,
                    help="Path to the claude CLI. Defaults to PATH lookup.")
    args = ap.parse_args()

    claude_exe = args.claude_exe or _find_claude()
    if not claude_exe:
        print(json.dumps({"error": "claude CLI not found; install Claude Code or pass --claude-exe"}))
        sys.exit(1)

    catalog_root = args.catalog_root or _paths.resolve_catalog_root()
    entries_dir = _paths.entries_dir(catalog_root)
    thumbs_dir = _paths.thumbnails_dir(catalog_root)
    vocabulary = _read_vocabulary(catalog_root)

    candidates = []
    for fname in sorted(os.listdir(entries_dir)):
        if not fname.endswith(".yaml"):
            continue
        path = os.path.join(entries_dir, fname)
        with open(path, "r", encoding="utf-8") as f:
            entry = yaml.safe_load(f) or {}
        eid = entry.get("id") or os.path.splitext(fname)[0]
        if args.ids and eid not in args.ids:
            continue
        if entry.get("status") in SKIP_STATUSES:
            continue
        if entry.get("thumbnail_source_kind") == "placeholder":
            continue
        if entry.get("proposed_tags") and not args.force:
            continue
        thumb = os.path.join(thumbs_dir, f"{eid}.png")
        if not os.path.exists(thumb):
            continue
        candidates.append((entry, path, thumb))
        if args.limit and len(candidates) >= args.limit:
            break

    results = []
    t0 = time.time()
    for entry, yaml_path, thumb_path in candidates:
        r = autotag_entry(entry, yaml_path, thumb_path, vocabulary, claude_exe,
                          dry_run=args.dry_run)
        results.append(r)
        if r.get("ok"):
            print(f"  {r['id']:30s} tags={r['tags']} traits={r['traits']}", file=sys.stderr)
        else:
            print(f"  {r['id']:30s} FAILED err={r.get('err', '?')}", file=sys.stderr)

    if not args.dry_run and any(r.get("ok") for r in results):
        _index.refresh_index(entries_dir)

    elapsed = round(time.time() - t0, 1)
    print(json.dumps({
        "processed": len(results),
        "ok": sum(1 for r in results if r.get("ok")),
        "failed": sum(1 for r in results if not r.get("ok")),
        "elapsed_sec": elapsed,
        "claude_exe": claude_exe,
        "dry_run": args.dry_run,
    }, indent=2))


if __name__ == "__main__":
    main()
