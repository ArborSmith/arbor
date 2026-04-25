"""Import a 3D model file into the UE5 content browser.

Runs inside UE5's embedded Python interpreter (not regular Python).
Args after --result-file <path>:
  file_path     (absolute path to GLB/GLTF/FBX)
  content_path  (UE5 content browser destination)
  asset_name    (name for the imported asset)

Optional flags:
  --auto-fix-pivot          Fix mesh pivot to bottom after import
  --scale-factor <float>    Uniformly scale the mesh after import (e.g. 100.0)
"""

import sys
import json
import unreal


def write_result(result_file, data):
    with open(result_file, "w") as f:
        json.dump(data, f)


def import_fbx(file_path, content_path, asset_name):
    """Import FBX using standard AssetImportTask."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", file_path)
    task.set_editor_property("destination_path", content_path)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_as_skeletal", False)
    task.set_editor_property("options", options)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_tools.import_asset_tasks([task])

    paths = task.get_editor_property("imported_object_paths")
    return list(paths) if paths else []


def import_glb(file_path, content_path, asset_name):
    """Import GLB/GLTF using AssetImportTask (Interchange framework in UE5 5.4+)."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", file_path)
    task.set_editor_property("destination_path", content_path)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_tools.import_asset_tasks([task])

    paths = task.get_editor_property("imported_object_paths")
    return list(paths) if paths else []


def make_unique_name(content_path, asset_name):
    """If an asset already exists at content_path/asset_name, append _01, _02, etc."""
    full_path = "{}/{}".format(content_path, asset_name)
    if not unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return asset_name
    for i in range(1, 100):
        candidate = "{}_{:02d}".format(asset_name, i)
        if not unreal.EditorAssetLibrary.does_asset_exist(
            "{}/{}".format(content_path, candidate)
        ):
            unreal.log("Asset '{}' exists, using unique name: {}".format(
                asset_name, candidate))
            return candidate
    # Exhausted — fall through with original name (will overwrite)
    return asset_name


def import_asset(result_file, file_path, content_path, asset_name,
                 auto_fix_pivot=False, scale_factor=1.0):
    # Ensure unique name to avoid overwriting existing assets
    asset_name = make_unique_name(content_path, asset_name)

    ext = file_path.rsplit(".", 1)[-1].lower()
    if ext in ("glb", "gltf"):
        paths = import_glb(file_path, content_path, asset_name)
    elif ext == "fbx":
        paths = import_fbx(file_path, content_path, asset_name)
    else:
        write_result(result_file, {
            "success": False,
            "error": "Unsupported file format: {}".format(ext),
        })
        return

    success = len(paths) > 0

    # Find the actual StaticMesh asset path from imported_paths.
    # Interchange (GLB/GLTF) creates subdirectories like:
    #   content_path/source_filename/StaticMeshes/asset_name
    # while FBX imports directly to content_path/asset_name.
    # We search imported_paths for the StaticMesh to use for post-processing.
    mesh_path = "{}/{}".format(content_path, asset_name)  # fallback
    if paths:
        for p in paths:
            # Strip the ".AssetName" object suffix if present
            clean = p.split(".")[0] if "." in p else p
            if "/StaticMeshes/" in clean or clean.endswith(asset_name):
                mesh_path = clean
                break

    result = {
        "success": success,
        "imported_paths": paths,
        "asset_path": mesh_path,
        "asset_name": asset_name,
    }
    if not success:
        result["error"] = "Import returned no assets — check UE5 Output Log"

    # Auto-scale after successful import (e.g. 100x for Meshy metre→cm fix)
    if success and abs(scale_factor - 1.0) > 1e-6:
        try:
            import arbor.mesh
            scale_result = arbor.mesh.fix_mesh_scale(mesh_path, scale=scale_factor)
            if scale_result and scale_result.get("success"):
                result["scale_fix"] = scale_result
            else:
                result["scale_fix_warning"] = (
                    "fix_mesh_scale returned no success — check UE5 Output Log"
                )
        except Exception as e:
            result["scale_fix_warning"] = "fix_mesh_scale failed: {}".format(e)

    # Auto-fix pivot after successful import (and after scale, so bounds are correct)
    if success and auto_fix_pivot:
        try:
            import arbor.mesh
            pivot_result = arbor.mesh.fix_mesh_pivot(mesh_path, pivot="bottom")
            if pivot_result and pivot_result.get("success"):
                result["pivot_fix"] = pivot_result
            else:
                result["pivot_fix_warning"] = (
                    "fix_mesh_pivot returned no success — check UE5 Output Log"
                )
        except Exception as e:
            result["pivot_fix_warning"] = "fix_mesh_pivot failed: {}".format(e)

    write_result(result_file, result)
    unreal.log("Import result: {}".format("success" if success else "failed"))


if __name__ == "__main__":
    args = sys.argv[1:]

    auto_fix_pivot = "--auto-fix-pivot" in args
    if auto_fix_pivot:
        args.remove("--auto-fix-pivot")

    scale_factor = 1.0
    if "--scale-factor" in args:
        sf_idx = args.index("--scale-factor")
        scale_factor = float(args[sf_idx + 1])
        args = args[:sf_idx] + args[sf_idx + 2:]

    if "--result-file" not in args:
        unreal.log_error("Missing --result-file argument")
    else:
        idx = args.index("--result-file")
        result_file = args[idx + 1]
        remaining = args[:idx] + args[idx + 2:]
        if len(remaining) < 3:
            write_result(result_file, {
                "success": False,
                "error": "Usage: import_asset.py --result-file <path> <file_path> <content_path> <asset_name>",
            })
        else:
            import_asset(result_file, remaining[0], remaining[1], remaining[2],
                         auto_fix_pivot=auto_fix_pivot,
                         scale_factor=scale_factor)
