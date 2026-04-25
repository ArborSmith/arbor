import { z } from "zod";
import { readFile, stat } from "node:fs/promises";
import { exec } from "node:child_process";
import { callArborJson } from "../ue5-client.js";
import { startAnnotationServer, getLastAnnotationUrl } from "../annotation-server.js";
import type { CategoryTool } from "./types.js";

// Import runPython from the old tool file — we'll inline it after migration
async function runPythonCode(code: string): Promise<{ success: boolean; result?: unknown; error?: string }> {
  // Dynamic import to avoid circular deps during transition
  const { runPython } = await import("../tools/core/run-python.js");
  return runPython({ code });
}

async function waitForScreenshotFile(
  filePath: string, timeoutMs = 15_000, pollMs = 250, minBytes = 1000,
): Promise<Buffer | null> {
  const start = Date.now();
  let lastSize = -1;
  while (Date.now() - start < timeoutMs) {
    await new Promise((r) => setTimeout(r, pollMs));
    try {
      const info = await stat(filePath);
      if (info.size >= minBytes) {
        if (info.size === lastSize) return await readFile(filePath);
        lastSize = info.size;
      }
    } catch { lastSize = -1; }
  }
  try {
    const info = await stat(filePath);
    if (info.size >= minBytes) return await readFile(filePath);
  } catch { /* nothing */ }
  return null;
}

export const captureTool: CategoryTool = {
  readOnlyActions: ["get_camera"],

  description:
    "Viewport capture and camera control: take screenshots (current/top-down/orbit/custom view), " +
    "get/set editor camera position, annotate screenshots with paint-over markup.",

  actionParams: {
    screenshot: {
      summary: "Take viewport screenshot in specified view mode",
      optional: ["view", "x", "y", "z", "pitch", "yaw", "roll", "target_x", "target_y", "target_z", "distance", "angle", "height"],
    },
    get_camera: {
      summary: "Get current editor camera position and rotation",
    },
    set_camera: {
      summary: "Set editor camera position and rotation",
      optional: ["x", "y", "z", "pitch", "yaw", "roll"],
    },
    capture_from_position: {
      summary: "Capture screenshot from specified camera position",
      optional: ["x", "y", "z", "pitch", "yaw", "roll"],
    },
    annotate: {
      summary: "Open paint-over annotation tool on screenshot",
      optional: ["screenshot_path"],
    },
  },

  schema: {
    view: z.enum(["current", "top_down", "orbit", "custom"]).optional()
      .describe("Screenshot view mode (screenshot). Default: current"),
    x: z.number().optional().describe("Camera X position"),
    y: z.number().optional().describe("Camera Y position"),
    z: z.number().optional().describe("Camera Z position"),
    pitch: z.number().optional().describe("Camera pitch (degrees)"),
    yaw: z.number().optional().describe("Camera yaw (degrees)"),
    roll: z.number().optional().describe("Camera roll (degrees)"),
    target_x: z.number().optional().describe("Look-at target X (orbit/top_down)"),
    target_y: z.number().optional().describe("Look-at target Y (orbit/top_down)"),
    target_z: z.number().optional().describe("Look-at target Z (orbit/top_down)"),
    distance: z.number().optional().describe("Orbit distance from target (cm). Default 2000"),
    angle: z.number().optional().describe("Orbit elevation angle (degrees). Default 45"),
    height: z.number().optional().describe("Top-down height above target (cm). Default 5000"),
    screenshot_path: z.string().optional()
      .describe("Path to existing screenshot to annotate (annotate action). If omitted, takes a fresh screenshot first."),
  },

  actions: {
    async screenshot(p) {
      const view = (p.view as string) ?? "current";
      let pythonCall: string;

      switch (view) {
        case "current":
          pythonCall = "arbor.capture.take_screenshot()"; break;
        case "top_down": {
          const tx = p.target_x ?? 0, ty = p.target_y ?? 0, tz = p.target_z ?? 0;
          pythonCall = `arbor.capture.take_screenshot_top_down(center=(${tx}, ${ty}, ${tz}), height=${p.height ?? 5000})`;
          break;
        }
        case "orbit": {
          const tx = p.target_x ?? 0, ty = p.target_y ?? 0, tz = p.target_z ?? 0;
          pythonCall = `arbor.capture.take_screenshot_orbit(target=(${tx}, ${ty}, ${tz}), distance=${p.distance ?? 2000}, angle=${p.angle ?? 45})`;
          break;
        }
        case "custom": {
          pythonCall = `arbor.capture.take_screenshot_from((${p.x ?? 0}, ${p.y ?? 0}, ${p.z ?? 0}), (${p.pitch ?? 0}, ${p.yaw ?? 0}, ${p.roll ?? 0}))`;
          break;
        }
        default:
          pythonCall = "arbor.capture.take_screenshot()";
      }

      const code = `
import importlib
import arbor.capture
importlib.reload(arbor.capture)

path = ${pythonCall}
if path:
    _write_result({"success": True, "screenshot_path": path})
else:
    _write_result({"success": False, "error": "Screenshot capture failed"})
`;

      const pyResult = await runPythonCode(code);
      if (!pyResult.success || !pyResult.result) {
        return {
          content: [{ type: "text", text: JSON.stringify({ success: false, error: "Screenshot Python failed" }) }],
          isError: true,
        };
      }

      const data = pyResult.result as { success: boolean; screenshot_path?: string; error?: string };
      if (!data.success || !data.screenshot_path) {
        return {
          content: [{ type: "text", text: JSON.stringify({ success: false, error: data.error ?? "No file produced" }) }],
          isError: true,
        };
      }

      const imageBuffer = await waitForScreenshotFile(data.screenshot_path);
      if (!imageBuffer) {
        return {
          content: [{ type: "text", text: JSON.stringify({ success: false, error: `File not written: ${data.screenshot_path}` }) }],
          isError: true,
        };
      }

      const base64 = imageBuffer.toString("base64");
      const mimeType = data.screenshot_path.endsWith(".jpg") || data.screenshot_path.endsWith(".jpeg")
        ? "image/jpeg" : "image/png";

      return {
        content: [
          { type: "image", data: base64, mimeType },
          { type: "text", text: JSON.stringify({ success: true, screenshot_path: data.screenshot_path }, null, 2) },
        ],
      };
    },

    async get_camera() {
      return callArborJson("ArborCaptureTools", "GetViewportCamera", {});
    },

    async set_camera(p) {
      return callArborJson("ArborCaptureTools", "SetViewportCamera", {
        ParamsJson: JSON.stringify({
          x: p.x, y: p.y, z: p.z, pitch: p.pitch, yaw: p.yaw, roll: p.roll,
        }),
      });
    },

    async capture_from_position(p) {
      return callArborJson("ArborCaptureTools", "CaptureFromPosition", {
        ParamsJson: JSON.stringify({
          x: p.x ?? 0, y: p.y ?? 0, z: p.z ?? 0,
          pitch: p.pitch ?? 0, yaw: p.yaw ?? 0, roll: p.roll ?? 0,
        }),
      });
    },

    async annotate(p) {
      let originalPath = p.screenshot_path as string | undefined;

      // Take a fresh screenshot if no path provided
      if (!originalPath) {
        const code = `
import importlib
import arbor.capture
importlib.reload(arbor.capture)

path = arbor.capture.take_screenshot()
if path:
    _write_result({"success": True, "screenshot_path": path})
else:
    _write_result({"success": False, "error": "Screenshot capture failed"})
`;
        const pyResult = await runPythonCode(code);
        if (!pyResult.success || !pyResult.result) {
          return {
            content: [{ type: "text", text: JSON.stringify({ success: false, error: "Screenshot failed" }) }],
            isError: true,
          };
        }
        const data = pyResult.result as { success: boolean; screenshot_path?: string; error?: string };
        if (!data.success || !data.screenshot_path) {
          return {
            content: [{ type: "text", text: JSON.stringify({ success: false, error: data.error ?? "No file" }) }],
            isError: true,
          };
        }
        // Wait for file to be fully written
        const buf = await waitForScreenshotFile(data.screenshot_path);
        if (!buf) {
          return {
            content: [{ type: "text", text: JSON.stringify({ success: false, error: "Screenshot file not written" }) }],
            isError: true,
          };
        }
        originalPath = data.screenshot_path;
      }

      // Start annotation server and open browser
      const annotationPromise = startAnnotationServer(originalPath);

      // Brief delay to let server start listening
      await new Promise((r) => setTimeout(r, 100));
      const url = getLastAnnotationUrl();
      if (url) {
        exec(`start "" "${url}"`);
      }

      try {
        const { annotatedPath } = await annotationPromise;

        // Read both images
        const [originalBuffer, annotatedBuffer] = await Promise.all([
          readFile(originalPath),
          readFile(annotatedPath),
        ]);

        const originalBase64 = originalBuffer.toString("base64");
        const annotatedBase64 = annotatedBuffer.toString("base64");
        const originalMime = originalPath.endsWith(".png") ? "image/png" : "image/jpeg";

        return {
          content: [
            { type: "text", text: "Original screenshot:" },
            { type: "image", data: originalBase64, mimeType: originalMime },
            { type: "text", text: "User's paint-over annotations:" },
            { type: "image", data: annotatedBase64, mimeType: "image/jpeg" },
            { type: "text", text: JSON.stringify({ success: true, original_path: originalPath, annotated_path: annotatedPath }) },
          ],
        };
      } catch (err) {
        return {
          content: [{ type: "text", text: JSON.stringify({ success: false, error: String(err) }) }],
          isError: true,
        };
      }
    },
  },
};
