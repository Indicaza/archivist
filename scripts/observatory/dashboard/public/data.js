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
  return { root: body.root ?? "", runs, options: filterOptions(runs) };
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
  if (!runDetails(runId).artifacts.includes("events")) throw new Error("This run has no UI events.");
  const { text } = await artifactText(runId, "events");
  let events;
  try {
    events = JSON.parse(text);
  } catch (error) {
    throw new Error(`ui-events.json can't be read: ${error.message}`);
  }
  return { status: events.status ?? null, error: events.error ?? null, actions: Array.isArray(events.actions) ? events.actions : [] };
}

export function compare(a, b) {
  const runs = [record(a), record(b)];
  return { runs, comparison: compareRuns(runs[0], runs[1]) };
}
