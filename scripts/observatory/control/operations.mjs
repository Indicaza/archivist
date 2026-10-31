// The Observatory control operations: one implementation behind the CLI
// (`--json`, for agents), the browser dashboard (`--control`) and the
// in-app panel (through Archivist's IDE host, which runs `cli.mjs control`).
//
// Every operation has a declared input, validated before anything runs; a
// result in a versioned envelope; error codes instead of prose to parse;
// and a stated side effect. Nothing here runs a command a request names:
// a test always runs this repository's UI Lab scenarios, with the app binary
// from this repository's build folder.
import { readdir, readFile, stat } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { readBaselines } from "../baselines.mjs";
import { loadBudgets } from "../comparisons.mjs";
import { artifactFile, listRawRuns, loadRuns, MAX_ARTIFACT_BYTES, readRunSummary, RUN_ID } from "../dashboard/records.mjs";
import { diagnosticReport } from "../dashboard/public/diagnostic.mjs";
import { normalizeRun } from "../dashboard/public/normalize.mjs";
import { makeRedactor } from "../dashboard/public/redact.mjs";
import { buildReport, categoryOf } from "../dashboard/public/regression.mjs";
import { collectSource } from "../metadata.mjs";
import { findAppBinary, overallTimeoutMs } from "../ui-lab.mjs";
import { exportEvidence, loadRunEvidence } from "./evidence.mjs";
import { ACTIVE, cancelJob, JOB_ID, JOB_STATUSES, listJobIds, MAX_REPEAT, refreshJob, runProgress, startJob } from "./jobs.mjs";
import { LIVE_DURATIONS, LIVE_ERROR_CODES, LIVE_HTTP_STATUS, liveOperations, MAX_WINDOW_MS } from "./live-operations.mjs";

export const RESPONSE_SCHEMA = "archivist.observatory.response";
export const RESPONSE_SCHEMA_VERSION = 1;
export const CONTROL_VERSION = 1;

export const ERROR_CODES = {
  "invalid-input": "A parameter is missing, unknown or out of range.",
  "unknown-operation": "No operation has that name.",
  "not-found": "No run, job or artifact has that id.",
  "unreadable-record": "The record exists but can't be read (damaged or from a newer version).",
  busy: "Another UI test is already running; only one runs at a time.",
  "not-running": "The job has already finished.",
  "no-binary": "There is no built Archivist app to test; build it first or ask for a build.",
  "controls-disabled": "This dashboard was started read-only.",
  unauthorized: "The request did not carry this session's control key.",
  "forbidden-origin": "The request came from another web page.",
  ...LIVE_ERROR_CODES,
  internal: "Something unexpected failed; the message says what.",
};

export class ControlError extends Error {
  constructor(code, message, details = null) {
    super(message);
    this.code = code;
    this.details = details;
  }
}

export function envelope(operation, result) {
  return { schema: RESPONSE_SCHEMA, schemaVersion: RESPONSE_SCHEMA_VERSION, operation, ok: true, result };
}

export function errorEnvelope(operation, error) {
  const code = error instanceof ControlError ? error.code : "internal";
  return {
    schema: RESPONSE_SCHEMA, schemaVersion: RESPONSE_SCHEMA_VERSION, operation, ok: false,
    error: { code, message: error.message, ...(error.details ? { details: error.details } : {}) },
  };
}

export const HTTP_STATUS = {
  "invalid-input": 400, "unknown-operation": 404, "not-found": 404, "unreadable-record": 422, busy: 409, "not-running": 409,
  "no-binary": 409, "controls-disabled": 404, unauthorized: 401, "forbidden-origin": 403, internal: 500,
  ...LIVE_HTTP_STATUS,
};

// ---- input validation --------------------------------------------------------------

const SCENARIO_ID = /^[a-z][a-z0-9-]{0,31}$/;
const ARTIFACT_NAME = /^[a-z][a-zA-Z0-9-]{0,31}$/;

export function validate(spec, input) {
  if (input == null) input = {};
  if (typeof input !== "object" || Array.isArray(input)) throw new ControlError("invalid-input", "The input must be a JSON object.");
  const value = {};
  const problems = [];
  for (const key of Object.keys(input)) if (!Object.hasOwn(spec, key)) problems.push(`unknown parameter "${key}"`);
  for (const [key, rule] of Object.entries(spec)) {
    let given = input[key];
    if (given === undefined || given === null) {
      if (rule.required) problems.push(`"${key}" is required`);
      else if (rule.default !== undefined) value[key] = rule.default;
      continue;
    }
    if (rule.type === "integer") {
      if (typeof given === "string" && /^-?\d+$/.test(given)) given = Number(given);
      if (rule.enum && !rule.enum.includes(given)) {
        problems.push(`"${key}" must be one of ${rule.enum.join(", ")}`);
        continue;
      }
      if (!Number.isInteger(given) || (rule.min !== undefined && given < rule.min) || (rule.max !== undefined && given > rule.max)) {
        problems.push(`"${key}" must be a whole number from ${rule.min} to ${rule.max}`);
        continue;
      }
    } else if (rule.type === "boolean") {
      if (typeof given !== "boolean") {
        problems.push(`"${key}" must be true or false`);
        continue;
      }
    } else if (rule.type === "string") {
      if (typeof given !== "string" || given.length > 200 || (rule.pattern && !new RegExp(rule.pattern).test(given)) || (rule.enum && !rule.enum.includes(given))) {
        problems.push(`"${key}" must be ${rule.enum ? `one of ${rule.enum.join(", ")}` : `a string matching ${rule.pattern}`}`);
        continue;
      }
    } else if (rule.type === "array") {
      const pattern = new RegExp(rule.items.pattern);
      if (!Array.isArray(given) || given.length < (rule.minItems ?? 0) || given.length > rule.maxItems || !given.every((item) => typeof item === "string" && pattern.test(item))) {
        problems.push(`"${key}" must be a list of ${rule.minItems ?? 0} to ${rule.maxItems} ids matching ${rule.items.pattern}`);
        continue;
      }
    }
    value[key] = given;
  }
  if (problems.length) throw new ControlError("invalid-input", problems.join("; "), { problems });
  return value;
}

// ---- shared helpers ----------------------------------------------------------------

export async function scenarioDefinitions(scenarioDir) {
  const names = (await readdir(scenarioDir)).filter((name) => name.endsWith(".json")).sort();
  const list = [];
  for (const name of names) {
    const id = name.slice(0, -5);
    if (!SCENARIO_ID.test(id)) continue;
    const definition = JSON.parse(await readFile(path.join(scenarioDir, name), "utf8"));
    const steps = definition.steps?.length ?? 0;
    list.push({
      id,
      version: definition.version ?? null,
      name: definition.name ?? id,
      description: definition.description ?? null,
      initialConditions: definition.initialConditions ?? null,
      warmupIterations: definition.warmupIterations ?? 0,
      iterations: definition.iterations ?? 1,
      plannedActions: (definition.setup?.length ?? 0) + steps * ((definition.warmupIterations ?? 0) + (definition.iterations ?? 1)) + (definition.teardown?.length ?? 0),
      timeoutMs: overallTimeoutMs(definition),
      methods: definition.methods ?? {},
    });
  }
  return list;
}

// The app a test would run and the code it was built from.
export async function describeTarget(ctx) {
  const binary = ctx.appBinary ?? (await findAppBinary(ctx.buildDir));
  let details = null;
  if (binary) {
    try {
      const info = await stat(binary);
      details = { sizeBytes: info.size, modifiedAt: info.mtime.toISOString() };
    } catch {
      details = null;
    }
  }
  const relative = binary ? path.relative(ctx.repoRoot, binary) : null;
  return {
    binary: binary ? (relative.startsWith("..") ? path.basename(binary) : relative) : null,
    exists: Boolean(details),
    ...details,
    source: await collectSource(ctx.repoRoot),
    note: "Tests run this binary as a separate app with a disposable profile. Your open Archivist window is not touched or restarted.",
  };
}

function compactRun(run) {
  const base = {
    runId: run.runId, kind: run.kind, status: run.status, createdAt: run.createdAt, label: run.label,
    commit: run.source?.commit ?? null, branch: run.source?.branch ?? null, dirty: run.source?.dirty ?? null,
    category: categoryOf(run), problems: run.problems ?? [],
  };
  if (run.ui) {
    Object.assign(base, {
      scenario: run.ui.scenarioId, scenarioVersion: run.ui.scenarioVersion, actions: run.ui.actions, failedActions: run.ui.failedActions,
      settledMedianMs: run.ui.settled?.medianMs ?? null, settledP95Ms: run.ui.settled?.p95Ms ?? null, frameGapMedianMs: run.ui.frameGap?.medianMs ?? null,
    });
  }
  if (run.live) Object.assign(base, { durationMs: run.live.durationMs, stopReason: run.live.stopReason, partial: run.live.partial, frameGaps: run.live.frameGaps, stalls: run.live.stalls, worstMs: run.live.worstMs });
  if (run.build) Object.assign(base, { classification: run.build.classification, totalMs: run.build.totalMs, failedPhase: run.build.failedPhase });
  return base;
}

async function comparisonFor(ctx, groups) {
  const [runs, baselines, budgets] = await Promise.all([loadRuns(ctx.root), readBaselines(ctx.root), loadBudgets(ctx.root)]);
  if (baselines.error) return { report: null, problem: baselines.error, runs };
  const report = buildReport({ runs, baselines: baselines.data, budgets: budgets.budgets, candidates: groups, source: { budgets: budgets.source } });
  return { report, runs };
}

function groupByCategory(runs, runIds) {
  const groups = {};
  for (const id of runIds) {
    const run = runs.find((entry) => entry.runId === id);
    const category = run && categoryOf(run);
    if (category) (groups[category] ??= []).push(id);
  }
  return groups;
}

// The category analysis for a run (compared alone, or with `withRunIds`).
async function categoryForRun(ctx, runId, withRunIds = []) {
  const runs = await loadRuns(ctx.root);
  const run = runs.find((entry) => entry.runId === runId);
  const category = run && categoryOf(run);
  if (!category) return null;
  const ids = [...new Set([runId, ...withRunIds])].filter((id) => categoryOf(runs.find((entry) => entry.runId === id) ?? {}) === category);
  const { report } = await comparisonFor(ctx, { [category]: ids });
  const result = report?.categories[0] ?? null;
  return result?.status === "no-baseline" ? null : result;
}

export async function requireRun(ctx, runId) {
  const loaded = await readRunSummary(ctx.root, runId);
  const exists = loaded && (await stat(loaded.dir).catch(() => null))?.isDirectory();
  if (!exists) throw new ControlError("not-found", `No run ${runId} in the Observatory folder.`);
  if (!loaded.summary) throw new ControlError("unreadable-record", `Run ${runId}: ${loaded.error}`);
  return loaded;
}

async function jobView(ctx, job) {
  const view = { ...job };
  view.elapsedMs = job.startedAt ? (Date.parse(job.finishedAt ?? new Date().toISOString()) - Date.parse(job.startedAt)) : null;
  view.progress = { runsDone: job.runs.length, runsPlanned: job.plan?.totalRuns ?? null, current: null };
  if (job.current?.runId && ACTIVE.has(job.status)) {
    view.progress.current = { ...job.current, ...(await runProgress(path.join(ctx.root, "runs", job.current.runId))) };
  } else if (job.current) {
    view.progress.current = job.current;
  }
  const runs = await loadRuns(ctx.root);
  view.results = job.runs.map((entry) => {
    const run = runs.find((candidate) => candidate.runId === entry.runId);
    return run ? compactRun(run) : { runId: entry.runId, status: "missing" };
  });
  if (!ACTIVE.has(job.status) && job.runs.length) {
    const groups = groupByCategory(runs, job.runs.map((entry) => entry.runId));
    const { report } = Object.keys(groups).length ? await comparisonFor(ctx, groups) : { report: null };
    view.comparison = report ? { summary: report.summary, categories: report.categories } : null;
  }
  return view;
}

// ---- the operations -------------------------------------------------------------------

const RUN_ID_PATTERN = RUN_ID.source;
const JOB_ID_PATTERN = JOB_ID.source;

export const OPERATIONS = {
  "observatory.capabilities": {
    description: "What this interface can do: operations, inputs, side effects, error codes, limits, and the app a test would run.",
    cli: "npm run observatory -- capabilities --json",
    input: {},
    sideEffects: "none",
    async run(ctx) {
      return {
        controlVersion: CONTROL_VERSION,
        observatoryRoot: ctx.root,
        operations: Object.entries(OPERATIONS).map(([name, op]) => ({ name, description: op.description, cli: op.cli, input: op.input, sideEffects: op.sideEffects })),
        errorCodes: ERROR_CODES,
        jobStatuses: JOB_STATUSES,
        limits: { maxRepeat: MAX_REPEAT, concurrentUiTests: 1, maxArtifactBytes: MAX_ARTIFACT_BYTES, liveDurationsSeconds: LIVE_DURATIONS, concurrentLiveRecordings: 1, maxLiveWindowMs: MAX_WINDOW_MS },
        exitCodes: { 0: "ok", 1: "the operation failed (see error.code)", 2: "invalid input or unknown operation", 4: "busy: another UI test is running" },
        target: await describeTarget(ctx),
        scenarios: (await scenarioDefinitions(ctx.scenarioDir)).map(({ id, name, version }) => ({ id, name, version })),
      };
    },
  },
  "observatory.scenarios.list": {
    description: "The UI Lab scenarios a test can run, with what each does and how many actions it plans.",
    cli: "npm run observatory -- scenarios list --json",
    input: {},
    sideEffects: "none",
    async run(ctx) {
      return { scenarios: await scenarioDefinitions(ctx.scenarioDir) };
    },
  },
  "observatory.tests.run": {
    description: "Start UI scenarios in the background (one test at a time), each in a fresh disposable profile. Returns the job at once; poll observatory.tests.status.",
    cli: "npm run observatory -- tests run [--scenario sidebar|workspace|all] [--repeat <n>] [--build] [--wait] --json",
    input: {
      scenario: { type: "string", pattern: "^(all|[a-z][a-z0-9-]{0,31})$", default: "all", description: "A scenario id, or all" },
      repeat: { type: "integer", min: 1, max: MAX_REPEAT, default: 1, description: "Rounds of every chosen scenario" },
      build: { type: "boolean", default: false, description: "Build first (npm run observatory:build). Off: test the binary already built." },
    },
    sideEffects: "Starts a background job that runs the Archivist binary from build/frontend-debug as a separate app with a disposable profile, and records one Observatory run per scenario and round. With build: true it first rebuilds build/frontend-debug (it never restarts a running Archivist).",
    async run(ctx, input) {
      const scenarios = await scenarioDefinitions(ctx.scenarioDir);
      const chosen = input.scenario === "all" ? scenarios.map((entry) => entry.id) : [input.scenario];
      if (!chosen.every((id) => scenarios.some((entry) => entry.id === id))) {
        throw new ControlError("invalid-input", `Unknown scenario "${input.scenario}". Available: all, ${scenarios.map((entry) => entry.id).join(", ")}.`);
      }
      const target = await describeTarget(ctx);
      if (!target.exists && !input.build) throw new ControlError("no-binary", "No Archivist app has been built in build/frontend-debug. Run it with build: true, or npm run observatory:build first.");
      const started = await startJob(ctx.root, { scenarios: chosen, repeat: input.repeat, build: input.build }, { cliPath: ctx.cliPath, target, env: ctx.env });
      if (started.busy) {
        throw new ControlError("busy", started.busy.jobId ? `Job ${started.busy.jobId} is running a UI test.` : "A UI test started from the command line is running.", { holder: started.busy });
      }
      return { job: await jobView(ctx, started.job) };
    },
  },
  "observatory.tests.status": {
    description: "A job's state, progress (actions done of planned, from the app's own records), measurements so far and, once finished, its results and baseline comparison. Without jobId: the newest job.",
    cli: "npm run observatory -- tests status [<job-id>] --json",
    input: { jobId: { type: "string", pattern: JOB_ID_PATTERN } },
    sideEffects: "Marks a job interrupted if its process is gone.",
    async run(ctx, input) {
      const jobId = input.jobId ?? (await listJobIds(ctx.root)).at(-1);
      if (!jobId) return { job: null };
      const loaded = await refreshJob(ctx.root, jobId);
      if (!loaded) throw new ControlError("not-found", `No job ${jobId}.`);
      if (!loaded.job) throw new ControlError("unreadable-record", `Job ${jobId}: ${loaded.error}`);
      return { job: await jobView(ctx, loaded.job) };
    },
  },
  "observatory.tests.cancel": {
    description: "Stop a running job. Only the job's own runner and the test app it started are signalled; the run is recorded as interrupted and the job as cancelled.",
    cli: "npm run observatory -- tests cancel <job-id> --json",
    input: { jobId: { type: "string", pattern: JOB_ID_PATTERN, required: true } },
    sideEffects: "Sends SIGINT to the job's runner process after checking it is that job's runner.",
    async run(ctx, input) {
      const result = await cancelJob(ctx.root, input.jobId);
      if (result.error === "not-found") throw new ControlError("not-found", `No job ${input.jobId}.`);
      if (result.error === "unreadable-record") throw new ControlError("unreadable-record", result.message);
      if (result.error === "not-running") throw new ControlError("not-running", `Job ${input.jobId} already finished (${result.job.status}).`, { status: result.job.status });
      return { job: await jobView(ctx, result.job), cancelRequested: true };
    },
  },
  "observatory.tests.list": {
    description: "Recent jobs, newest first.",
    cli: "npm run observatory -- tests list --json",
    input: { limit: { type: "integer", min: 1, max: 100, default: 20 } },
    sideEffects: "Marks jobs interrupted if their process is gone.",
    async run(ctx, input) {
      const ids = (await listJobIds(ctx.root)).reverse().slice(0, input.limit);
      const jobs = [];
      for (const id of ids) {
        const loaded = await refreshJob(ctx.root, id);
        jobs.push(loaded?.job
          ? { jobId: id, status: loaded.job.status, createdAt: loaded.job.createdAt, finishedAt: loaded.job.finishedAt, scenarios: loaded.job.plan?.scenarios, repeat: loaded.job.plan?.repeat, runIds: loaded.job.runs.map((entry) => entry.runId), error: loaded.job.error }
          : { jobId: id, status: "unreadable", error: loaded?.error ?? null });
      }
      return { jobs };
    },
  },
  "observatory.runs.list": {
    description: "Recorded runs, newest first, as compact records (status, identity and headline measurements).",
    cli: "npm run observatory -- runs list [--kind build|ui-scenario|runtime-log|live-session] [--scenario <id>] [--status <s>] [--limit <n>] --json",
    input: {
      kind: { type: "string", enum: ["build", "ui-scenario", "runtime-log", "live-session"] },
      scenario: { type: "string", pattern: SCENARIO_ID.source },
      status: { type: "string", pattern: "^[a-z-]{1,20}$" },
      limit: { type: "integer", min: 1, max: 500, default: 20 },
    },
    sideEffects: "none",
    async run(ctx, input) {
      const all = await loadRuns(ctx.root);
      const runs = all
        .filter((run) => !input.kind || run.kind === input.kind)
        .filter((run) => !input.scenario || run.ui?.scenarioId === input.scenario)
        .filter((run) => !input.status || run.status === input.status);
      return { total: runs.length, runs: runs.slice(0, input.limit).map(compactRun) };
    },
  },
  "observatory.runs.get": {
    description: "One run: its compact record, the full summary.json as stored, and its artifacts with sizes.",
    cli: "npm run observatory -- show <run-id> --json",
    input: { runId: { type: "string", pattern: RUN_ID_PATTERN, required: true } },
    sideEffects: "none",
    async run(ctx, input) {
      const loaded = await requireRun(ctx, input.runId);
      const artifacts = [];
      for (const [name, file] of Object.entries(loaded.summary.artifacts ?? {})) {
        if (typeof file !== "string") continue;
        const resolved = await artifactFile(loaded.dir, loaded.summary, name);
        let bytes = null;
        if (resolved) bytes = (await stat(resolved).catch(() => null))?.size ?? null;
        artifacts.push({ name, file, bytes, available: Boolean(resolved) });
      }
      return { run: compactRun(normalizeRun(input.runId, loaded.summary)), summary: loaded.summary, artifacts };
    },
  },
  "observatory.runs.artifact": {
    description: "Part of one artifact a run lists (events, log, scenario): the last lines by default, redacted unless raw is true.",
    cli: "npm run observatory -- runs artifact <run-id> <name> [--tail <lines>] [--raw] --json",
    input: {
      runId: { type: "string", pattern: RUN_ID_PATTERN, required: true },
      name: { type: "string", pattern: ARTIFACT_NAME.source, required: true },
      tailLines: { type: "integer", min: 1, max: 5000, default: 200 },
      raw: { type: "boolean", default: false },
    },
    sideEffects: "none",
    async run(ctx, input) {
      const loaded = await requireRun(ctx, input.runId);
      const file = await artifactFile(loaded.dir, loaded.summary, input.name);
      if (!file) throw new ControlError("not-found", `Run ${input.runId} lists no artifact "${input.name}" (or its file is missing).`);
      const size = (await stat(file)).size;
      if (size > MAX_ARTIFACT_BYTES) throw new ControlError("invalid-input", `The artifact is ${size} bytes, over the ${MAX_ARTIFACT_BYTES} limit.`);
      const lines = (await readFile(file, "utf8")).split(/\r?\n/);
      if (lines.at(-1) === "") lines.pop();
      const redact = input.raw ? (text) => text : makeRedactor({ root: ctx.root, repo: ctx.repoRoot, home: os.homedir() });
      const tail = lines.slice(-input.tailLines).map(redact);
      return { runId: input.runId, name: input.name, bytes: size, totalLines: lines.length, fromLine: lines.length - tail.length + 1, redacted: !input.raw, lines: tail };
    },
  },
  "observatory.runs.compare": {
    description: "Compare runs with their category's baseline (the same rules as baseline compare). Nothing is saved.",
    cli: "npm run observatory -- runs compare <run-id> [<run-id> ...] --json",
    input: { runIds: { type: "array", items: { pattern: RUN_ID_PATTERN }, minItems: 1, maxItems: 50, required: true } },
    sideEffects: "none",
    async run(ctx, input) {
      const runs = await listRawRuns(ctx.root);
      const missing = input.runIds.filter((id) => !runs.some((run) => run.runId === id));
      if (missing.length) throw new ControlError("not-found", `No run ${missing.join(", ")}.`);
      const normalized = await loadRuns(ctx.root);
      const groups = groupByCategory(normalized, input.runIds);
      const uncategorized = input.runIds.filter((id) => !Object.values(groups).flat().includes(id));
      if (!Object.keys(groups).length) throw new ControlError("invalid-input", "None of these runs has a baseline category (only classified builds and UI scenario runs do).");
      const { report, problem } = await comparisonFor(ctx, groups);
      if (!report) throw new ControlError("unreadable-record", problem);
      return { report, uncategorized };
    },
  },
  "observatory.reports.generate": {
    description: "The Copy for AI diagnostic report for a run: compact Markdown plus the same facts as data. Paths and secrets are redacted; raw logs are never included.",
    cli: "npm run observatory -- report <run-id> [--json]",
    input: {
      runId: { type: "string", pattern: RUN_ID_PATTERN, required: true },
      withRunIds: { type: "array", items: { pattern: RUN_ID_PATTERN }, maxItems: 50, default: [], description: "Other runs of the same kind to compare with the baseline together (e.g. a job's rounds)." },
    },
    sideEffects: "none",
    async run(ctx, input) {
      await requireRun(ctx, input.runId);
      const evidence = await loadRunEvidence(ctx.root, input.runId);
      const category = await categoryForRun(ctx, input.runId, input.withRunIds);
      const report = diagnosticReport({
        runId: input.runId, summary: evidence.loaded.summary, events: evidence.events, scenario: evidence.scenario, category,
        logText: evidence.log?.text ?? null, live: evidence.live, redact: makeRedactor({ root: ctx.root, repo: ctx.repoRoot, home: os.homedir() }),
      });
      return { markdown: report.markdown, facts: report.facts };
    },
  },
  "observatory.reports.export": {
    description: "Write an evidence bundle for a run (report, facts, redacted summary, UI events, scenario, comparison, log excerpt, manifest) as a folder and a .zip in <Observatory folder>/exports.",
    cli: "npm run observatory -- export <run-id> [--raw-logs] --json",
    input: {
      runId: { type: "string", pattern: RUN_ID_PATTERN, required: true },
      includeRawLogs: { type: "boolean", default: false, description: "Also include the full, unredacted log (may contain private paths or names)." },
    },
    sideEffects: "Writes <Observatory folder>/exports/<export-id>/ and <export-id>.zip. Never changes the run.",
    async run(ctx, input) {
      await requireRun(ctx, input.runId);
      const category = await categoryForRun(ctx, input.runId);
      const result = await exportEvidence(ctx.root, input.runId, { category, includeRawLogs: input.includeRawLogs });
      if (result.error) throw new ControlError("unreadable-record", result.error);
      return result;
    },
  },
  ...liveOperations({ ControlError, requireRun, runIdPattern: RUN_ID_PATTERN }),
};

// One call: validate, run, wrap. Never throws.
export async function dispatch(ctx, operation, input) {
  const op = Object.hasOwn(OPERATIONS, operation) ? OPERATIONS[operation] : null;
  if (!op) return errorEnvelope(operation, new ControlError("unknown-operation", `Unknown operation "${operation}". See observatory.capabilities.`));
  try {
    return envelope(operation, await op.run(ctx, validate(op.input, input)));
  } catch (error) {
    return errorEnvelope(operation, error);
  }
}

export function exitCodeFor(response) {
  if (response.ok) return 0;
  if (response.error.code === "invalid-input" || response.error.code === "unknown-operation") return 2;
  if (response.error.code === "busy") return 4;
  return 1;
}
