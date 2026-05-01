import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const actorsTool: CategoryTool = {
  description:
    "Manage actors in the UE5 level: list, inspect, spawn, place, delete, modify, snap to ground, scatter meshes.",

  actionParams: {
    list: { summary: "List all actors in the level" },
    scene_info: {
      summary: "Get scene info with optional class or name prefix filter",
      optional: ["filter_class", "filter_prefix"],
    },
    inspect: {
      summary: "Inspect actor properties and components",
      required: ["actor_name"],
      optional: ["property_filter", "component_filter"],
    },
    spawn_primitive: {
      summary: "Spawn a primitive shape (cube, sphere, cylinder, cone, plane)",
      optional: ["shape", "x", "y", "z", "scale_x", "scale_y", "scale_z", "pitch", "yaw", "roll", "label"],
    },
    spawn_light: {
      summary: "Spawn a light actor (point, spot, directional, rect)",
      optional: ["light_type", "x", "y", "z", "intensity", "color", "attenuation_radius", "label"],
    },
    spawn_navmesh: {
      summary: "Spawn a navigation mesh volume",
      optional: ["x", "y", "z", "extent_x", "extent_y", "extent_z"],
    },
    spawn_class: {
      summary:
        "Spawn an actor of the given UClass at a transform. Use this for any actor type that isn't a primitive/light/navmesh — e.g. /Script/Engine.TargetPoint, /Script/Engine.TriggerVolume, or a Blueprint generated class /Game/BP_Foo.BP_Foo_C (the _C suffix is auto-appended if you omit it). Pitch/yaw/roll are passed as discrete keys, so callers never construct an unreal.Rotator() — avoids the positional-arg footgun where Python's Rotator silently puts values in wrong fields.",
      required: ["class_path"],
      optional: ["x", "y", "z", "pitch", "yaw", "roll", "scale_x", "scale_y", "scale_z", "label"],
    },
    place: {
      summary: "Place an asset from content browser into the level",
      required: ["asset_path"],
      optional: ["x", "y", "z", "pitch", "yaw", "roll", "scale_x", "scale_y", "scale_z"],
    },
    delete: {
      summary: "Delete actors by name",
      required: ["actor_names"],
    },
    modify: {
      summary: "Modify actor position, rotation, scale, visibility, or label",
      required: ["actor_name"],
      optional: ["position", "rotation", "scale", "visible", "label"],
    },
    set_property: {
      summary:
        "Set arbitrary UPROPERTY on an actor by reflection (covers brush extents, gameplay-tag fields, soft refs, etc. that modify doesn't). Marks dirty; follow with ue5_level.save_current to persist.",
      required: ["actor_name", "property_name"],
      optional: ["value"],
    },
    snap_to_ground: {
      summary: "Snap actor to ground with optional offset",
      required: ["actor_name"],
      optional: ["offset", "preserve_rotation", "ignore_labels"],
    },
    snap_all: {
      summary: "Snap all actors (optionally filtered by label) to ground",
      optional: ["filter_labels", "offset"],
    },
    scatter: {
      summary: "Scatter static mesh instances in rectangular bounds",
      required: ["mesh_path"],
      optional: ["count", "min_x", "min_y", "max_x", "max_y", "snap_to_ground", "scale_x", "scale_y", "scale_z"],
    },
    live_compile: { summary: "Trigger live compilation of all blueprints" },
  },

  readOnlyActions: ["list", "inspect", "scene_info"],

  schema: {
    // spawn_primitive
    shape: z
      .enum(["cube", "sphere", "cylinder", "cone", "plane"])
      .optional()
      .describe("Shape for spawn_primitive"),
    // spawn_light
    light_type: z
      .enum(["point", "spot", "directional", "rect"])
      .optional()
      .describe("Light type for spawn_light"),
    intensity: z.number().optional().describe("Light intensity (spawn_light). Default 5000"),
    color: z
      .object({ r: z.number(), g: z.number(), b: z.number() })
      .optional()
      .describe("Light color {r,g,b} 0-1 (spawn_light)"),
    attenuation_radius: z.number().optional().describe("Attenuation radius (spawn_light). Default 1000"),
    // common position/rotation/scale
    x: z.number().optional().describe("X position"),
    y: z.number().optional().describe("Y position"),
    z: z.number().optional().describe("Z position"),
    scale_x: z.number().optional().describe("X scale"),
    scale_y: z.number().optional().describe("Y scale"),
    scale_z: z.number().optional().describe("Z scale"),
    pitch: z.number().optional().describe("Pitch rotation in degrees"),
    yaw: z.number().optional().describe("Yaw rotation in degrees"),
    roll: z.number().optional().describe("Roll rotation in degrees"),
    label: z.string().optional().describe("Actor label for readability"),
    // place
    asset_path: z.string().optional().describe("Content browser path to asset (place action)"),
    // spawn_class
    class_path: z
      .string()
      .optional()
      .describe(
        "Class path for spawn_class. Examples: /Script/Engine.TargetPoint, /Script/Engine.TriggerVolume, /Game/BP_Foo.BP_Foo_C (or just /Game/BP_Foo — _C suffix auto-appended)."
      ),
    // delete
    actor_names: z.array(z.string()).optional().describe("Actor names/paths to delete"),
    // modify, set_property
    actor_name: z.string().optional().describe("Actor name/path/label to target"),
    // set_property
    property_name: z.string().optional().describe("UPROPERTY name on the actor (set_property)"),
    value: z
      .unknown()
      .optional()
      .describe(
        [
          "JSON value for set_property. Encoding by FProperty type:",
          "  bool → true/false ; int32/float/double → number ; FString/FName → \"text\"",
          "  FGameplayTag → \"Quest.A.B\" (must be registered)",
          "  FGameplayTagContainer → [\"Quest.A\", \"Quest.B\"]",
          "  FObjectProperty / FSoftObjectProperty → \"/Game/Path/Asset.Asset\"",
          "  FClassProperty / FSoftClassProperty → \"/Script/Module.ClassName\" or \"/Game/BP_Foo.BP_Foo_C\"",
        ].join("\n")
      ),
    position: z
      .object({ x: z.number(), y: z.number(), z: z.number() })
      .optional()
      .describe("New position (modify)"),
    rotation: z
      .object({ pitch: z.number(), yaw: z.number(), roll: z.number() })
      .optional()
      .describe("New rotation (modify)"),
    scale: z
      .object({ x: z.number(), y: z.number(), z: z.number() })
      .optional()
      .describe("New scale (modify)"),
    visible: z.boolean().optional().describe("Show/hide actor (modify)"),
    // inspect
    property_filter: z.string().optional().describe("Filter properties by name substring (inspect)"),
    component_filter: z.string().optional().describe("Filter components by name/class substring (inspect)"),
    // scene_info filters
    filter_class: z.string().optional().describe("Filter by actor class (scene_info)"),
    filter_prefix: z.string().optional().describe("Filter by name prefix (scene_info)"),
    // navmesh extents
    extent_x: z.number().optional().describe("Half-extent X (spawn_navmesh). Default 2000"),
    extent_y: z.number().optional().describe("Half-extent Y (spawn_navmesh). Default 2000"),
    extent_z: z.number().optional().describe("Half-extent Z (spawn_navmesh). Default 2000"),
    // snap
    offset: z.number().optional().describe("Ground offset (snap_to_ground/snap_all)"),
    preserve_rotation: z.boolean().optional().describe("Preserve rotation during snap. Default true"),
    ignore_labels: z.string().optional().describe("Comma-separated labels to ignore (snap_to_ground)"),
    filter_labels: z.string().optional().describe("Comma-separated labels to snap (snap_all)"),
    // scatter
    mesh_path: z.string().optional().describe("Static mesh path (scatter)"),
    count: z.number().optional().describe("Number of meshes to scatter"),
    min_x: z.number().optional().describe("Scatter bounds min X"),
    min_y: z.number().optional().describe("Scatter bounds min Y"),
    max_x: z.number().optional().describe("Scatter bounds max X"),
    max_y: z.number().optional().describe("Scatter bounds max Y"),
    snap_to_ground: z.boolean().optional().describe("Snap scattered meshes to ground. Default true"),
  },

  actions: {
    async list() {
      return callArborJson("ArborActorTools", "ListAllActors", {});
    },

    async scene_info(p) {
      return callArborJson("ArborActorTools", "GetSceneInfo", {
        FilterClass: (p.filter_class as string) ?? "",
        FilterPrefix: (p.filter_prefix as string) ?? "",
      });
    },

    async inspect(p) {
      if (!p.actor_name) throw new Error("actor_name is required for inspect");
      return callArborJson("ArborActorTools", "InspectActor", {
        ParamsJson: JSON.stringify({
          actor_name: p.actor_name,
          property_filter: p.property_filter,
          component_filter: p.component_filter,
        }),
      });
    },

    async spawn_primitive(p) {
      const cppParams = {
        shape: p.shape,
        x: p.x, y: p.y, z: p.z,
        scale_x: p.scale_x, scale_y: p.scale_y, scale_z: p.scale_z,
        pitch: p.pitch, yaw: p.yaw, roll: p.roll,
        label: p.label,
      };
      return callArborJson("ArborSpawnTools", "SpawnPrimitive", {
        ParamsJson: JSON.stringify(cppParams),
      });
    },

    async spawn_light(p) {
      const cppParams = {
        light_type: p.light_type,
        x: p.x, y: p.y, z: p.z,
        intensity: p.intensity,
        color: p.color,
        attenuation_radius: p.attenuation_radius,
        label: p.label,
      };
      return callArborJson("ArborSpawnTools", "SpawnLight", {
        ParamsJson: JSON.stringify(cppParams),
      });
    },

    async spawn_navmesh(p) {
      return callArborJson("ArborSpawnTools", "SpawnNavMesh", {
        ParamsJson: JSON.stringify({
          x: p.x, y: p.y, z: p.z,
          extent_x: p.extent_x, extent_y: p.extent_y, extent_z: p.extent_z,
        }),
      });
    },

    async spawn_class(p) {
      if (!p.class_path) throw new Error("class_path is required for spawn_class");
      return callArborJson("ArborSpawnTools", "SpawnActorByClass", {
        ParamsJson: JSON.stringify({
          class_path: p.class_path,
          x: p.x, y: p.y, z: p.z,
          pitch: p.pitch, yaw: p.yaw, roll: p.roll,
          scale_x: p.scale_x, scale_y: p.scale_y, scale_z: p.scale_z,
          label: p.label,
        }),
      });
    },

    async place(p) {
      if (!p.asset_path) throw new Error("asset_path is required for place");
      const result = await callArborJson<{
        success: boolean;
        actor_name?: string;
        actor_path?: string;
        location?: { x: number; y: number; z: number };
        error?: string;
      }>("ArborSpawnTools", "PlaceActor", {
        ParamsJson: JSON.stringify({
          asset_path: p.asset_path,
          x: p.x, y: p.y, z: p.z,
          pitch: p.pitch, yaw: p.yaw, roll: p.roll,
          scale_x: p.scale_x, scale_y: p.scale_y, scale_z: p.scale_z,
        }),
      });
      if (!result.success) throw new Error(result.error || "Failed to place actor");
      return result;
    },

    async delete(p) {
      if (!p.actor_names) throw new Error("actor_names is required for delete");
      return callArborJson("ArborActorTools", "DeleteActors", {
        ActorNamesJson: JSON.stringify(p.actor_names),
      });
    },

    async modify(p) {
      if (!p.actor_name) throw new Error("actor_name is required for modify");
      return callArborJson("ArborActorTools", "ModifyActor", {
        ParamsJson: JSON.stringify({
          actor_name: p.actor_name,
          position: p.position,
          rotation: p.rotation,
          scale: p.scale,
          visible: p.visible,
          label: p.label,
        }),
      });
    },

    async set_property(p) {
      if (!p.actor_name) throw new Error("actor_name is required for set_property");
      if (!p.property_name) throw new Error("property_name is required for set_property");
      return callArborJson("ArborActorTools", "SetActorProperty", {
        ActorName: p.actor_name as string,
        PropertyName: p.property_name as string,
        ValueJson: JSON.stringify(p.value),
      });
    },

    async snap_to_ground(p) {
      if (!p.actor_name) throw new Error("actor_name is required for snap_to_ground");
      return callArborJson("ArborActorTools", "SnapToGround", {
        ActorLabel: p.actor_name as string,
        Offset: (p.offset as number) ?? 0,
        PreserveRotation: (p.preserve_rotation as boolean) ?? true,
        IgnoreLabels: (p.ignore_labels as string) ?? "",
      });
    },

    async snap_all(p) {
      return callArborJson("ArborActorTools", "SnapAllToGround", {
        FilterLabels: (p.filter_labels as string) ?? "",
        Offset: (p.offset as number) ?? 0,
      });
    },

    async scatter(p) {
      if (!p.mesh_path) throw new Error("mesh_path is required for scatter");
      return callArborJson("ArborSpawnTools", "ScatterMeshes", {
        ParamsJson: JSON.stringify({
          mesh_path: p.mesh_path,
          count: p.count ?? 10,
          min_x: p.min_x ?? -1000, min_y: p.min_y ?? -1000,
          max_x: p.max_x ?? 1000, max_y: p.max_y ?? 1000,
          snap_to_ground: p.snap_to_ground ?? true,
          scale_x: p.scale_x, scale_y: p.scale_y, scale_z: p.scale_z,
        }),
      });
    },

    async live_compile() {
      return callArborJson("ArborActorTools", "LiveCompile", {});
    },
  },
};
