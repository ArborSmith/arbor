import { callArborJson } from "../../src/ue5-client.js";

/**
 * Delete all actors whose label starts with "IntTest_" from the current level.
 * Call this in beforeAll to start each test suite with a clean slate.
 */
export async function clearTestActors(): Promise<void> {
  try {
    // Get all actors with the IntTest_ prefix
    const scene = (await callArborJson("ArborActorTools", "GetSceneInfo", {
      FilterClass: "",
      FilterPrefix: "IntTest_",
    })) as { actors?: Array<{ name: string; label: string }> };

    if (scene.actors && scene.actors.length > 0) {
      const names = scene.actors.map((a) => a.label || a.name);
      await callArborJson("ArborActorTools", "DeleteActors", {
        ActorNamesJson: JSON.stringify(names),
      });
    }
  } catch {
    /* best-effort — level may not have any test actors */
  }
}
