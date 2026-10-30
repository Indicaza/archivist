// Reads Observatory runs from disk for the dashboard's Node server.
// Read-only: it never writes to the runs directory.
import { readdir, readFile, realpath } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { runsDir } from "../storage.mjs";
import { normalizeRun, unreadableRun } from "./public/normalize.mjs";

// Run ids are directory names Observatory made; nothing else is served.
export const RUN_ID = /^[0-9]{8}T[0-9]{6}-[0-9]{3}Z-[a-z0-9-]+-[0-9a-f]{6}$/;
// Larger artifacts are refused rather than read into memory.
export const MAX_ARTIFACT_BYTES = 32 * 1024 * 1024;

export async function readRunSummary(root, runId) {
  if (!RUN_ID.test(runId)) {
    return null;
  }
  const dir = path.join(runsDir(root), runId);
  try {
    return { dir, summary: JSON.parse(await readFile(path.join(dir, "summary.json"), "utf8")), error: null };
  } catch (error) {
    return { dir, summary: null, error: error.code === "ENOENT" ? "summary.json is missing" : `summary.json is unreadable: ${error.message}` };
  }
}

// Every run's summary as stored, newest first: { runId, summary, error }.
// This is what the page receives; it normalizes them itself.
export async function listRawRuns(root) {
  let names;
  try {
    names = (await readdir(runsDir(root), { withFileTypes: true }))
      .filter((entry) => entry.isDirectory() && RUN_ID.test(entry.name))
      .map((entry) => entry.name);
  } catch (error) {
    if (error.code === "ENOENT") {
      return [];
    }
    throw error;
  }
  const runs = await Promise.all(names.map(async (runId) => {
    const { summary, error } = await readRunSummary(root, runId);
    return { runId, summary, error };
  }));
  return runs.sort((a, b) => b.runId.localeCompare(a.runId));
}

// The repository's budgets; <root>/budgets.json overrides them on one machine.
export const REPOSITORY_BUDGETS = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "budgets.json");

async function readJsonFile(file) {
  try {
    return { value: JSON.parse(await readFile(file, "utf8")), error: null, found: true };
  } catch (error) {
    if (error.code === "ENOENT") return { value: null, error: null, found: false };
    return { value: null, error: `${path.basename(file)} can't be read: ${error.message}`, found: true };
  }
}

// For the dashboard's Baselines page: both files as stored. The page checks
// them itself (regression.mjs), like it does run summaries.
export async function readBaselinesAndBudgets(root) {
  const baselines = await readJsonFile(path.join(root, "baselines.json"));
  const local = await readJsonFile(path.join(root, "budgets.json"));
  const budgetsSource = local.found ? path.join(root, "budgets.json") : REPOSITORY_BUDGETS;
  const budgets = local.found ? local : await readJsonFile(REPOSITORY_BUDGETS);
  return {
    baselines: baselines.value,
    baselinesError: baselines.error,
    budgets: budgets.value,
    budgetsSource: budgets.found ? budgetsSource : null,
    budgetsError: budgets.error ?? (budgets.found ? null : "no budgets file"),
  };
}

// Every run, normalized, newest first. Unreadable runs are listed, not dropped.
export async function loadRuns(root) {
  return (await listRawRuns(root)).map(({ runId, summary, error }) => (summary ? normalizeRun(runId, summary) : unreadableRun(runId, error)));
}

// Artifacts a run lists in its summary, by name; only these are served.
export function artifactPath(dir, summary, name) {
  if (!summary?.artifacts || !Object.hasOwn(summary.artifacts, name)) {
    return null;
  }
  const relative = summary.artifacts[name];
  if (typeof relative !== "string" || !relative) {
    return null;
  }
  const resolved = path.resolve(dir, relative);
  return resolved.startsWith(dir + path.sep) ? resolved : null;
}

// The listed artifact after following links: still inside the run's folder.
export async function artifactFile(dir, summary, name) {
  const file = artifactPath(dir, summary, name);
  if (!file) {
    return null;
  }
  try {
    const [real, realDir] = await Promise.all([realpath(file), realpath(dir)]);
    return real.startsWith(realDir + path.sep) ? real : null;
  } catch {
    return null;
  }
}
