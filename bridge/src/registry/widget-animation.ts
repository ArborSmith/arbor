import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const widgetAnimationTool: CategoryTool = {
  description:
    "UMG widget animations via designer-friendly preset recipes (fade_in, fade_out, slide_in, " +
    "slide_out, pop, scale_in, pulse, strikeoff). One animation 'name' holds one or more tracks, " +
    "so e.g. an 'Intro' = fade_in + slide_in. After authoring, the animation surfaces as a variable " +
    "on the widget - play it from the event graph via ue5_blueprint (UUserWidget::PlayAnimation). " +
    "A low-level add_track hatch exposes raw channel keyframes.",

  actionParams: {
    add_preset: {
      summary: "Add/extend an animation from preset recipes",
      required: ["asset_path", "preset_spec"],
    },
    query: {
      summary: "List animations with durations and bindings",
      required: ["asset_path"],
    },
    remove: {
      summary: "Remove an animation by name",
      required: ["asset_path", "animation_name"],
    },
    add_track: {
      summary: "Low-level: author a track with explicit channel keyframes",
      required: ["asset_path", "track_spec"],
    },
  },

  readOnlyActions: ["query"],

  schema: {
    asset_path: z.string().optional().describe("Content path to the WidgetBlueprint asset"),
    preset_spec: z.record(z.unknown()).optional().describe(
      "Animation spec (add_preset): { name, tracks: [ { preset, target, duration?, " +
      "direction?(left|right|top|bottom), distance?, overshoot? } ] }. " +
      "Presets: fade_in, fade_out, slide_in, slide_out, pop, scale_in, pulse, strikeoff."
    ),
    animation_name: z.string().optional().describe("Animation name (remove)"),
    track_spec: z.record(z.unknown()).optional().describe(
      "Low-level track (add_track): { animation, target, track_type:'float'|'transform2d', " +
      "property?, duration?, channels:[ { component, keys:[ {t, v} ] } ] }. " +
      "transform2d components: TranslationX|TranslationY|ScaleX|ScaleY|Rotation"
    ),
  },

  actions: {
    async add_preset(p) {
      if (!p.asset_path || !p.preset_spec) throw new Error("asset_path, preset_spec required");
      return callArborJson("WidgetAnimationBuilder", "AddAnimationFromPreset", {
        AssetPath: p.asset_path,
        PresetJsonString: JSON.stringify(p.preset_spec),
      });
    },

    async query(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("WidgetAnimationBuilder", "QueryAnimations", { AssetPath: p.asset_path });
    },

    async remove(p) {
      if (!p.asset_path || !p.animation_name) throw new Error("asset_path, animation_name required");
      return callArborJson("WidgetAnimationBuilder", "RemoveAnimation", {
        AssetPath: p.asset_path,
        AnimationName: p.animation_name,
      });
    },

    async add_track(p) {
      if (!p.asset_path || !p.track_spec) throw new Error("asset_path, track_spec required");
      return callArborJson("WidgetAnimationBuilder", "AddAnimationTrack", {
        AssetPath: p.asset_path,
        TrackJsonString: JSON.stringify(p.track_spec),
      });
    },
  },
};
