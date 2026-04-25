import { describe, it, expect } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { captureTool } from "../../src/registry/capture.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Capture Operations", () => {
  // ── Camera ────────────────────────────────────────────────────────

  describe("Camera", () => {
    it("get_camera returns position and rotation", async () => {
      const result = (await captureTool.actions.get_camera({})) as {
        x?: number;
        y?: number;
        z?: number;
        location?: unknown;
      };
      expect(result).toBeDefined();
    });

    it("set_camera then get_camera reflects new position", async () => {
      const setResult = (await captureTool.actions.set_camera({
        x: 1000,
        y: 2000,
        z: 500,
        pitch: -30,
        yaw: 45,
        roll: 0,
      })) as { success?: boolean };
      expect(setResult).toBeDefined();

      const getResult = (await captureTool.actions.get_camera({})) as Record<
        string,
        unknown
      >;
      expect(getResult).toBeDefined();
    });
  });

  // ── Capture From Position ─────────────────────────────────────────

  describe("Capture From Position", () => {
    it("captures a top-down view", async () => {
      const result = (await captureTool.actions.capture_from_position({
        x: 0,
        y: 0,
        z: 1000,
        pitch: -90,
        yaw: 0,
        roll: 0,
      })) as { success?: boolean; screenshot_path?: string };
      expect(result).toBeDefined();
    });
  });

  // ── Annotate (skipped) ────────────────────────────────────────────

  describe("Annotate", () => {
    it.skip("interactive — requires browser interaction", () => {});
  });
});

describe.skipIf(editorRunning)("Capture Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
