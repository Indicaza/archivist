// Turns one run's summary.json into the small, flat record the dashboard
// lists, filters and compares, keeping only what was actually recorded
// (missing stays null). Pure: no imports, so the page (in a browser or in
// Archivist's own web view) and the Node server share it.

// The newest summary schema this dashboard understands. Keep in step with
// SCHEMA_VERSION in ../../storage.mjs (a test checks).
export const SUPPORTED_SCHEMA_VERSION = 1;

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
    profiling: summary.buildConditions?.profiling ?? null,
    jobs: jobMetrics(summary.evidence),
  };
}

// From build-evidence.mjs (null for builds recorded before it existed).
function jobMetrics(evidence) {
  if (!evidence?.timing) return null;
  const category = (name) => evidence.byCategory?.[name] ?? null;
  return {
    count: num(evidence.jobs),
    wallMs: num(evidence.timing.wallMs),
    summedMs: num(evidence.timing.summedJobMs),
    peakParallel: num(evidence.timing.peakParallelJobs),
    criticalPathMs: evidence.criticalPath?.available ? num(evidence.criticalPath.lengthMs) : null,
    compiles: num(category("compile")?.jobs ?? 0),
    compileMs: num(category("compile")?.summedMs ?? 0),
    linkMs: num((category("link")?.summedMs ?? 0) + (category("archive")?.summedMs ?? 0)),
    autogenMs: num(category("autogen")?.summedMs ?? 0),
    longestJobMs: num(evidence.timing.longestJob?.durationMs),
  };
}

function medianOf(values) {
  const list = values.filter((value) => typeof value === "number" && Number.isFinite(value)).sort((a, b) => a - b);
  if (!list.length) return null;
  const mid = Math.floor(list.length / 2);
  return list.length % 2 ? list[mid] : (list[mid - 1] + list[mid]) / 2;
}

// AI turns (from the trace digest): medians over the run's completed turns.
// Cancelled and failed turns are counted, not timed, so a cancel never
// reads as a fast turn.
function aiMetrics(summary) {
  const trace = summary.operations?.trace;
  if (!summary.ai && !trace?.aiTurns?.length) return null;
  const all = trace?.aiTurns ?? [];
  const turns = all.filter((turn) => turn.outcome === "completed");
  const of = (read) => num(medianOf(turns.map(read)));
  const queues = (trace?.db ?? []).map((job) => job.queueWait?.medianMs).filter((value) => value != null);
  return {
    provider: summary.ai?.provider ?? null,
    behaviour: summary.ai?.behaviour ?? null,
    upstreamHost: summary.ai?.upstreamHost ?? null,
    traceSchema: trace?.available ? num(trace.schemaVersion) : null,
    turns: all.length,
    completed: turns.length,
    failed: all.filter((turn) => turn.outcome === "failed" || turn.outcome === "interrupted").length,
    cancelled: all.filter((turn) => turn.outcome === "cancelled").length,
    turnMs: of((turn) => turn.durationMs),
    prepareMs: of((turn) => turn.prepareMs),
    retrievalMs: of((turn) => turn.stages?.retrievalMs),
    contextCompileMs: of((turn) => turn.stages?.contextCompileMs),
    firstNetworkByteMs: of((turn) => turn.firstNetworkByteMs),
    firstModelDeltaMs: of((turn) => turn.firstModelDeltaMs),
    firstVisibleTextMs: of((turn) => turn.firstVisibleTextMs),
    persistMs: of((turn) => turn.persistMs),
    dbQueueWaitMs: queues.length ? num(Math.max(...queues)) : null,
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
    operationsCollected: summary.operations ? summary.operations.collected === true : null,
    ai: aiMetrics(summary),
    startup: summary.operations?.startup ? {
      firstFrameMs: num(summary.operations.startup.firstFrameMs),
      eventLoopMs: num(summary.operations.startup.eventLoopMs),
      readyMs: num(summary.operations.startup.readyMs),
    } : null,
  };
}

// A live recording's headline figures (the app's flight recorder).
function liveMetrics(summary) {
  const live = summary.live ?? {};
  const stats = live.stats ?? {};
  const windows = stats.frames?.windows ?? [];
  const busiest = windows.reduce((best, window) => (!best || (window.frames ?? 0) > (best.frames ?? 0) ? window : best), null);
  const spikes = Array.isArray(live.spikes) ? live.spikes : [];
  return {
    durationMs: num(live.durationMs),
    requestedDurationS: num(live.requestedDurationS),
    stopReason: live.stopReason ?? null,
    startedBy: live.startedBy ?? null,
    partial: live.partial === true,
    windows: windows.length,
    frames: num(stats.frames?.total),
    frameP95Ms: num(busiest?.whileDrawing?.p95Ms),
    frameGaps: spikes.filter((spike) => spike.type === "frame-gap").length,
    stalls: num(stats.eventLoop?.stalls),
    maxDelayMs: num(stats.eventLoop?.maxDelayMs),
    worstMs: spikes.length ? Math.max(...spikes.map((spike) => num(spike.durationMs) ?? 0)) : null,
    cpuMeanPercent: num(stats.cpu?.meanPercent),
    rssGrowthBytes: num(stats.memory?.growthRssBytes),
  };
}

// An audit (control/audit.mjs): what ran, the runs it made, its findings.
function auditMetrics(summary) {
  const findings = Array.isArray(summary.findings) ? summary.findings : [];
  const count = (severity) => findings.filter((finding) => finding.severity === severity).length;
  return {
    preset: summary.preset ?? null,
    presetName: summary.presetName ?? summary.preset ?? null,
    jobId: summary.jobId ?? null,
    jobStatus: summary.jobStatus ?? null,
    rulesVersion: num(summary.rulesVersion),
    plan: summary.plan ?? null,
    runIds: (summary.children ?? []).map((child) => child.runId).filter(Boolean),
    children: summary.children ?? [],
    findings,
    counts: { high: count("high"), medium: count("medium"), low: count("low"), info: count("info"), total: findings.length },
    highlights: summary.highlights ?? null,
    durationMs: num(summary.durationMs),
  };
}

// The flat record the dashboard lists, filters and compares.
export function normalizeRun(runId, summary) {
  const problems = [];
  if (!summary || typeof summary !== "object" || Array.isArray(summary)) {
    return { runId, readable: false, supported: false, kind: "unknown", status: "unreadable", problems: ["summary.json is not an object"] };
  }
  const schemaVersion = num(summary.schemaVersion);
  const supported = schemaVersion !== null && schemaVersion <= SUPPORTED_SCHEMA_VERSION;
  if (!supported) {
    problems.push(`schemaVersion ${summary.schemaVersion ?? "missing"} is not one this dashboard reads (up to ${SUPPORTED_SCHEMA_VERSION})`);
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
  } else if (supported && kind === "live-session") {
    record.live = liveMetrics(summary);
  } else if (supported && kind === "runtime-log") {
    record.runtime = { events: num(summary.timing?.recognizedEvents), summary: summary.timing?.summary ?? {} };
  } else if (supported && kind === "audit") {
    record.audit = auditMetrics(summary);
  }
  return record;
}

// The run id carries its kind (build, ui-sidebar, runtime), for runs whose
// summary can't be read.
export function kindFromId(runId) {
  const kind = runId.split("-").slice(2, -1).join("-");
  return kind === "build" ? "build" : kind.startsWith("ui-") ? "ui-scenario" : kind === "runtime" ? "runtime-log" : kind === "live" ? "live-session" : kind === "audit" ? "audit" : "unknown";
}

// A run whose summary.json could not be read at all.
export function unreadableRun(runId, error) {
  return {
    runId, readable: false, supported: false, schemaVersion: null, kind: kindFromId(runId), status: "unreadable",
    label: null, createdAt: null, finishedAt: null, problems: [error || "summary.json can't be read"],
    source: {}, machine: {}, config: {}, error: null,
  };
}
