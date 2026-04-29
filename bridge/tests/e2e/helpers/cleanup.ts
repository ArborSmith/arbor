/**
 * E2E-scoped cleanup helpers. Mirror `tests/helpers/level-isolation.ts` and
 * `tests/helpers/asset-cleanup.ts` but parameterized so each scenario can use
 * its own prefix without modifying the shared helpers (which the integration
 * suite depends on with their hardcoded "IntTest_" / "IntTest" values).
 */

import { callArborJson } from "../../../src/ue5-client.js";
import { runPython } from "../../../src/tools/core/run-python.js";

export async function clearActorsByPrefix(prefix: string): Promise<void> {
  try {
    const scene = (await callArborJson("ArborActorTools", "GetSceneInfo", {
      FilterClass: "",
      FilterPrefix: prefix,
    })) as { actors?: Array<{ name: string; label: string }> };

    if (scene.actors && scene.actors.length > 0) {
      const names = scene.actors.map((a) => a.label || a.name);
      await callArborJson("ArborActorTools", "DeleteActors", {
        ActorNamesJson: JSON.stringify(names),
      });
    }
  } catch {
    /* best-effort */
  }
}

/**
 * Delete a content subtree (e.g. "/Game/E2E"). Uses the same retry+GC dance as
 * `asset-cleanup.ts` because UE5 holds file handles on loaded assets.
 */
export async function deleteAssetsUnder(contentSubdir: string): Promise<void> {
  const subdir = contentSubdir.replace(/^\/Game\/?/, "").replace(/\/$/, "");
  if (!subdir) return;
  try {
    await runPython({
      code: `
import unreal
import os
import shutil
import time

content_dir = unreal.Paths.project_content_dir()
target_dir = os.path.normpath(os.path.join(content_dir, ${JSON.stringify(subdir)}))

if not os.path.isdir(target_dir):
    _write_result({"success": True, "note": "directory did not exist"})
else:
    file_count_before = sum(len(files) for _, _, files in os.walk(target_dir))
    for _ in range(5):
        unreal.SystemLibrary.collect_garbage()

    deleted = False
    remaining = file_count_before
    for _ in range(5):
        shutil.rmtree(target_dir, ignore_errors=True)
        if not os.path.isdir(target_dir):
            deleted = True
            remaining = 0
            break
        remaining = sum(len(files) for _, _, files in os.walk(target_dir))
        time.sleep(0.5)
        unreal.SystemLibrary.collect_garbage()

    _write_result({
        "success": deleted,
        "files_before": file_count_before,
        "files_remaining": remaining,
    })
`,
    });
  } catch {
    /* best-effort */
  }
}
