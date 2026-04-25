import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const STATIC_DIR = resolve(__dirname, "..", "static");
const TIMEOUT_MS = 10 * 60 * 1000; // 10 minutes

export interface IssueReviewInput {
  title: string;
  body: string;
  repo: string;
}

export interface IssueReviewResult {
  action: "confirm" | "cancel";
  title?: string;
  body?: string;
}

export interface StartedReview {
  url: Promise<string>;
  result: Promise<IssueReviewResult>;
}

function collectBody(req: IncomingMessage): Promise<Buffer> {
  return new Promise((resolveBody, rejectBody) => {
    const chunks: Buffer[] = [];
    req.on("data", (chunk: Buffer) => chunks.push(chunk));
    req.on("end", () => resolveBody(Buffer.concat(chunks)));
    req.on("error", rejectBody);
  });
}

/**
 * Spin up a local HTTP server hosting the issue-review UI. Caller is
 * expected to open the returned URL in a browser; the result Promise
 * resolves once the user clicks Submit or Cancel (or rejects on timeout).
 */
export function startIssueReviewServer(input: IssueReviewInput): StartedReview {
  let resolveUrl!: (url: string) => void;
  let rejectUrl!: (err: Error) => void;
  let resolveResult!: (r: IssueReviewResult) => void;
  let rejectResult!: (err: Error) => void;

  const urlPromise = new Promise<string>((res, rej) => {
    resolveUrl = res;
    rejectUrl = rej;
  });
  const resultPromise = new Promise<IssueReviewResult>((res, rej) => {
    resolveResult = res;
    rejectResult = rej;
  });

  let settled = false;

  const server = createServer(async (req: IncomingMessage, res: ServerResponse) => {
    try {
      const url = new URL(req.url ?? "/", `http://${req.headers.host}`);

      if (req.method === "GET" && url.pathname === "/") {
        const html = await readFile(join(STATIC_DIR, "issue-review.html"));
        res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
        res.end(html);
        return;
      }

      if (req.method === "GET" && url.pathname === "/api/issue") {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(input));
        return;
      }

      if (req.method === "POST" && url.pathname === "/api/submit") {
        const body = await collectBody(req);
        let payload: { action?: string; title?: string; body?: string };
        try {
          payload = JSON.parse(body.toString("utf8")) as { action?: string; title?: string; body?: string };
        } catch {
          res.writeHead(400, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ error: "Invalid JSON" }));
          return;
        }

        const action: "confirm" | "cancel" = payload.action === "confirm" ? "confirm" : "cancel";
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: true }));

        if (!settled) {
          settled = true;
          cleanup();
          if (action === "confirm") {
            resolveResult({
              action,
              title: payload.title ?? input.title,
              body: payload.body ?? input.body,
            });
          } else {
            resolveResult({ action: "cancel" });
          }
        }
        return;
      }

      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("Not found");
    } catch (err) {
      res.writeHead(500, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ error: String(err) }));
    }
  });

  const timer = setTimeout(() => {
    if (!settled) {
      settled = true;
      cleanup();
      rejectResult(new Error("Issue review timed out (10 minutes). User did not respond."));
    }
  }, TIMEOUT_MS);

  function cleanup() {
    clearTimeout(timer);
    server.close();
  }

  server.listen(0, "127.0.0.1", () => {
    const addr = server.address();
    if (addr && typeof addr === "object") {
      resolveUrl(`http://127.0.0.1:${addr.port}/`);
    } else {
      const err = new Error("Failed to determine review-server address");
      rejectUrl(err);
      if (!settled) {
        settled = true;
        cleanup();
        rejectResult(err);
      }
    }
  });

  server.on("error", (err) => {
    if (!settled) {
      settled = true;
      cleanup();
      rejectUrl(err);
      rejectResult(err);
    }
  });

  return { url: urlPromise, result: resultPromise };
}
