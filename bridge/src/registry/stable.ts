import type { CategoryTool } from "./types.js";

import { actorsTool } from "./actors.js";
import { blueprintTool } from "./blueprint.js";
import { aiTool } from "./ai.js";
import { terrainTool } from "./terrain.js";
import { materialsTool } from "./materials.js";
import { lightingTool } from "./lighting.js";
import { foliageTool } from "./foliage.js";
import { meshTool } from "./mesh.js";
import { assetsTool } from "./assets.js";
import { captureTool } from "./capture.js";
import { playtestTool } from "./playtest.js";
import { levelTool } from "./level.js";

export interface StableToolEntry {
  name: string;
  tool: CategoryTool;
}

export const STABLE_TOOLS: StableToolEntry[] = [
  { name: "ue5_actors", tool: actorsTool },
  { name: "ue5_blueprint", tool: blueprintTool },
  { name: "ue5_ai", tool: aiTool },
  { name: "ue5_terrain", tool: terrainTool },
  { name: "ue5_materials", tool: materialsTool },
  { name: "ue5_lighting", tool: lightingTool },
  { name: "ue5_foliage", tool: foliageTool },
  { name: "ue5_mesh", tool: meshTool },
  { name: "ue5_assets", tool: assetsTool },
  { name: "ue5_capture", tool: captureTool },
  { name: "ue5_playtest", tool: playtestTool },
  { name: "ue5_level", tool: levelTool },
];
