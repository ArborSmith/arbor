import { callArborJson } from "../../src/ue5-client.js";

const spawnedActors: string[] = [];

export function trackActor(name: string): void {
  spawnedActors.push(name);
}

export function trackActors(names: string[]): void {
  spawnedActors.push(...names);
}

export async function cleanupAll(): Promise<void> {
  if (spawnedActors.length === 0) return;
  try {
    await callArborJson("ArborActorTools", "DeleteActors", {
      ActorNamesJson: JSON.stringify([...spawnedActors]),
    });
  } catch {
    console.warn(
      `Cleanup warning: could not delete actors: ${spawnedActors.join(", ")}`
    );
  }
  spawnedActors.length = 0;
}
