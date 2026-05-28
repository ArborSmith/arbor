"""Catalog path resolution.

The catalog lives in the PROJECT (Perforce-tracked), not the plugin (git).
This module figures out where the catalog root is for the current context.

Resolution order:
  1. Env var ARBOR_MATERIAL_CATALOG_ROOT (explicit override)
  2. <project>/MaterialCatalog/ when running inside UE5
     (resolved via unreal.Paths.project_dir())
  3. Walk up from CWD looking for a MaterialCatalog/ directory with a
     config.yaml inside it (for CLI tooling outside UE5)
  4. Raise CatalogNotFoundError - caller must create one with bootstrap()

The catalog layout is:
  <root>/
    config.yaml                  // sentinel + per-project settings
    vocabulary.md                // project-tuned tag vocabulary
    entries/<id>.yaml            // full YAML entries
    thumbnails/<id>.png          // rendered thumbnails
    _index.json                  // generated; flat view for the Slate widget
"""

import os

try:
    import yaml
except ImportError:
    yaml = None


class CatalogNotFoundError(RuntimeError):
    pass


_ENV_OVERRIDE = "ARBOR_MATERIAL_CATALOG_ROOT"
_CONFIG_FILENAME = "config.yaml"


def _is_catalog_root(path: str) -> bool:
    return os.path.isfile(os.path.join(path, _CONFIG_FILENAME))


def _try_env() -> str | None:
    value = os.environ.get(_ENV_OVERRIDE)
    if value and os.path.isdir(value):
        return value
    return None


def _try_unreal_project() -> str | None:
    try:
        import unreal  # type: ignore
    except ImportError:
        return None
    try:
        project_dir = unreal.Paths.project_dir()
    except Exception:
        return None
    if not project_dir:
        return None
    candidate = os.path.join(project_dir, "MaterialCatalog")
    if os.path.isdir(candidate):
        return os.path.normpath(candidate)
    return None


def _try_walk_up(start: str | None = None) -> str | None:
    cur = os.path.abspath(start or os.getcwd())
    seen = set()
    while cur not in seen:
        seen.add(cur)
        candidate = os.path.join(cur, "MaterialCatalog")
        if _is_catalog_root(candidate):
            return candidate
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return None


def resolve_catalog_root(start_dir: str | None = None,
                         allow_missing_config: bool = False) -> str:
    """Return the absolute path of the catalog root for the current context.

    Args:
        start_dir: for CLI-side walk-up search. If None, uses cwd.
        allow_missing_config: if True, returns the UE-resolved path even if
            config.yaml doesn't exist yet (useful for the bootstrap step).

    Raises:
        CatalogNotFoundError if no catalog can be found.
    """
    env = _try_env()
    if env:
        return env

    ue_path = _try_unreal_project()
    if ue_path:
        if allow_missing_config or _is_catalog_root(ue_path):
            return ue_path

    walk = _try_walk_up(start_dir)
    if walk:
        return walk

    raise CatalogNotFoundError(
        f"No MaterialCatalog/ found. Set {_ENV_OVERRIDE} or "
        f"run extraction from inside UE5 with a <project>/MaterialCatalog/ folder."
    )


def entries_dir(catalog_root: str | None = None) -> str:
    root = catalog_root or resolve_catalog_root()
    return os.path.join(root, "entries")


def thumbnails_dir(catalog_root: str | None = None) -> str:
    root = catalog_root or resolve_catalog_root()
    return os.path.join(root, "thumbnails")


def index_path(catalog_root: str | None = None) -> str:
    root = catalog_root or resolve_catalog_root()
    return os.path.join(root, "_index.json")


def config_path(catalog_root: str | None = None) -> str:
    root = catalog_root or resolve_catalog_root()
    return os.path.join(root, _CONFIG_FILENAME)


def read_config(catalog_root: str | None = None) -> dict:
    """Load the catalog's config.yaml. Returns {} if missing."""
    path = config_path(catalog_root)
    if not os.path.isfile(path) or yaml is None:
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def bootstrap(catalog_root: str, provenance: str = "") -> dict:
    """Create a fresh MaterialCatalog/ at the given path.

    Idempotent - if the catalog already exists, leaves it alone but ensures
    the directory structure is complete.
    """
    if yaml is None:
        raise RuntimeError("PyYAML required to bootstrap a catalog")

    os.makedirs(os.path.join(catalog_root, "entries"), exist_ok=True)
    os.makedirs(os.path.join(catalog_root, "thumbnails"), exist_ok=True)

    cfg_path = os.path.join(catalog_root, _CONFIG_FILENAME)
    if not os.path.exists(cfg_path):
        cfg = {
            "version": 1,
            "default_vendor_dir": "/Game/MaterialCatalog",
            "default_provenance": provenance or "",
            "notes": (
                "This file marks the catalog root. It is read by the Arbor "
                "material-authoring skill to locate entries/, thumbnails/, "
                "and the _index.json. Commit this folder to Perforce so the "
                "team shares the catalog."
            ),
        }
        with open(cfg_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, sort_keys=False, default_flow_style=False)
    return read_config(catalog_root)
