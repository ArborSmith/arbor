import { z } from "zod";
import { callArborJson } from "../ue5-client.js";
import type { CategoryTool } from "./types.js";

export const environmentTool: CategoryTool = {
  description:
    "Graph-based environment building. Claude describes spatial layouts as semantic graphs " +
    "(nodes = assets, edges = spatial relationships like 'adjacent' or 'facing') and the " +
    "system resolves them to world positions and spawns the actors.",

  actionParams: {
    build: {
      summary: "Validate, resolve, and spawn full environment from semantic graph",
      required: ["graph"],
      optional: ["environment_id"],
    },
    clear: {
      summary: "Destroy all actors for an environment",
      required: ["environment_id"],
    },
    resolve: {
      summary: "Preview graph resolution to transforms without spawning",
      required: ["graph"],
    },
  },

  schema: {
    environment_id: z
      .string()
      .optional()
      .describe("Environment ID for tracking spawned actors (build, clear)"),
    graph: z
      .record(z.unknown())
      .optional()
      .describe(
        "Environment graph (build, resolve). Schema: " +
        '{id, origin?:{x,y,z}, nodes:{node_id:{asset_path, asset_type?, label?, ' +
        'transform_hints?:{yaw?, scale?}}}, edges:[{from:{node,anchor}, to:{node,anchor}, ' +
        'relationship:"adjacent"|"facing", params?:{gap?}}]}'
      ),
  },

  actions: {
    async build(p) {
      if (!p.graph) throw new Error("graph required");
      const graph = p.graph as Record<string, unknown>;

      // Validate graph structure (basic checks)
      const nodes = graph.nodes as Record<string, unknown> | undefined;
      if (!nodes || Object.keys(nodes).length === 0) {
        throw new Error("graph must have at least one node in 'nodes'");
      }

      // Set environment_id from param or graph.id
      const envId =
        (p.environment_id as string) ||
        (graph.id as string) ||
        "unnamed";
      graph.id = envId;

      // Step 1: Resolve graph to transforms
      const resolved = await callArborJson<{
        success: boolean;
        error?: string;
        transforms?: Record<string, unknown>;
        node_count?: number;
      }>("ArborLayoutSolver", "ResolveGraph", {
        GraphJson: JSON.stringify(graph),
      });

      if (!resolved.success) {
        return resolved;
      }

      // Step 2: Build spawn params (merge asset_path from graph + transforms from solver)
      const transforms = resolved.transforms as Record<
        string,
        { location?: unknown; rotation?: unknown; scale?: unknown }
      >;
      const spawnNodes: Record<string, unknown> = {};

      for (const [nodeId, nodeDef] of Object.entries(
        nodes as Record<string, Record<string, unknown>>
      )) {
        const t = transforms?.[nodeId] ?? {};
        spawnNodes[nodeId] = {
          asset_path: nodeDef.asset_path,
          location: t.location ?? { x: 0, y: 0, z: 0 },
          rotation: t.rotation ?? { pitch: 0, yaw: 0, roll: 0 },
          scale: t.scale ?? { x: 1, y: 1, z: 1 },
          label: nodeDef.label,
        };
      }

      // Step 3: Spawn
      const spawnResult = await callArborJson<{
        success: boolean;
        spawned?: unknown[];
        failed?: unknown[];
      }>("ArborEnvironmentSpawner", "SpawnEnvironment", {
        ParamsJson: JSON.stringify({
          environment_id: envId,
          nodes: spawnNodes,
        }),
      });

      return {
        ...spawnResult,
        environment_id: envId,
        transforms: resolved.transforms,
        node_count: resolved.node_count,
      };
    },

    async clear(p) {
      const envId = p.environment_id as string;
      if (!envId) throw new Error("environment_id required");
      return callArborJson("ArborEnvironmentSpawner", "DespawnEnvironment", {
        EnvironmentId: envId,
      });
    },

    async resolve(p) {
      if (!p.graph) throw new Error("graph required");
      return callArborJson("ArborLayoutSolver", "ResolveGraph", {
        GraphJson: JSON.stringify(p.graph),
      });
    },
  },
};
