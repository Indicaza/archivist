// The dashboard's local server for `npm run observatory:dashboard`: the page
// plus three read-only endpoints. Archivist serves the same page and the same
// endpoints from its own IDE host (frontend/src/Core/Domains/Observatory),
// so the page works the same in a browser and inside the app.
//
//   GET api/runs                          { root, runs: [{ runId, summary, error }] }
//   GET api/runs/<runId>/artifacts/<name> the artifact's text, if the run's
//                                         summary lists it under `artifacts`
//   GET api/baselines                     { baselines, baselinesError, budgets,
//                                           budgetsSource, budgetsError }
//
// With --control it also answers POST api/live/<op> (as observatory.live.<op>)
// and POST api/control/<operation> (the
// operations in ../control/operations.mjs), guarded by ../control/gate.mjs:
// this session's key, same-origin, JSON only. Without it, it stays read-only.
//
// Binds to 127.0.0.1, accepts only localhost Host headers, and serves
// nothing else from disk.
import { readFile, stat } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { checkControlRequest, readBody } from "../control/gate.mjs";
import { ControlError, dispatch, errorEnvelope, HTTP_STATUS } from "../control/operations.mjs";
import { artifactFile, artifactPath, baselineRunIds, listRawRunsForPage, MAX_ARTIFACT_BYTES, readBaselinesAndBudgets, readRunSummary, RUN_ID } from "./records.mjs";

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
  "regression.mjs": "text/javascript; charset=utf-8",
  "redact.mjs": "text/javascript; charset=utf-8",
  "diagnostic.mjs": "text/javascript; charset=utf-8",
  "dom.js": "text/javascript; charset=utf-8",
  "lab.js": "text/javascript; charset=utf-8",
  "live.mjs": "text/javascript; charset=utf-8",
  "live-page.js": "text/javascript; charset=utf-8",
  "audit.mjs": "text/javascript; charset=utf-8",
  "audit-page.js": "text/javascript; charset=utf-8",
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

async function serveControl(request, response, operation, control, origins) {
  const denied = checkControlRequest(request, { token: control?.token, origins });
  if (denied) return send(response, HTTP_STATUS[denied.code], errorEnvelope(operation, new ControlError(denied.code, denied.message)));
  let input;
  try {
    const text = await readBody(request);
    input = text.trim() ? JSON.parse(text) : {};
  } catch (error) {
    return send(response, 400, errorEnvelope(operation, new ControlError("invalid-input", `The body is not valid JSON: ${error.message}`)));
  }
  const result = await dispatch(control.ctx, operation, input);
  return send(response, result.ok ? 200 : HTTP_STATUS[result.error.code] ?? 500, result);
}

// control: null (read-only) or { token, ctx } for --control.
export function createDashboardServer({ root, port, control = null }) {
  const actualPort = () => server.address()?.port ?? port;
  const allowedHosts = () => new Set([`127.0.0.1:${actualPort()}`, `localhost:${actualPort()}`]);
  const allowedOrigins = () => new Set([`http://127.0.0.1:${actualPort()}`, `http://localhost:${actualPort()}`]);
  const server = createServer(async (request, response) => {
    try {
      // Another site can't reach this server through DNS tricks: only
      // requests addressed to localhost are answered.
      if (!allowedHosts().has(String(request.headers.host ?? ""))) {
        return fail(response, 403, "Only localhost requests are answered.");
      }
      const url = new URL(request.url, "http://localhost");
      if (url.pathname.startsWith("/api/control/")) {
        return serveControl(request, response, decodeURIComponent(url.pathname.slice(13)), control, allowedOrigins());
      }
      // The same address the app answers for its live recorder, so the page
      // calls one URL in both hosts; here it goes through the control
      // operations, which reach the app only while agent access is on.
      if (url.pathname.startsWith("/api/live/")) {
        const op = url.pathname.slice(10);
        return serveControl(request, response, /^[a-z][a-z-]{0,19}$/.test(op) ? `observatory.live.${op}` : "observatory.live.?", control, allowedOrigins());
      }
      if (request.method !== "GET" && request.method !== "HEAD") {
        return fail(response, 405, "The dashboard is read-only.");
      }
      const file = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
      if (Object.hasOwn(PAGE_FILES, file)) {
        return send(response, 200, await readFile(path.join(PAGE_DIR, file)), PAGE_FILES[file]);
      }
      if (url.pathname === "/api/runs") {
        return send(response, 200, { root, ...(await listRawRunsForPage(root, { keep: await baselineRunIds(root) })) });
      }
      if (url.pathname === "/api/baselines") {
        return send(response, 200, await readBaselinesAndBudgets(root));
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
