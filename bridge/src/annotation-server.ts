import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const STATIC_DIR = resolve(__dirname, "..", "static");
const TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

interface AnnotationResult {
  annotatedPath: string;
}

function collectBody(req: IncomingMessage): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (chunk: Buffer) => chunks.push(chunk));
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

export function startAnnotationServer(
  screenshotPath: string,
): Promise<AnnotationResult> {
  return new Promise((resolveResult, rejectResult) => {
    let settled = false;

    const server = createServer(async (req: IncomingMessage, res: ServerResponse) => {
      try {
        const url = new URL(req.url ?? "/", `http://${req.headers.host}`);

        // Serve annotation HTML
        if (req.method === "GET" && url.pathname === "/") {
          const html = await readFile(join(STATIC_DIR, "annotate.html"));
          res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
          res.end(html);
          return;
        }

        // Serve original screenshot
        if (req.method === "GET" && url.pathname === "/api/image") {
          const imgPath = url.searchParams.get("path");
          if (!imgPath) {
            res.writeHead(400, { "Content-Type": "application/json" });
            res.end(JSON.stringify({ error: "Missing path parameter" }));
            return;
          }
          const imgBuffer = await readFile(imgPath);
          const mime = imgPath.endsWith(".png") ? "image/png" : "image/jpeg";
          res.writeHead(200, { "Content-Type": mime });
          res.end(imgBuffer);
          return;
        }

        // Receive annotated image
        if (req.method === "POST" && url.pathname === "/api/submit") {
          const body = await collectBody(req);
          if (body.length < 100) {
            res.writeHead(400, { "Content-Type": "application/json" });
            res.end(JSON.stringify({ error: "Empty or invalid image" }));
            return;
          }

          // Save next to original screenshot
          const dir = dirname(screenshotPath);
          const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
          const annotatedPath = join(dir, `annotated_${timestamp}.jpg`);
          await writeFile(annotatedPath, body);

          res.writeHead(200, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ success: true, path: annotatedPath }));

          // Resolve and close
          if (!settled) {
            settled = true;
            cleanup();
            resolveResult({ annotatedPath });
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

    // Timeout
    const timer = setTimeout(() => {
      if (!settled) {
        settled = true;
        cleanup();
        rejectResult(new Error("Annotation timed out (5 minutes). User did not submit."));
      }
    }, TIMEOUT_MS);

    function cleanup() {
      clearTimeout(timer);
      server.close();
    }

    // Listen on random port
    server.listen(0, "127.0.0.1", () => {
      const addr = server.address();
      if (addr && typeof addr === "object") {
        const port = addr.port;
        const url = `http://127.0.0.1:${port}/?image=${encodeURIComponent(screenshotPath)}`;
        // Store URL on the promise for the caller to open the browser
        (startAnnotationServer as any)._lastUrl = url;
      }
    });
  });
}

/** Get the URL of the most recently started annotation server. */
export function getLastAnnotationUrl(): string | undefined {
  return (startAnnotationServer as any)._lastUrl;
}
