// Baselines: which recorded runs later runs are compared with, one group per
// workload category (clean / incremental / no-change builds, each UI
// scenario). Kept in <Observatory folder>/baselines.json. Setting, replacing
// or removing a baseline only edits that file: the runs themselves are never
// touched, and replaced baselines stay listed under "history".
import { readFile } from "node:fs/promises";
import path from "node:path";
import { normalizeRun } from "./dashboard/public/normalize.mjs";
import { categoryLabel, categoryOf, METRIC_DEFINITIONS_VERSION, METRICS } from "./dashboard/public/regression.mjs";
import { readRunSummary, RUN_ID } from "./dashboard/records.mjs";
import { writeJsonAtomic } from "./storage.mjs";

export const BASELINES_FILE = "baselines.json";
export const BASELINES_SCHEMA = "archivist.observatory.baselines";
export const BASELINES_SCHEMA_VERSION = 1;

const OK = new Set(["success", "passed"]);
// Runs grouped into one baseline must agree on these.
const GROUP_FIELDS = [
  ["machine", "platform"], ["machine", "architecture"], ["machine", "cpuModel"], ["machine", "logicalCpus"],
  ["config", "buildType"], ["ui", "scenarioVersion"], ["ui", "driverVersion"],
];

export function baselinesPath(root) {
  return path.join(root, BASELINES_FILE);
}

export function emptyBaselines() {
  return { schema: BASELINES_SCHEMA, schemaVersion: BASELINES_SCHEMA_VERSION, updatedAt: null, baselines: {}, history: [] };
}

// { data, error }: data is null only when the file exists but can't be used.
export async function readBaselines(root) {
  let text;
  try {
    text = await readFile(baselinesPath(root), "utf8");
  } catch (error) {
    if (error.code === "ENOENT") return { data: emptyBaselines(), error: null };
    return { data: null, error: `${BASELINES_FILE} can't be read: ${error.message}` };
  }
  let data;
  try {
    data = JSON.parse(text);
  } catch (error) {
    return { data: null, error: `${BASELINES_FILE} is not valid JSON: ${error.message}` };
  }
  if (!data || typeof data !== "object" || data.schema !== BASELINES_SCHEMA) {
    return { data: null, error: `${BASELINES_FILE} is not an Observatory baselines file` };
  }
  if (data.schemaVersion !== BASELINES_SCHEMA_VERSION) {
    return { data: null, error: `${BASELINES_FILE} has schemaVersion ${data.schemaVersion}; this version of Observatory reads ${BASELINES_SCHEMA_VERSION}` };
  }
  return { data: { ...emptyBaselines(), ...data, baselines: data.baselines ?? {}, history: data.history ?? [] }, error: null };
}

// The file must be readable before it's changed: a damaged or newer file is
// never overwritten.
async function editable(root) {
  const { data, error } = await readBaselines(root);
  if (error) throw new Error(`${error}. Fix or move ${baselinesPath(root)} first; it was not changed.`);
  return data;
}

async function loadRecords(root, runIds) {
  const records = [];
  for (const runId of runIds) {
    if (!RUN_ID.test(runId)) throw new Error(`${runId} is not an Observatory run id.`);
    const loaded = await readRunSummary(root, runId);
    if (!loaded.summary) throw new Error(`Run ${runId}: ${loaded.error}.`);
    records.push(normalizeRun(runId, loaded.summary));
  }
  return records;
}

function pick(run, [group, field]) {
  return run[group]?.[field] ?? null;
}

// Checks the runs and describes them as one baseline entry.
export function baselineEntry(records, { note = null, setAt = new Date().toISOString() } = {}) {
  if (!records.length) throw new Error("Name at least one run.");
  const ids = new Set();
  for (const run of records) {
    if (ids.has(run.runId)) throw new Error(`${run.runId} is listed twice.`);
    ids.add(run.runId);
    if (!run.readable || !run.supported) throw new Error(`${run.runId} can't be read: ${(run.problems ?? []).join("; ")}.`);
    if (!OK.has(run.status)) throw new Error(`${run.runId} did not succeed (${run.status}); only successful runs can be a baseline.`);
    if (!categoryOf(run)) {
      throw new Error(`${run.runId} has no baseline category${run.kind === "build" ? " (its build kind wasn't classified)" : ""}. Builds and UI scenario runs can be baselines.`);
    }
  }
  const category = categoryOf(records[0]);
  const mixed = records.find((run) => categoryOf(run) !== category);
  if (mixed) {
    throw new Error(`${mixed.runId} is a ${categoryLabel(categoryOf(mixed))} run, not ${categoryLabel(category)}. One baseline holds one kind of workload.`);
  }
  for (const field of GROUP_FIELDS) {
    const values = new Set(records.map((run) => pick(run, field)).filter((value) => value != null));
    if (values.size > 1) throw new Error(`These runs differ in ${field[1]} (${[...values].join(" vs ")}); a baseline's runs must be comparable with each other.`);
  }
  const sorted = [...records].sort((a, b) => a.runId.localeCompare(b.runId));
  const newest = sorted.at(-1);
  const commits = [...new Set(sorted.map((run) => run.source?.commit).filter(Boolean))];
  return {
    category,
    runIds: sorted.map((run) => run.runId),
    setAt,
    note,
    metricDefinitionsVersion: METRIC_DEFINITIONS_VERSION,
    metricsMeasured: Object.entries(METRICS).filter(([, metric]) => sorted.some((run) => metric.read(run) != null)).map(([key]) => key),
    reference: {
      commit: newest.source?.commit ?? null,
      commits,
      branch: newest.source?.branch ?? null,
      dirty: sorted.some((run) => run.source?.dirty === true),
      recordedAt: newest.createdAt ?? null,
      machine: newest.machine,
      config: newest.config,
      scenario: newest.ui ? { id: newest.ui.scenarioId, version: newest.ui.scenarioVersion, driverVersion: newest.ui.driverVersion } : null,
      buildKind: newest.build?.classification ?? null,
    },
  };
}

export async function setBaseline(root, runIds, options = {}) {
  const data = await editable(root);
  const entry = baselineEntry(await loadRecords(root, runIds), options);
  const previous = data.baselines[entry.category];
  if (previous) data.history.push({ ...previous, replacedAt: entry.setAt, change: "replaced" });
  data.baselines[entry.category] = entry;
  data.updatedAt = entry.setAt;
  await writeJsonAtomic(baselinesPath(root), data);
  return { entry, previous: previous ?? null };
}

export async function removeBaseline(root, category, { now = new Date().toISOString() } = {}) {
  const data = await editable(root);
  const previous = data.baselines[category];
  if (!previous) throw new Error(`There is no ${category} baseline. Current: ${Object.keys(data.baselines).join(", ") || "none"}.`);
  delete data.baselines[category];
  data.history.push({ ...previous, replacedAt: now, change: "removed" });
  data.updatedAt = now;
  await writeJsonAtomic(baselinesPath(root), data);
  return previous;
}
