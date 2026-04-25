import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const terrainTool: CategoryTool = {
  readOnlyActions: ["sample_height", "find_highest", "find_lowest", "find_flat"],

  description:
    "Landscape creation and queries: create procedural terrain with noise, add rivers/lakes, paint layers, " +
    "sample heights, find elevation extremes, find flat areas.",

  actionParams: {
    create: {
      summary: "Create procedural terrain with noise parameters and optional rivers/layers",
      optional: ["preset", "location", "component_count", "scale", "frequency", "amplitude", "octaves", "seed", "noise_type", "material_path", "river", "river_width", "auto_paint", "layers"],
    },
    sample_height: {
      summary: "Sample terrain height at world coordinates",
      required: ["x", "y"],
    },
    find_highest: {
      summary: "Find highest point on terrain",
      optional: ["region"],
    },
    find_lowest: {
      summary: "Find lowest point on terrain",
      optional: ["region"],
    },
    find_flat: {
      summary: "Find flat area with minimum radius",
      optional: ["min_radius", "region"],
    },
    paint_layer: {
      summary: "Auto-paint terrain layers with rules",
      optional: ["rules_json", "layers", "seed"],
    },
    paint_circle: {
      summary: "Paint layer in circular area",
      required: ["layer_name"],
      optional: ["center_x", "center_y", "radius_frac", "strength"],
    },
    add_river: {
      summary: "Add river water body along spline points",
      optional: ["points", "width", "depth", "label"],
    },
    add_lake: {
      summary: "Add lake water body as closed polygon",
      optional: ["points", "width", "depth", "label"],
    },
  },

  schema: {
    // create
    preset: z.enum(["rolling_hills", "mountains", "flat"]).optional().describe("Terrain preset. Default: rolling_hills"),
    location: z.object({ x: z.number().optional(), y: z.number().optional(), z: z.number().optional() }).optional().describe("World position for landscape origin"),
    component_count: z.number().optional().describe("Components per axis. 8=505x505 verts, 16=1009x1009"),
    scale: z.object({ x: z.number().optional(), y: z.number().optional(), z: z.number().optional() }).optional().describe("Landscape scale. Z controls height range"),
    frequency: z.number().optional().describe("Noise frequency"),
    amplitude: z.number().optional().describe("Height variation 0.0-1.0"),
    octaves: z.number().optional().describe("Noise detail layers 1-8"),
    seed: z.number().optional().describe("Random seed"),
    noise_type: z.string().optional().describe("Noise type: fbm or ridge"),
    material_path: z.string().optional().describe("Landscape material content path"),
    river: z.boolean().optional().describe("Add procedural river (requires Water Plugin)"),
    river_width: z.number().optional().describe("River width in cm. Default 500"),
    auto_paint: z.boolean().optional().describe("Auto-paint layers after creation"),
    layers: z.array(z.object({
      name: z.string(),
      min_height: z.number().optional(), max_height: z.number().optional(),
      min_slope: z.number().optional(), max_slope: z.number().optional(),
      falloff: z.number().optional(),
    })).optional().describe("Layer painting rules for auto_paint"),
    // queries
    x: z.number().optional().describe("World X coordinate (sample_height)"),
    y: z.number().optional().describe("World Y coordinate (sample_height)"),
    min_radius: z.number().optional().describe("Minimum flat area radius in cm (find_flat). Default 500"),
    landscape: z.string().optional().describe("Landscape actor label. Auto-detects if omitted"),
    region: z.object({
      min_x: z.number(), min_y: z.number(), max_x: z.number(), max_y: z.number(),
    }).optional().describe("Bounding box to restrict search area"),
    // paint_layer
    layer_name: z.string().optional().describe("Layer name (paint_layer, paint_circle)"),
    rules_json: z.string().optional().describe("Layer rules JSON (paint_layer)"),
    // paint_circle
    center_x: z.number().optional().describe("Circle center X (paint_circle)"),
    center_y: z.number().optional().describe("Circle center Y (paint_circle)"),
    radius_frac: z.number().optional().describe("Circle radius as fraction of landscape (paint_circle)"),
    strength: z.number().optional().describe("Paint strength 0-1 (paint_circle)"),
    // water
    points: z.array(z.object({ x: z.number(), y: z.number(), z: z.number().optional() })).optional()
      .describe("Spline points for river/lake"),
    width: z.number().optional().describe("River/lake width"),
    depth: z.number().optional().describe("Water depth"),
    label: z.string().optional().describe("Water body actor label"),
  },

  actions: {
    async create(p) {
      const preset = (p.preset as string) || "rolling_hills";
      const loc = (p.location as { x?: number; y?: number; z?: number }) || {};
      const scl = (p.scale as { x?: number; y?: number; z?: number }) || {};

      let freq = p.frequency as number | undefined;
      let amp = p.amplitude as number | undefined;
      let noiseType = (p.noise_type as string) || "fbm";

      if (preset === "rolling_hills") {
        if (freq === undefined) freq = 4.0;
        if (amp === undefined) amp = 0.5;
        noiseType = "fbm";
      } else if (preset === "mountains") {
        if (freq === undefined) freq = 3.0;
        if (amp === undefined) amp = 0.8;
        noiseType = "ridge";
      } else if (preset === "flat") {
        freq = 1.0; amp = 0.0; noiseType = "fbm";
      }

      const defaultLayers = [
        { name: "Grass", max_slope: 25, max_height: 0.65, falloff: 0.1 },
        { name: "Dirt", max_slope: 40, falloff: 0.15 },
        { name: "Rock", min_slope: 20, falloff: 0.1 },
      ];

      return callArborJson("LandscapeBuilder", "CreateTerrainPipeline", {
        ParamsJson: JSON.stringify({
          location: [loc.x || 0, loc.y || 0, loc.z || 0],
          component_count: p.component_count || 8,
          scale: [scl.x || 100, scl.y || 100, scl.z || 100],
          frequency: freq || 4.0,
          amplitude: amp ?? 0.5,
          octaves: p.octaves || 4,
          seed: p.seed,
          noise_type: noiseType,
          material_path: p.material_path,
          river: p.river || false,
          river_width: p.river_width || 500,
          auto_paint: p.auto_paint || false,
          layers: p.layers || defaultLayers,
        }),
      });
    },

    async sample_height(p) {
      if (p.x === undefined || p.y === undefined) throw new Error("x, y required");
      return callArborJson("ArborActorTools", "SampleTerrainHeight", { X: p.x, Y: p.y });
    },

    async find_highest(p) {
      const regionJson = p.region ? JSON.stringify(p.region) : "";
      return callArborJson("LandscapeBuilder", "FindExtremePoint", {
        Landscape: null, Mode: "max", RegionJson: regionJson,
      });
    },

    async find_lowest(p) {
      const regionJson = p.region ? JSON.stringify(p.region) : "";
      return callArborJson("LandscapeBuilder", "FindExtremePoint", {
        Landscape: null, Mode: "min", RegionJson: regionJson,
      });
    },

    async find_flat(p) {
      const regionJson = p.region ? JSON.stringify(p.region) : "";
      return callArborJson("LandscapeBuilder", "FindFlatArea", {
        Landscape: null, MinRadius: (p.min_radius as number) ?? 500, RegionJson: regionJson,
      });
    },

    async paint_layer(p) {
      return callArborJson("LandscapeBuilder", "AutoPaintLayers", {
        Landscape: null,
        RulesJson: (p.rules_json as string) || JSON.stringify(p.layers || []),
        Seed: (p.seed as number) ?? 0,
        SavePath: "/Game/Landscape",
      });
    },

    async paint_circle(p) {
      if (!p.layer_name) throw new Error("layer_name required");
      return callArborJson("LandscapeBuilder", "PaintLayerCircle", {
        Landscape: null,
        LayerName: p.layer_name,
        CenterX: (p.center_x as number) ?? 0,
        CenterY: (p.center_y as number) ?? 0,
        RadiusFrac: (p.radius_frac as number) ?? 0.1,
        Strength: (p.strength as number) ?? 1.0,
      });
    },

    async add_river(p) {
      return callArborJson("LandscapeBuilder", "AddWaterBodyRiver", {
        ParamsJson: JSON.stringify({
          points: p.points,
          width: p.width ?? 500,
          depth: p.depth ?? 200,
          label: p.label,
        }),
      });
    },

    async add_lake(p) {
      return callArborJson("LandscapeBuilder", "AddWaterBodyLake", {
        ParamsJson: JSON.stringify({
          points: p.points,
          width: p.width ?? 1000,
          depth: p.depth ?? 300,
          label: p.label,
        }),
      });
    },
  },
};
