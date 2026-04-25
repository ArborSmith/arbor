// Types for the optional private overlay extension hook.
//
// The actual `extensions.local.ts` file is NOT part of the public repo — it
// is dropped in by a private overlay (see arbor-overlay repo). This `.d.ts`
// lets TypeScript type-check the dynamic import in `index.ts` without the
// module physically existing. At runtime the import is wrapped in try/catch
// and silently no-ops when the module is absent.
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { Features } from "./features.js";

export function register(server: McpServer, features: Features): void;
