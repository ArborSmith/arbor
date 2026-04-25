import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { isConnected } from "../../src/ue5-client.js";
import { playtestTool } from "../../src/registry/playtest.js";

const editorRunning = await isConnected();

describe.runIf(editorRunning)("Playtest Operations", () => {
  beforeAll(async () => {
    // Stop any leftover PIE from a prior test suite
    try {
      await playtestTool.actions.stop_pie({});
    } catch {
      /* best-effort */
    }
    await new Promise((r) => setTimeout(r, 1000));
  });

  afterAll(async () => {
    // Safety stop — ensure PIE is not left running
    try {
      await playtestTool.actions.stop_pie({});
    } catch {
      /* best-effort */
    }
  });

  // ── Pre-PIE ───────────────────────────────────────────────────────

  describe("Pre-PIE", () => {
    it("is_running returns a defined result", async () => {
      const result = (await playtestTool.actions.is_running({})) as {
        running?: boolean;
        is_running?: boolean;
      };
      expect(result).toBeDefined();
    });
  });

  // ── PIE Lifecycle ─────────────────────────────────────────────────

  describe("PIE Lifecycle", () => {
    it(
      "start → is_running → player_info → framerate → teleport → tap_key → get_held_keys → stop",
      async () => {
        // Start PIE
        const start = (await playtestTool.actions.start_pie(
          {}
        )) as { success?: boolean };
        expect(start).toBeDefined();

        // Poll for PIE to be running (up to 15s)
        let isRunning = false;
        for (let i = 0; i < 15; i++) {
          await new Promise((r) => setTimeout(r, 1000));
          const running = (await playtestTool.actions.is_running(
            {}
          )) as { running?: boolean; is_running?: boolean };
          isRunning = !!(running.running ?? running.is_running);
          if (isRunning) break;
        }
        expect(isRunning, "PIE should be running within 15s of start").toBe(true);

        // Get player info
        const info = (await playtestTool.actions.player_info(
          {}
        )) as Record<string, unknown>;
        expect(info).toBeDefined();

        // Get framerate
        const fps = (await playtestTool.actions.framerate(
          {}
        )) as Record<string, unknown>;
        expect(fps).toBeDefined();

        // Teleport
        const teleport = (await playtestTool.actions.teleport({
          x: 100,
          y: 200,
          z: 300,
        })) as Record<string, unknown>;
        expect(teleport).toBeDefined();

        // Tap a key
        const tap = (await playtestTool.actions.tap_key({
          key: "w",
          duration: 0.1,
        })) as { success?: boolean };
        expect(tap).toBeDefined();

        // Get held keys
        const held = (await playtestTool.actions.get_held_keys(
          {}
        )) as { keys?: string[]; held_keys?: string[] };
        expect(held).toBeDefined();

        // Stop PIE
        const stop = (await playtestTool.actions.stop_pie(
          {}
        )) as { success?: boolean };
        expect(stop).toBeDefined();
      },
      60_000
    );
  });

  // ── Navmesh-dependent (skipped) ───────────────────────────────────

  describe("Navmesh-dependent", () => {
    it.skip("move_to — requires built navmesh", () => {});
    it.skip("navigate_path — requires built navmesh", () => {});
    it.skip("can_reach — requires built navmesh", () => {});
  });
});

describe.skipIf(editorRunning)("Playtest Operations (skipped)", () => {
  it("UE5 editor not reachable — skipping integration tests", () => {
    console.log(
      "Start the UE5 editor with Remote Control API enabled to run these tests."
    );
  });
});
