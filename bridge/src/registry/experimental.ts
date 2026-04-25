import type { CategoryTool } from "./types.js";

import { anchorsTool } from "./anchors.js";
import { environmentTool } from "./environment.js";
import { codexTool } from "./codex.js";
import { conceptArtStudioTool } from "./concept-art-studio.js";
import { pcgTool } from "./pcg.js";

export type ExperimentalFeatureKey =
  | "anchors"
  | "environment"
  | "codex"
  | "concept_art_studio"
  | "pcg";

export interface ExperimentalToolEntry {
  name: string;
  tool: CategoryTool;
  featureKey: ExperimentalFeatureKey;
}

export const EXPERIMENTAL_TOOLS: ExperimentalToolEntry[] = [
  { name: "ue5_anchors", tool: anchorsTool, featureKey: "anchors" },
  { name: "ue5_environment", tool: environmentTool, featureKey: "environment" },
  { name: "ue5_codex", tool: codexTool, featureKey: "codex" },
  { name: "ue5_concept_art_studio", tool: conceptArtStudioTool, featureKey: "concept_art_studio" },
  { name: "ue5_pcg", tool: pcgTool, featureKey: "pcg" },
];
