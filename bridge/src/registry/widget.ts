import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const widgetTool: CategoryTool = {
  description:
    "UMG Widget Blueprint authoring (the widget tree): create a WidgetBlueprint subclass of a " +
    "UUserWidget/UCommonActivatableWidget, build/query/edit the widget hierarchy, set widget and " +
    "slot properties (including brush images from textures), set the root, discover widget types, " +
    "and compile. Event-graph wiring (e.g. PlayAnimation on an event) goes through ue5_blueprint; " +
    "animations go through ue5_widget_animation.",

  actionParams: {
    create: {
      summary: "Create a WidgetBlueprint, optionally building the whole tree",
      required: ["name"],
      optional: ["parent_class", "content_path", "tree"],
    },
    query: {
      summary: "Query widget tree, animations, and parent-class BindWidget properties",
      required: ["asset_path"],
    },
    add_widget: {
      summary: "Add one widget under a named parent panel",
      required: ["asset_path", "widget_spec"],
    },
    remove_widget: {
      summary: "Remove a widget (and its children) by name",
      required: ["asset_path", "widget_name"],
    },
    set_widget_property: {
      summary: "Set widget properties (Text, brush image, color, Percent, Visibility, slot via __slot)",
      required: ["asset_path", "widget_name", "property_spec"],
    },
    set_root: {
      summary: "Set the tree root to an existing widget",
      required: ["asset_path", "widget_name"],
    },
    list_widget_types: {
      summary: "List available UWidget classes (never guess)",
      optional: ["filter"],
    },
    compile: {
      summary: "Compile + save; surfaces BindWidget/compile errors verbatim",
      required: ["asset_path"],
    },
  },

  readOnlyActions: ["query", "list_widget_types"],

  schema: {
    asset_path: z.string().optional().describe("Content path to the WidgetBlueprint asset"),
    name: z.string().optional().describe("Widget Blueprint name (create)"),
    parent_class: z.string().optional().describe(
      "Parent UUserWidget subclass by short name or /Script path (create). Default UserWidget."
    ),
    content_path: z.string().optional().describe("Content folder (create). Default /Game/UI/Widgets"),
    tree: z.array(z.record(z.unknown())).optional().describe(
      "Widget specs in order (parents before children). Each: " +
      "{name, type, parent?, root?, is_variable?, properties?, slot_properties?}"
    ),
    widget_spec: z.record(z.unknown()).optional().describe(
      "Single widget spec (add_widget): {name, type, parent?, root?, is_variable?, properties?, slot_properties?}"
    ),
    widget_name: z.string().optional().describe("Target widget name (remove_widget, set_widget_property, set_root)"),
    property_spec: z.record(z.unknown()).optional().describe(
      "Object of {PropertyName: value} (set_widget_property). Brush image: " +
      '{"Brush":{"image":"/Game/UI/T_Panel","draw_as":"Box","image_size":{"x":256,"y":64}}}. ' +
      'Slot fields: {"__slot":{...}}'
    ),
    filter: z.string().optional().describe("Substring filter for class names (list_widget_types)"),
  },

  actions: {
    async create(p) {
      if (!p.name) throw new Error("name required");
      const contentPath = (p.content_path as string) || "/Game/UI/Widgets";
      const spec: Record<string, unknown> = {
        name: p.name,
        parent_class: (p.parent_class as string) || "UserWidget",
        content_path: contentPath,
      };
      if (p.tree) spec.tree = p.tree;
      return callArborJson("WidgetBlueprintBuilder", "BuildWidgetFromJSONString", {
        JsonString: JSON.stringify(spec),
        AssetPath: contentPath,
      });
    },

    async query(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("WidgetBlueprintBuilder", "QueryWidget", { AssetPath: p.asset_path });
    },

    async add_widget(p) {
      if (!p.asset_path || !p.widget_spec) throw new Error("asset_path, widget_spec required");
      return callArborJson("WidgetBlueprintBuilder", "AddWidget", {
        AssetPath: p.asset_path,
        WidgetJsonString: JSON.stringify(p.widget_spec),
      });
    },

    async remove_widget(p) {
      if (!p.asset_path || !p.widget_name) throw new Error("asset_path, widget_name required");
      return callArborJson("WidgetBlueprintBuilder", "RemoveWidget", {
        AssetPath: p.asset_path,
        WidgetName: p.widget_name,
      });
    },

    async set_widget_property(p) {
      if (!p.asset_path || !p.widget_name || !p.property_spec)
        throw new Error("asset_path, widget_name, property_spec required");
      return callArborJson("WidgetBlueprintBuilder", "SetWidgetProperty", {
        AssetPath: p.asset_path,
        WidgetName: p.widget_name,
        PropertyJsonString: JSON.stringify(p.property_spec),
      });
    },

    async set_root(p) {
      if (!p.asset_path || !p.widget_name) throw new Error("asset_path, widget_name required");
      return callArborJson("WidgetBlueprintBuilder", "SetRootWidget", {
        AssetPath: p.asset_path,
        WidgetName: p.widget_name,
      });
    },

    async list_widget_types(p) {
      return callArborJson("WidgetBlueprintBuilder", "ListWidgetTypes", {
        Filter: (p.filter as string) ?? "",
      });
    },

    async compile(p) {
      if (!p.asset_path) throw new Error("asset_path required");
      return callArborJson("WidgetBlueprintBuilder", "CompileAndSaveWidget", { AssetPath: p.asset_path });
    },
  },
};
