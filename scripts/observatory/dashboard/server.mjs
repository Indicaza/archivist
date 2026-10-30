// The dashboard's local server for `npm run observatory:dashboard`: the page
// plus two read-only endpoints. Archivist serves the same page and the same
// two endpoints from its own IDE host (frontend/src/Core/Domains/Observatory),
// so the page works the same in a browser and inside the app.
//
//   GET api/runs                          { root, runs: [{ runId, summary, error }] }
//   GET api/runs/<runId>/artifacts/<name> the artifact's text, if the run's
//                                         summary lists it under `artifacts`
//
// Binds to 127.0.0.1, answers GET/HEAD only, accepts only localhost Host
// headers, and serves nothing else from disk.
import { readFile, stat } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { artifactFile, artifactPath, listRawRuns, MAX_ARTIFACT_BYTES, readRunSummary, RUN_ID } from "./records.mjs";

export const PAGE_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), "public");
// Every file the page is made of. Nothing outside this list is served.
export const PAGE_FILES = {
  "index.html": "text/html; charset=utf-8",
  "app.js": "text/javascript; charset=utf-8",
  "charts.js": "text/javascript; charset=utf-8",
  "data.js": "text/javascript; charset=utf-8",
  "styles.css": "text/css; charset=utf-8",
  "compare.mjs": "text/javascript; charset=utf-8",
  "filter.mjs": "text/javascript; charset=utf-8",
  "normalize.mjs": "text/javascript; charset=utf-8",
  "preview.mjs": "text/javascript; charset=utf-8",
};

const SECURITY_HEADERS = {
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
  "Content-Security-Policy": "default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'",
  "Cache-Control": "no-store",
};

function send(response, status, body, type = "application/json; charset=utf-8", extra = {}) {
  response.writeHead(status, { "Content-Type": type, ...SECURITY_HEADERS, ...extra });
  response.end(typeof body === "string" || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}

function fail(response, status, message) {
  send(response, status, { error: message });
}

async function serveArtifact(response, root, runId, name) {
  if (!RUN_ID.test(runId)) {
    return fail(response, 400, "Not a run id.");
  }
  const loaded = await readRunSummary(root, runId);
  if (!loaded.summary) {
    return fail(response, 404, loaded.error);
  }
  if (!artifactPath(loaded.dir, loaded.summary, name)) {
    return fail(response, 404, "This run lists no such artifact.");
  }
  const file = await artifactFile(loaded.dir, loaded.summary, name);
  if (!file) {
    return fail(response, 404, "The artifact file is missing.");
  }
  if ((await stat(file)).size > MAX_ARTIFACT_BYTES) {
    return fail(response, 413, `The artifact is larger than ${MAX_ARTIFACT_BYTES / 2 ** 20} MB; open it from the run folder.`);
  }
  return send(response, 200, await readFile(file), "text/plain; charset=utf-8", {
    "Content-Disposition": `inline; filename="${runId}-${path.basename(file)}"`,
  });
}

export function createDashboardServer({ root, port }) {
  const allowedHosts = () => {
    const actual = server.address()?.port ?? port;
    return new Set([`127.0.0.1:${actual}`, `localhost:${actual}`]);
  };
  const server = createServer(async (request, response) => {
    try {
      // Another site can't reach this server through DNS tricks: only
      // requests addressed to localhost are answered.
      if (!allowedHosts().has(String(request.headers.host ?? ""))) {
        return fail(response, 403, "Only localhost requests are answered.");
      }
      if (request.method !== "GET" && request.method !== "HEAD") {
        return fail(response, 405, "The dashboard is read-only.");
      }
      const url = new URL(request.url, "http://localhost");
      const file = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
      if (Object.hasOwn(PAGE_FILES, file)) {
        return send(response, 200, await readFile(path.join(PAGE_DIR, file)), PAGE_FILES[file]);
      }
      if (url.pathname === "/api/runs") {
        return send(response, 200, { root, runs: await listRawRuns(root) });
      }
      const match = url.pathname.match(/^\/api\/runs\/([^/]+)\/artifacts\/([^/]+)$/);
      if (match) {
        return serveArtifact(response, root, decodeURIComponent(match[1]), decodeURIComponent(match[2]));
      }
      return fail(response, 404, "Not found.");
    } catch (error) {
      return fail(response, 500, error.message);
    }
  });
  return server;
}
