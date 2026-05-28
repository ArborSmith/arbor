"""Extract a directory of UMaterial assets into catalog YAMLs.

Walks a content path via the Asset Registry, runs extract_material.extract()
on each UMaterial, prints a summary.
"""

import unreal

from . import extract_material


def extract_all(content_path: str, out_dir: str | None = None, provenance: str = "",
                limit: int = 0) -> dict:
    """Extract every UMaterial under content_path.

    Args:
        content_path: e.g. "/Game/StarterContent/Materials"
        out_dir: filesystem path for YAML output
        provenance: free-form attribution string written into each entry
        limit: 0 = no limit. Otherwise stop after N materials.

    Returns:
        dict with counts + per-asset results.
    """
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = asset_registry.get_assets_by_path(content_path, recursive=True)

    results = []
    extracted = 0
    failed = 0
    skipped = 0

    for asset_data in assets:
        # Filter to UMaterial (not instances, not functions).
        if str(asset_data.asset_class_path.asset_name) != "Material":
            skipped += 1
            continue

        asset_path = str(asset_data.package_name) + "." + str(asset_data.asset_name)
        # query_material wants the package path, not the full object path.
        package_path = str(asset_data.package_name)

        result = extract_material.extract(package_path, out_dir, provenance=provenance)
        results.append(result)
        if result.get("success"):
            extracted += 1
        else:
            failed += 1

        if limit and extracted >= limit:
            break

    summary = {
        "content_path": content_path,
        "extracted": extracted,
        "failed": failed,
        "skipped_non_material": skipped,
        "results": results,
    }
    unreal.log(f"[extract_batch] extracted={extracted} failed={failed} "
               f"skipped={skipped} from {content_path}")
    return summary
