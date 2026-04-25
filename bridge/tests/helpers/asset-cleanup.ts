import { runPython } from "../../src/tools/core/run-python.js";

export async function deleteTestAssets(): Promise<void> {
  try {
    const result = await runPython({
      code: `
import unreal
import os
import shutil
import time

content_dir = unreal.Paths.project_content_dir()
int_test_dir = os.path.normpath(os.path.join(content_dir, "IntTest"))

if not os.path.isdir(int_test_dir):
    _write_result({"success": True, "note": "directory did not exist"})
else:
    file_count_before = sum(len(files) for _, _, files in os.walk(int_test_dir))

    # GC to release file handles held by loaded assets
    for i in range(5):
        unreal.SystemLibrary.collect_garbage()

    deleted = False
    remaining = file_count_before
    for attempt in range(5):
        shutil.rmtree(int_test_dir, ignore_errors=True)
        if not os.path.isdir(int_test_dir):
            deleted = True
            remaining = 0
            break
        remaining = sum(len(files) for _, _, files in os.walk(int_test_dir))
        time.sleep(0.5)
        unreal.SystemLibrary.collect_garbage()

    _write_result({
        "success": deleted,
        "disk_path": int_test_dir,
        "files_before": file_count_before,
        "files_remaining": remaining,
        "note": "cleaned" if deleted else "some files still locked by editor",
    })
`,
    });
    console.error(`[ue5-bridge] deleteTestAssets result: ${JSON.stringify(result.result ?? result.error)}`);
  } catch (e) {
    console.error(`[ue5-bridge] deleteTestAssets error: ${e}`);
  }
}
