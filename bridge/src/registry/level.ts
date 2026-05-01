import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const levelTool: CategoryTool = {
  description:
    "Level (map) navigation: load a different map, save the current one, query which level is open. " +
    "Pair with ue5_actors.set_property to do reflection-based actor edits after a load.",

  actionParams: {
    load: {
      summary:
        "Open a different map in the editor. Errors if the current level has unsaved changes unless force=true.",
      required: ["asset_path"],
      optional: ["force"],
    },
    save_current: {
      summary: "Save the active level + its dirty packages.",
    },
    current: {
      summary:
        "Inspect the currently-open level: returns {asset_path, level_name, is_dirty, actor_count}.",
    },
  },

  readOnlyActions: ["current"],

  schema: {
    asset_path: z
      .string()
      .optional()
      .describe("Map content path for `load`, e.g. /Game/Maps/MyLevel"),
    force: z
      .boolean()
      .optional()
      .describe(
        "`load`: discard unsaved changes in current level. Default false (errors if dirty)."
      ),
  },

  actions: {
    async load(p) {
      if (!p.asset_path) throw new Error("asset_path is required for load");
      return callArborJson("ArborLevelTools", "LoadLevel", {
        AssetPath: p.asset_path as string,
        bForce: p.force === true,
      });
    },

    async save_current() {
      return callArborJson("ArborLevelTools", "SaveCurrentLevel", {});
    },

    async current() {
      return callArborJson("ArborLevelTools", "GetCurrentLevel", {});
    },
  },
};
