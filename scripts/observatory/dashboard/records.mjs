// Reads Observatory runs for the dashboard. Read-only: it never writes to
// the runs directory. Each record is normalized into a small, flat shape
// with only measurements that were actually recorded (missing stays null).
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { SCHEMA_VERSION, runsDir } from "../storage.mjs";

// Run ids are directory names Observatory made; nothing else is served.
export const RUN_ID = /^[0-9]{8}T[0-9]{6}-[0-9]{3}Z-[a-z0-9-]+-[0-9a-f]{6}$/;

const BUILD_PHASES = ["ide-bundle", "configure", "build-plan", "native-build"];

function num(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function phaseMs(summary, name) {
  const phase = (summary.phases ?? []).find((entry) => entry.name === name);
  return phase && phase.status !== "skipped" ? num(phase.durationMs) : null;
}

function buildMetrics(summary) {
  const native = (summary.phases ?? []).find((entry) => entry.name === "native-build");
  const ide = (summary.phases ?? []).find((entry) => entry.name === "ide-bundle");
  const log = native?.details?.ninjaLog;
  return {
    totalMs: num(summary.totalDurationMs),
    phases: Object.fromEntries(BUILD_PHASES.map((name) => [name, phaseMs(summary, name)])),
    ideCacheReused: typeof ide?.details?.cacheReused === "boolean" ? ide.details.cacheReused : null,
    classification: summary.buildConditions?.classification?.value ?? null,
    ninjaWorkSteps: log?.basis === "measured-by-ninja" ? num(log.workSteps) : null,
    failedPhase: (summary.phases ?? []).find((entry) => entry.status === "failure" || entry.status === "interrupted")?.name ?? null,
  };
}

function uiMetrics(summary) {
  const measured = summary.stats?.measured;
  const pick = (field) => (measured?.[field]
    ? { medianMs: num(measured[field].medianMs), p95Ms: num(measured[field].p95Ms), maxMs: num(measured[field].maxMs), n: num(measured[field].n) }
    : null);
  return {
    scenarioId: summary.scenario?.id ?? null,
    scenarioVersion: num(summary.scenario?.version),
    scenarioName: summary.scenario?.name ?? null,
    driverVersion: num(summary.app?.driverVersion),
    appDurationMs: num(summary.app?.durationMs),
    actions: num(summary.actions?.total),
    failedActions: num(summary.actions?.failed),
    settled: pick("settledMs"),
    state: pick("stateMs"),
    dispatch: pick("dispatchMs"),
    frameGap: pick("frameMaxIntervalMs"),
    steps: (summary.stats?.steps ?? []).map((step) => ({
      step: step.step,
      label: step.label ?? step.op,
      method: step.method ?? null,
      failed: num(step.failed),
      settledMedianMs: num(step.settledMs?.medianMs),
      settledMaxMs: num(step.settledMs?.maxMs),
      stateMedianMs: num(step.stateMs?.medianMs),
    })),
    wellsEvents: num(summary.runtimeTiming?.recognizedEvents),
  };
}

// The flat record the dashboard lists, filters and compares.
export function normalizeRun(runId, summary) {
  const problems = [];
  if (!summary || typeof summary !== "object" || Array.isArray(summary)) {
    return { runId, readable: false, supported: false, kind: "unknown", status: "unreadable", problems: ["summary.json is not an object"] };
  }
  const schemaVersion = num(summary.schemaVersion);
  const supported = schemaVersion !== null && schemaVersion <= SCHEMA_VERSION;
  if (!supported) {
    problems.push(`schemaVersion ${summary.schemaVersion ?? "missing"} is not one this dashboard reads (up to ${SCHEMA_VERSION})`);
  }
  if (summary.runId && summary.runId !== runId) {
    problems.push(`summary says runId ${summary.runId}`);
  }
  const kind = typeof summary.kind === "string" ? summary.kind : "unknown";
  let status = typeof summary.status === "string" ? summary.status : "unknown";
  if (status === "running") {
    // Never finished: the collector was killed or is still going.
    problems.push("the run never finished (status running)");
    status = "incomplete";
  }
  const env = summary.environment ?? {};
  const toolchain = summary.toolchain ?? summary.build?.toolchain ?? {};
  const record = {
    runId,
    readable: true,
    supported,
    schemaVersion,
    kind,
    status,
    label: summary.label ?? null,
    createdAt: summary.createdAt ?? null,
    finishedAt: summary.finishedAt ?? null,
    source: {
      commit: summary.source?.commit ?? null,
      branch: summary.source?.branch ?? null,
      dirty: typeof summary.source?.dirty === "boolean" ? summary.source.dirty : null,
    },
    machine: {
      platform: env.platform ?? null,
      osVersion: env.osVersion ?? null,
      architecture: env.architecture ?? null,
      cpuModel: env.cpuModel ?? null,
      logicalCpus: num(env.logicalCpus),
      totalMemoryBytes: num(env.totalMemoryBytes),
    },
    config: {
      buildType: toolchain.buildType ?? null,
      compiler: toolchain.compiler?.id ? `${toolchain.compiler.id} ${toolchain.compiler.version ?? ""}`.trim() : null,
      qtVersion: toolchain.qtVersion ?? null,
      cmakeVersion: toolchain.cmakeVersion ?? null,
      binarySha256: summary.build?.sha256 ?? null,
    },
    error: summary.error ?? summary.errors?.[0]?.message ?? null,
    problems,
  };
  if (supported && kind === "build") {
    record.build = buildMetrics(summary);
  } else if (supported && kind === "ui-scenario") {
    record.ui = uiMetrics(summary);
  } else if (supported && kind === "runtime-log") {
    record.runtime = { events: num(summary.timing?.recognizedEvents), summary: summary.timing?.summary ?? {} };
  }
  return record;
}

export async function readRunSummary(root, runId) {
  if (!RUN_ID.test(runId)) {
    return null;
  }
  const dir = path.join(runsDir(root), runId);
  try {
    return { dir, summary: JSON.parse(await readFile(path.join(dir, "summary.json"), "utf8")) };
  } catch (error) {
    return { dir, summary: null, error: error.code === "ENOENT" ? "summary.json is missing" : `summary.json is unreadable: ${error.message}` };
  }
}

// Every run, newest first. Unreadable runs are listed, not dropped.
export async function loadRuns(root) {
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
    const loaded = await readRunSummary(root, runId);
    if (!loaded.summary) {
      return { runId, readable: false, supported: false, kind: kindFromId(runId), status: "unreadable", problems: [loaded.error], source: {}, machine: {}, config: {} };
    }
    return normalizeRun(runId, loaded.summary);
  }));
  return runs.sort((a, b) => b.runId.localeCompare(a.runId));
}

// The run id carries its kind (build, ui-sidebar, runtime), for runs whose
// summary can't be read.
function kindFromId(runId) {
  const kind = runId.split("-").slice(2, -1).join("-");
  return kind === "build" ? "build" : kind.startsWith("ui-") ? "ui-scenario" : kind === "runtime" ? "runtime-log" : "unknown";
}

// Artifacts a run lists in its summary, by name; only these are served.
export function artifactPath(dir, summary, name) {
  const relative = summary?.artifacts?.[name];
  if (typeof relative !== "string" || !relative) {
    return null;
  }
  const resolved = path.resolve(dir, relative);
  return resolved.startsWith(dir + path.sep) ? resolved : null;
}
