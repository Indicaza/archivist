// The dashboard's local server: static page + a read-only JSON API over the
// Observatory runs. Binds to 127.0.0.1, answers GET only, accepts only
// localhost Host headers, and serves files only by run id and the artifact
// names a run's own summary lists.
import { readFile, realpath } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { compareRuns } from "./compare.mjs";
import { filterOptions } from "./filter.mjs";
import { artifactPath, loadRuns, normalizeRun, readRunSummary, RUN_ID } from "./records.mjs";

const PUBLIC_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), "public");
const HERE = path.dirname(fileURLToPath(import.meta.url));
// The page, plus the comparison and filter modules it shares with the
// server (they import nothing, so the browser can load them as they are).
const STATIC = {
  "/": [path.join(PUBLIC_DIR, "index.html"), "text/html; charset=utf-8"],
  "/app.js": [path.join(PUBLIC_DIR, "app.js"), "text/javascript; charset=utf-8"],
  "/charts.js": [path.join(PUBLIC_DIR, "charts.js"), "text/javascript; charset=utf-8"],
  "/styles.css": [path.join(PUBLIC_DIR, "styles.css"), "text/css; charset=utf-8"],
  "/compare.mjs": [path.join(HERE, "compare.mjs"), "text/javascript; charset=utf-8"],
  "/filter.mjs": [path.join(HERE, "filter.mjs"), "text/javascript; charset=utf-8"],
};
const MAX_PREVIEW_LINES = 2000;

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

// Lines of a text artifact: a window of it, or the lines matching `q`.
export function previewText(text, { offset = 0, limit = 400, q = "" } = {}) {
  const lines = text.split(/\r?\n/);
  if (lines.at(-1) === "") {
    lines.pop();
  }
  const size = Math.min(MAX_PREVIEW_LINES, Math.max(1, Number(limit) || 400));
  let numbered = lines.map((line, index) => ({ n: index + 1, text: line }));
  if (q) {
    const needle = String(q).toLowerCase();
    numbered = numbered.filter((line) => line.text.toLowerCase().includes(needle));
  }
  const start = Math.max(0, Number(offset) || 0);
  return {
    totalLines: lines.length,
    matchingLines: numbered.length,
    offset: start,
    lines: numbered.slice(start, start + size),
    truncated: start + size < numbered.length,
  };
}

// An artifact the run lists, after following links: still inside the run's folder.
async function artifactFile(loaded, name) {
  const file = artifactPath(loaded.dir, loaded.summary, name);
  if (!file) {
    return null;
  }
  try {
    const [real, dir] = await Promise.all([realpath(file), realpath(loaded.dir)]);
    return real.startsWith(dir + path.sep) ? real : null;
  } catch {
    return null;
  }
}

async function handleRun(response, root, runId, rest, params) {
  if (!RUN_ID.test(runId)) {
    return fail(response, 400, "Not a run id.");
  }
  const loaded = await readRunSummary(root, runId);
  if (!loaded) {
    return fail(response, 404, "No such run.");
  }
  if (!rest) {
    return send(response, 200, {
      run: loaded.summary ? normalizeRun(runId, loaded.summary) : { runId, readable: false, status: "unreadable", problems: [loaded.error] },
      summary: loaded.summary,
      artifacts: Object.keys(loaded.summary?.artifacts ?? {}),
    });
  }
  const [what, name] = rest.split("/");
  if (what === "events") {
    const file = await artifactFile(loaded, "events");
    if (!file) {
      return fail(response, 404, "This run has no UI events.");
    }
    try {
      const events = JSON.parse(await readFile(file, "utf8"));
      return send(response, 200, { status: events.status ?? null, error: events.error ?? null, actions: events.actions ?? [] });
    } catch (error) {
      return fail(response, 404, `ui-events.json can't be read: ${error.message}`);
    }
  }
  if (what === "artifacts" && name) {
    if (!artifactPath(loaded.dir, loaded.summary, name)) {
      return fail(response, 404, "This run lists no such artifact.");
    }
    const file = await artifactFile(loaded, name);
    if (!file) {
      return fail(response, 404, "The artifact file is missing.");
    }
    const text = await readFile(file, "utf8");
    if (params.get("download") === "1") {
      return send(response, 200, text, "text/plain; charset=utf-8", {
        "Content-Disposition": `inline; filename="${runId}-${path.basename(file)}"`,
      });
    }
    return send(response, 200, {
      name,
      file: path.basename(file),
      sizeBytes: Buffer.byteLength(text),
      ...previewText(text, { offset: params.get("offset"), limit: params.get("limit"), q: params.get("q") ?? "" }),
    });
  }
  return fail(response, 404, "Unknown run resource.");
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
      if (STATIC[url.pathname]) {
        const [file, type] = STATIC[url.pathname];
        return send(response, 200, await readFile(file), type);
      }
      if (url.pathname === "/api/runs") {
        const runs = await loadRuns(root);
        return send(response, 200, { root, runs, options: filterOptions(runs) });
      }
      if (url.pathname === "/api/compare") {
        const ids = [url.searchParams.get("a"), url.searchParams.get("b")];
        if (!ids.every((id) => id && RUN_ID.test(id))) {
          return fail(response, 400, "Pick two runs to compare.");
        }
        const records = [];
        for (const id of ids) {
          const loaded = await readRunSummary(root, id);
          records.push(loaded?.summary ? normalizeRun(id, loaded.summary) : { runId: id, readable: false, supported: false, problems: [loaded?.error ?? "missing"] });
        }
        return send(response, 200, { runs: records, comparison: compareRuns(records[0], records[1]) });
      }
      const match = url.pathname.match(/^\/api\/runs\/([^/]+)(?:\/(.+))?$/);
      if (match) {
        return handleRun(response, root, decodeURIComponent(match[1]), match[2], url.searchParams);
      }
      return fail(response, 404, "Not found.");
    } catch (error) {
      return fail(response, 500, error.message);
    }
  });
  return server;
}
