// Where the page gets its data. The host (the Node dashboard server or
// Archivist's own IDE host) only lists the raw summaries and serves the
// artifact files a summary names; everything else is worked out here.
// URLs are relative, so the page works at "/" and at "/observatory/".
import { compareRuns } from "./compare.mjs";
import { filterOptions } from "./filter.mjs";
import { normalizeRun, unreadableRun } from "./normalize.mjs";
import { previewText } from "./preview.mjs";

const raw = new Map();
let cachedText = null;

async function getJson(url) {
  const response = await fetch(url, { cache: "no-store" });
  const body = await response.json().catch(() => ({ error: `HTTP ${response.status}` }));
  if (!response.ok) throw new Error(errorMessage(body, response.status));
  return body;
}

// The Node server answers { error: "..." }; Archivist answers { error: { message } }.
function errorMessage(body, status) {
  const error = body?.error;
  return typeof error === "string" ? error : error?.message ?? `HTTP ${status}`;
}

function record(runId) {
  const entry = raw.get(runId);
  if (!entry) return unreadableRun(runId, "No such run.");
  return entry.summary ? normalizeRun(runId, entry.summary) : unreadableRun(runId, entry.error);
}

export async function loadRuns() {
  const body = await getJson("api/runs");
  raw.clear();
  cachedText = null;
  for (const entry of body.runs ?? []) raw.set(entry.runId, entry);
  const runs = [...raw.keys()].sort().reverse().map(record);
  // A long history is cut to the newest runs (plus those baselines name).
  const omitted = Number.isInteger(body.omitted) && body.omitted > 0 ? body.omitted : 0;
  return { root: body.root ?? "", runs, options: filterOptions(runs), listing: { total: body.total ?? runs.length, omitted } };
}

export function runDetails(runId) {
  const summary = raw.get(runId)?.summary ?? null;
  const artifacts = Object.entries(summary?.artifacts ?? {}).filter(([, file]) => typeof file === "string" && file).map(([name]) => name);
  return { run: record(runId), summary, artifacts };
}

export function artifactUrl(runId, name) {
  return `api/runs/${encodeURIComponent(runId)}/artifacts/${encodeURIComponent(name)}`;
}

async function artifactText(runId, name) {
  const key = `${runId}/${name}`;
  if (cachedText?.key === key) return cachedText;
  const response = await fetch(artifactUrl(runId, name), { cache: "no-store" });
  if (!response.ok) {
    const body = await response.json().catch(() => ({}));
    throw new Error(errorMessage(body, response.status));
  }
  const text = await response.text();
  const file = String(raw.get(runId)?.summary?.artifacts?.[name] ?? name).split("/").pop();
  cachedText = { key, text, file, sizeBytes: new TextEncoder().encode(text).length };
  return cachedText;
}

export async function artifactPreview(runId, name, options) {
  const { text, file, sizeBytes } = await artifactText(runId, name);
  return { name, file, sizeBytes, ...previewText(text, options) };
}

export async function runEvents(runId) {
  if (!runDetails(runId).artifacts.includes("events") || runDetails(runId).summary?.kind === "live-session") throw new Error("This run has no UI events.");
  const { text } = await artifactText(runId, "events");
  let events;
  try {
    events = JSON.parse(text);
  } catch (error) {
    throw new Error(`ui-events.json can't be read: ${error.message}`);
  }
  return { status: events.status ?? null, error: events.error ?? null, actions: Array.isArray(events.actions) ? events.actions : [] };
}

// baselines.json and the budgets, as stored; the page checks them itself.
export async function loadBaselines() {
  return getJson("api/baselines");
}

export async function runScenario(runId) {
  if (!runDetails(runId).artifacts.includes("scenario")) return null;
  try {
    return JSON.parse((await artifactText(runId, "scenario")).text);
  } catch {
    return null;
  }
}

// The run's log text for the report's error lines, if it is small enough to read.
export async function runLogText(runId) {
  if (!runDetails(runId).artifacts.includes("log")) return null;
  try {
    return (await artifactText(runId, "log")).text;
  } catch {
    return null;
  }
}

// ---- controls -------------------------------------------------------------------
// Running tests and writing exports need this session's control key. The
// dashboard prints a link carrying it (#control=<key>); Archivist's panel
// opens the page with it. It never leaves this page except in the
// X-Observatory-Token header of a same-origin request.

const KEY_STORAGE = "archivist.observatory.control";
let controlKey = null;

export function initControl() {
  // #control=<key>, optionally &to=<page>; the key is removed from the address at once.
  const match = location.hash.match(/^#control=([A-Za-z0-9_-]{16,128})(?:&to=([a-z]{1,20}))?$/);
  if (match) {
    controlKey = match[1];
    try {
      sessionStorage.setItem(KEY_STORAGE, controlKey);
    } catch {
      // kept in memory only
    }
    history.replaceState(null, "", `${location.pathname}${location.search}#/${match[2] ?? ""}`);
    return;
  }
  try {
    controlKey = sessionStorage.getItem(KEY_STORAGE);
  } catch {
    controlKey = null;
  }
}

export function hasControlKey() {
  return Boolean(controlKey);
}

// Always resolves to a response envelope ({ ok, result } or { ok: false, error }).
export async function control(operation, input = {}) {
  if (!controlKey) {
    return { ok: false, error: { code: "controls-disabled", message: "This page has no control key." } };
  }
  let response;
  try {
    response = await fetch(`api/control/${encodeURIComponent(operation)}`, {
      method: "POST",
      cache: "no-store",
      headers: { "Content-Type": "application/json", "X-Observatory-Token": controlKey },
      body: JSON.stringify(input),
    });
  } catch (error) {
    return { ok: false, error: { code: "internal", message: `The dashboard server didn't answer: ${error.message}` } };
  }
  const body = await response.json().catch(() => null);
  if (body && typeof body.ok === "boolean") return body;
  return { ok: false, error: { code: response.status === 404 || response.status === 405 ? "controls-disabled" : "internal", message: errorMessage(body, response.status) } };
}

export function compare(a, b) {
  const runs = [record(a), record(b)];
  return { runs, comparison: compareRuns(runs[0], runs[1]) };
}

// ---- live recorder ----------------------------------------------------------------
// The page always calls api/live/<op>. Inside Archivist that is the app's own
// recorder (same origin, the panel's key); from the browser dashboard the
// server runs the observatory.live.* operations, which reach the app only
// while "Let agents record" is on. Only the app can change agent access.

let liveInApp = false;

export function liveIsInApp() {
  return liveInApp;
}

export async function live(operation, input = {}) {
  if (!controlKey) return { ok: false, error: { code: "controls-disabled", message: "This page has no control key." } };
  let response;
  try {
    response = await fetch(`api/live/${encodeURIComponent(operation)}`, {
      method: "POST",
      cache: "no-store",
      headers: { "Content-Type": "application/json", "X-Observatory-Token": controlKey },
      body: JSON.stringify(input),
    });
  } catch (error) {
    return { ok: false, error: { code: "app-unreachable", message: `No answer: ${error.message}` } };
  }
  const body = await response.json().catch(() => null);
  if (!body || typeof body.ok !== "boolean") {
    return { ok: false, error: { code: "internal", message: errorMessage(body, response.status) } };
  }
  if (operation === "status" && body.ok) liveInApp = Object.hasOwn(body.result ?? {}, "agentAccess");
  return body;
}

const liveCache = new Map();

// metrics.json and events.json of a recording (null when missing or unreadable).
export async function liveArtifacts(runId) {
  const { artifacts, summary } = runDetails(runId);
  const key = `${runId}:${summary?.finishedAt ?? ""}`;
  if (liveCache.has(key)) return liveCache.get(key);
  const read = async (name) => {
    if (!artifacts.includes(name)) return null;
    try {
      const response = await fetch(artifactUrl(runId, name), { cache: "no-store" });
      return response.ok ? await response.json() : null;
    } catch {
      return null;
    }
  };
  const [metrics, events] = await Promise.all([read("metrics"), read("events")]);
  const value = { metrics, events };
  if (summary?.status !== "running") liveCache.set(key, value);
  return value;
}
