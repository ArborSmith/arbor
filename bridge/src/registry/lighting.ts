import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const lightingTool: CategoryTool = {
  description:
    "Scene lighting setup: create outdoor (sun/sky/fog/clouds) or indoor (skylight/rectlight) lighting, " +
    "add post-process volumes with settings.",

  actionParams: {
    setup_outdoor: {
      summary: "Setup outdoor scene with directional light, sky, fog, clouds",
      optional: ["time_of_day", "cloud_coverage", "fog_density"],
    },
    setup_indoor: {
      summary: "Setup indoor scene with skylight and rect light",
      optional: ["sky_light_intensity", "rect_light_intensity"],
    },
    add_post_process: {
      summary: "Add post-process volume with bloom, exposure, vignette, AO, color grading",
      optional: ["x", "y", "z", "extent_x", "extent_y", "extent_z", "infinite", "bloom_intensity", "exposure_compensation", "auto_exposure", "vignette_intensity", "ambient_occlusion_intensity", "color_grading_lut"],
    },
  },

  schema: {
    // outdoor
    time_of_day: z.number().optional().describe("Time of day 0-24 (setup_outdoor). Default 10"),
    cloud_coverage: z.number().optional().describe("Cloud coverage 0-1 (setup_outdoor)"),
    fog_density: z.number().optional().describe("Fog density (setup_outdoor)"),
    // indoor
    sky_light_intensity: z.number().optional().describe("Sky light intensity (setup_indoor)"),
    rect_light_intensity: z.number().optional().describe("Rect light intensity (setup_indoor)"),
    // post process
    x: z.number().optional().describe("Volume X position"),
    y: z.number().optional().describe("Volume Y position"),
    z: z.number().optional().describe("Volume Z position"),
    extent_x: z.number().optional().describe("Volume half-extent X"),
    extent_y: z.number().optional().describe("Volume half-extent Y"),
    extent_z: z.number().optional().describe("Volume half-extent Z"),
    infinite: z.boolean().optional().describe("Infinite extent (unbound). Default true"),
    bloom_intensity: z.number().optional().describe("Bloom intensity"),
    exposure_compensation: z.number().optional().describe("Exposure compensation (EV)"),
    auto_exposure: z.boolean().optional().describe("Enable auto exposure"),
    vignette_intensity: z.number().optional().describe("Vignette intensity 0-1"),
    ambient_occlusion_intensity: z.number().optional().describe("AO intensity"),
    color_grading_lut: z.string().optional().describe("Color grading LUT texture path"),
  },

  actions: {
    async setup_outdoor(p) {
      return callArborJson("ArborLightingTools", "SetupOutdoorScene", {
        ParamsJson: JSON.stringify({
          time_of_day: p.time_of_day, cloud_coverage: p.cloud_coverage,
          fog_density: p.fog_density,
        }),
      });
    },

    async setup_indoor(p) {
      return callArborJson("ArborLightingTools", "SetupIndoorScene", {
        ParamsJson: JSON.stringify({
          sky_light_intensity: p.sky_light_intensity,
          rect_light_intensity: p.rect_light_intensity,
        }),
      });
    },

    async add_post_process(p) {
      return callArborJson("ArborLightingTools", "AddPostProcessVolume", {
        ParamsJson: JSON.stringify({
          x: p.x, y: p.y, z: p.z,
          extent_x: p.extent_x, extent_y: p.extent_y, extent_z: p.extent_z,
          infinite: p.infinite ?? true,
          bloom_intensity: p.bloom_intensity,
          exposure_compensation: p.exposure_compensation,
          auto_exposure: p.auto_exposure,
          vignette_intensity: p.vignette_intensity,
          ambient_occlusion_intensity: p.ambient_occlusion_intensity,
          color_grading_lut: p.color_grading_lut,
        }),
      });
    },
  },
};
