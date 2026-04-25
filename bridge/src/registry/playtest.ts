import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

async function runPythonCode(code: string): Promise<{ success: boolean; result?: unknown; error?: string }> {
  const { runPython } = await import("../tools/core/run-python.js");
  return runPython({ code });
}

export const playtestTool: CategoryTool = {
  readOnlyActions: ["is_running", "player_info", "framerate", "can_reach", "get_held_keys"],

  description:
    "PIE playtest control and player input: start/stop PIE, get player info, teleport, check framerate, " +
    "simulate keyboard/mouse input, walk to locations, navigate paths.",

  actionParams: {
    start_pie: { summary: "Start Play-in-Editor session" },
    stop_pie: { summary: "Stop Play-in-Editor session" },
    is_running: { summary: "Check if PIE is currently running" },
    player_info: { summary: "Get player pawn position, rotation, and info" },
    teleport: {
      summary: "Teleport player to position",
      optional: ["x", "y", "z", "target"],
    },
    framerate: { summary: "Get current framerate" },
    can_reach: {
      summary: "Check if player can pathfind to target",
      optional: ["target_x", "target_y", "target_z", "target"],
    },
    press_key: {
      summary: "Press and hold a key",
      required: ["key"],
    },
    release_key: {
      summary: "Release a held key",
      required: ["key"],
    },
    release_all: { summary: "Release all held keys" },
    tap_key: {
      summary: "Tap a key briefly",
      required: ["key"],
      optional: ["duration"],
    },
    hold_key: {
      summary: "Hold a key for specified duration",
      required: ["key", "duration"],
    },
    look: {
      summary: "Look in direction (instant rotation)",
      optional: ["yaw_delta", "pitch_delta"],
    },
    smooth_look: {
      summary: "Smoothly look in direction over duration",
      optional: ["yaw_delta", "pitch_delta", "duration"],
    },
    move: {
      summary: "Move in direction for duration",
      required: ["direction", "duration"],
    },
    move_to: {
      summary: "Move to target location with optional arrival screenshot",
      required: ["target"],
      optional: ["arrive_radius", "duration", "screenshot_on_arrive"],
    },
    navigate_path: {
      summary: "Navigate waypoint path with optional arrival screenshots",
      required: ["waypoints"],
      optional: ["arrive_radius", "duration", "screenshot_on_arrive"],
    },
    jump: { summary: "Make player jump" },
    interact: { summary: "Perform interaction (press E)" },
    get_held_keys: { summary: "Get list of currently held keys" },
  },

  schema: {
    // input
    key: z.string().optional().describe("Key name: w, a, s, d, space, shift, e, f, lmb, rmb, etc."),
    duration: z.number().optional().describe("Duration in seconds (tap_key, hold_key, move, smooth_look)"),
    direction: z.enum(["forward", "backward", "left", "right", "forward_left", "forward_right", "backward_left", "backward_right"])
      .optional().describe("Movement direction (move)"),
    yaw_delta: z.number().optional().describe("Horizontal rotation degrees (look/smooth_look)"),
    pitch_delta: z.number().optional().describe("Vertical rotation degrees (look/smooth_look)"),
    target: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional()
      .describe("World target location (move_to, teleport)"),
    waypoints: z.array(z.object({ x: z.number(), y: z.number(), z: z.number() })).optional()
      .describe("Waypoint list (navigate_path)"),
    arrive_radius: z.number().optional().describe("Arrival threshold cm (move_to/navigate_path). Default 100"),
    screenshot_on_arrive: z.boolean().optional().describe("Screenshot at each waypoint arrival"),
    // teleport
    x: z.number().optional().describe("Teleport X"),
    y: z.number().optional().describe("Teleport Y"),
    z: z.number().optional().describe("Teleport Z"),
    // can_reach
    target_x: z.number().optional().describe("Target X (can_reach)"),
    target_y: z.number().optional().describe("Target Y (can_reach)"),
    target_z: z.number().optional().describe("Target Z (can_reach)"),
  },

  actions: {
    async start_pie() { return callArborJson("ArborPlaytestTools", "StartPIE", {}); },
    async stop_pie() { return callArborJson("ArborPlaytestTools", "StopPIE", {}); },
    async is_running() { return callArborJson("ArborPlaytestTools", "IsPIERunning", {}); },
    async player_info() { return callArborJson("ArborPlaytestTools", "GetPlayerInfo", {}); },

    async teleport(p) {
      return callArborJson("ArborPlaytestTools", "TeleportPlayer", {
        ParamsJson: JSON.stringify({
          x: p.x ?? (p.target as { x: number })?.x ?? 0,
          y: p.y ?? (p.target as { y: number })?.y ?? 0,
          z: p.z ?? (p.target as { z: number })?.z ?? 0,
        }),
      });
    },

    async framerate() { return callArborJson("ArborPlaytestTools", "GetFramerate", {}); },

    async can_reach(p) {
      return callArborJson("ArborPlaytestTools", "CheckPlayerCanReach", {
        ParamsJson: JSON.stringify({
          x: p.target_x ?? (p.target as { x: number })?.x ?? 0,
          y: p.target_y ?? (p.target as { y: number })?.y ?? 0,
          z: p.target_z ?? (p.target as { z: number })?.z ?? 0,
        }),
      });
    },

    // --- Python-based input actions ---
    async press_key(p) {
      if (!p.key) throw new Error("key required");
      return runInputAction(`arbor.input.press_key(${JSON.stringify(p.key)})`);
    },
    async release_key(p) {
      if (!p.key) throw new Error("key required");
      return runInputAction(`arbor.input.release_key(${JSON.stringify(p.key)})`);
    },
    async release_all() {
      return runInputAction(`arbor.input.release_all_keys()`);
    },
    async tap_key(p) {
      if (!p.key) throw new Error("key required");
      return runInputAction(`arbor.input.tap_key(${JSON.stringify(p.key)}, ${p.duration ?? 0.1})`);
    },
    async hold_key(p) {
      if (!p.key) throw new Error("key required");
      if (p.duration === undefined) throw new Error("duration required for hold_key");
      return runInputAction(`arbor.input.hold_key(${JSON.stringify(p.key)}, ${p.duration})`);
    },
    async look(p) {
      return runInputAction(`arbor.input.look_direction(yaw_delta=${p.yaw_delta ?? 0}, pitch_delta=${p.pitch_delta ?? 0})`);
    },
    async smooth_look(p) {
      return runInputAction(`arbor.input.smooth_look(yaw_delta=${p.yaw_delta ?? 0}, pitch_delta=${p.pitch_delta ?? 0}, duration=${p.duration ?? 1.0})`);
    },
    async move(p) {
      if (!p.direction) throw new Error("direction required");
      if (p.duration === undefined) throw new Error("duration required for move");
      return runInputAction(`arbor.input.move_direction(${JSON.stringify(p.direction)}, ${p.duration})`);
    },
    async move_to(p) {
      if (!p.target) throw new Error("target required");
      const t = p.target as { x: number; y: number; z: number };
      return runInputAction(
        `arbor.input.move_to((${t.x}, ${t.y}, ${t.z}), arrive_radius=${p.arrive_radius ?? 100}, timeout=${p.duration ?? 30}, screenshot_on_arrive=${p.screenshot_on_arrive ? "True" : "False"})`
      );
    },
    async navigate_path(p) {
      if (!p.waypoints || !(p.waypoints as unknown[]).length) throw new Error("waypoints required");
      const wps = JSON.stringify((p.waypoints as Array<{ x: number; y: number; z: number }>).map(w => [w.x, w.y, w.z]));
      return runInputAction(
        `arbor.input.navigate_path(${wps}, arrive_radius=${p.arrive_radius ?? 100}, timeout_per_waypoint=${p.duration ?? 30}, screenshot_at_waypoints=${p.screenshot_on_arrive ? "True" : "False"})`
      );
    },
    async jump() { return runInputAction(`arbor.input.jump()`); },
    async interact() { return runInputAction(`arbor.input.interact()`); },
    async get_held_keys() { return runInputAction(`arbor.input.get_held_keys()`); },
  },
};

async function runInputAction(pythonCall: string): Promise<unknown> {
  const code = `\nimport arbor.input\n${pythonCall}\n`;
  const result = await runPythonCode(code);
  if (!result.success) {
    return { success: false, error: result.error || "Python execution failed" };
  }
  const data = result.result as Record<string, unknown> | undefined;
  if (data && data.success === false) {
    return { success: false, error: (data.error as string) || "Input action failed" };
  }
  return data ?? { success: true };
}
