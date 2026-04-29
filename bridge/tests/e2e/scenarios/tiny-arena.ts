/**
 * "Tiny Arena" — first E2E scenario.
 *
 * High-level prompt that exercises 7 of the 11 stable MCP categories:
 *   actors, materials, lighting, blueprint, ai, capture, (implicit) assets
 *
 * The prompt deliberately pins down predictable labels and asset paths so the
 * validators can find what Claude built. We're testing Claude's tool-routing
 * judgment on the *how*, not its naming creativity.
 */

import type { Scenario } from "../helpers/requirement.js";
import {
  actorWithLabelExists,
  countActorsWithLabel,
  actorOfClassExists,
  outdoorLightingPresent,
  wallAndFloorMaterialsDiffer,
  blueprintAtPath,
  uassetExists,
  characterBpHasAiControllerWired,
  behaviorTreePathFromCharacter,
  behaviorTreeContainsNodeClass,
  screenshotIsNotBlank,
  findLatestScreenshotSince,
} from "../helpers/validators.js";

const ENEMY_BP_PATH = "/Game/E2E/Tiny/BP_E2EEnemy_Tiny";
const FLOOR_LABEL = "E2E_TinyArena_Floor";
const WALL_LABEL = "E2E_TinyArena_Wall_";
const ENEMY_LABEL = "E2E_TinyArena_Enemy";

// Captured at module-load time so the screenshot search ignores anything older.
const SCENARIO_LOAD_MS = Date.now();

export const tinyArena: Scenario = {
  id: "tiny-arena",
  name: "Tiny Arena",
  assetCleanupRoot: "/Game/E2E",
  actorPrefix: "E2E_TinyArena_",

  prompt: `You are running inside an automated end-to-end test for the Arbor MCP toolset.
Build a small enclosed test arena in the currently open level. Use the ue5_* MCP
tools — do NOT ask the user for input, do NOT pause, finish in one pass.

Required setup:

1. A floor roughly 20x20 metres centred at the world origin (z = 0).
   - Actor label: "E2E_TinyArena_Floor"

2. Four walls forming a closed perimeter around the floor, ~3 metres tall.
   - Actor labels: "E2E_TinyArena_Wall_N", "E2E_TinyArena_Wall_E",
     "E2E_TinyArena_Wall_S", "E2E_TinyArena_Wall_W"

3. A material applied to the walls that is visually distinct from the floor's
   material (any colour difference is fine — the goal is that wall material
   path != floor material path).

4. Outdoor scene lighting (directional sun + sky). Use the lighting tool.

5. A NavMesh volume covering the floor.

6. One enemy character Blueprint:
   - Asset path: "${ENEMY_BP_PATH}"
   - Has an AIController whose Behavior Tree just runs a single BTTask_Wait task
     (any wait time is fine).
   - Spawn ONE instance inside the arena, label "${ENEMY_LABEL}_01".

7. After everything is built, take an orbit screenshot of the arena (target
   the world origin, distance ~2000, angle ~45). Report the screenshot file
   path in your final summary.

When done, write a short summary listing what you built and the screenshot path.
Do not file any GitHub issues. Do not run a playtest. Do not modify anything
outside the "/Game/E2E/" content directory or actors with the "E2E_TinyArena_"
prefix.`,

  requirements: [
    {
      id: "floor-exists",
      name: "Floor actor exists",
      category: "actors",
      check: () => actorWithLabelExists(FLOOR_LABEL),
    },
    {
      id: "four-walls",
      name: "≥4 wall actors placed",
      category: "actors",
      check: () => countActorsWithLabel(WALL_LABEL, 4),
    },
    {
      id: "wall-floor-materials-differ",
      name: "Wall material is distinct from floor material",
      category: "materials",
      check: () => wallAndFloorMaterialsDiffer(FLOOR_LABEL, WALL_LABEL),
    },
    {
      id: "outdoor-lighting",
      name: "Outdoor lighting (DirectionalLight + Sky) present",
      category: "lighting",
      check: () => outdoorLightingPresent(),
    },
    {
      id: "navmesh-volume",
      name: "NavMeshBoundsVolume present",
      category: "actors",
      check: () => actorOfClassExists("NavMeshBoundsVolume"),
    },
    {
      id: "enemy-bp-on-disk",
      name: `Enemy BP exists on disk at ${ENEMY_BP_PATH}`,
      category: "blueprint",
      check: () => uassetExists(ENEMY_BP_PATH),
    },
    {
      id: "enemy-bp-queryable",
      name: "Enemy BP loadable via blueprint.query",
      category: "blueprint",
      check: () => blueprintAtPath(ENEMY_BP_PATH),
    },
    {
      id: "enemy-bp-ai-wired",
      name: "Enemy BP has AIControllerClass set on CDO",
      category: "blueprint",
      check: () => characterBpHasAiControllerWired(ENEMY_BP_PATH),
    },
    {
      id: "bt-has-wait",
      name: "Behavior Tree contains a BTTask_Wait node",
      category: "ai",
      check: async () => {
        // Fallback root narrowed to the scenario's BT folder. Arbor's canonical
        // AIController doesn't expose `default_behavior_tree`, so the resolver
        // also walks the AIController BP's event graph and (last resort) takes
        // the only BT in this folder.
        const btPath = await behaviorTreePathFromCharacter(
          ENEMY_BP_PATH,
          "/Game/E2E/Tiny"
        );
        if (!btPath) {
          return {
            passed: false,
            detail:
              "no BT could be resolved from Character BP -> AIController (CDO, event graph, or folder scan)",
            observed: { resolved_bt_path: null },
          };
        }
        return behaviorTreeContainsNodeClass(btPath, "Wait");
      },
    },
    {
      id: "enemy-instance-spawned",
      name: "≥1 enemy instance spawned in level",
      category: "actors",
      check: () => countActorsWithLabel(ENEMY_LABEL, 1),
    },
    {
      id: "orbit-screenshot",
      name: "Orbit screenshot was captured and is not blank",
      category: "capture",
      check: async () => {
        const path = await findLatestScreenshotSince(SCENARIO_LOAD_MS);
        return screenshotIsNotBlank(path);
      },
    },
  ],
};
