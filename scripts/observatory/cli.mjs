#!/usr/bin/env node
// Archivist Observatory: local build and runtime measurement records.
//   node scripts/observatory/cli.mjs build [--label <text>] [--follow]
//   node scripts/observatory/cli.mjs ingest-runtime <log> [--label <text>] [--build-run <runId>]
//   node scripts/observatory/cli.mjs ui [--scenario <name>] [--skip-build] [--keep-profile]
//   node scripts/observatory/cli.mjs show [runId]
//   node scripts/observatory/cli.mjs dashboard [--port <n>] [--no-open]
//   node scripts/observatory/cli.mjs baseline set|list|remove|compare ...
//   node scripts/observatory/cli.mjs check [--no-collect] [--strict] [--repeat <n>]
//   node scripts/observatory/cli.mjs capabilities | scenarios | tests | runs | report | export | control
//   (see USAGE; --json gives the versioned envelopes in control/operations.mjs)
import { copyFile, readFile } from "node:fs/promises";
import path from "node:path";
import { parseArgs } from "node:util";
import { collectBuildEvidence } from "./build-evidence.mjs";
import { summarizeTraces } from "./time-trace.mjs";
import { analyzeOperations } from "./ui-operations.mjs";
import { archivistBuildPhases, buildConditionsBefore, classifyBuild } from "./build-phases.mjs";
import { exitCodeFor, runCapture } from "./capture.mjs";
import { collectMachine, collectSource, collectToolchain } from "./metadata.mjs";
import { phaseLine, formatBuildSummary } from "./report.mjs";
import { parseRuntimeLog, summarizeEvents } from "./runtime-log.mjs";
import {
  binaryIdentity, classifyRun, createProfile, exitCodeForStatus, findAppBinary, labEnvironment, overallTimeoutMs,
  readEvents, removeProfile, runApp,
} from "./ui-lab.mjs";
import { spawn } from "node:child_process";
import { createDashboardServer } from "./dashboard/server.mjs";
import { baselineCommand, checkReport, compareAfterRun, loadBudgets } from "./comparisons.mjs";
import { formatUiSummary } from "./ui-report.mjs";
import { acquireLock, readJob, releaseLock, setStatus, writeJob, ACTIVE } from "./control/jobs.mjs";
import { dispatch, exitCodeFor as responseExitCode, scenarioDefinitions } from "./control/operations.mjs";
import { writeAuditRecord } from "./control/audit.mjs";
import { formatResponse } from "./control/human.mjs";
import { newControlToken } from "./control/gate.mjs";
import { BUILD_DIR, CLI_PATH, REPO_ROOT, SCENARIO_DIR, TIME_TRACE_DIR } from "./paths.mjs";
const DEFAULT_BUILD_DIR = BUILD_DIR;
import { summarizeActions } from "./ui-stats.mjs";
import {
  SCHEMA_VERSION, closeLog, createRunDir, listRunIds, observatoryRoot, openLog, readSummary, runsDir, writeJsonAtomic,
} from "./storage.mjs";

const ROOT = REPO_ROOT;

const USAGE = `Archivist Observatory

  npm run observatory:build [-- --label <text>] [--follow]
  npm run observatory -- ingest-runtime <log> [--label <text>] [--build-run <runId>]
  npm run observatory:ui [-- --scenario sidebar|workspace|chat|startup] [--skip-build] [--keep-profile]
  npm run observatory -- show [runId]
  npm run observatory:dashboard [-- --port <n>] [--no-open] [--control]

  npm run observatory -- baseline set <run-id> [<run-id> ...] [--note <text>]
  npm run observatory -- baseline list
  npm run observatory -- baseline remove <category>
  npm run observatory -- baseline compare <run-id> [<run-id> ...] [--json]
  npm run observatory:check [-- --no-collect] [--strict] [--repeat <n>]
                            [--scenario <name>] [--skip-build] [--json] [--budgets <file>]

  check records a build and each UI scenario (--repeat times), then compares
  every category that has a baseline with its newest compatible runs.
  --no-collect only compares runs already recorded. Exit codes: 0 done
  (including inconclusive results), 1 a functional failure, 5 a confirmed
  regression with --strict.

Controls (the same operations the dashboard's Test Lab uses; add --json
for a versioned JSON envelope on stdout and nothing else):

  npm run observatory -- capabilities
  npm run observatory -- scenarios list
  npm run observatory -- tests run [--scenario sidebar|workspace|chat|startup|all] [--repeat <n>] [--build] [--wait]
  npm run observatory -- tests status [<job-id>]
  npm run observatory -- tests cancel <job-id>
  npm run observatory -- tests list
  npm run observatory -- runs list [--kind <k>] [--scenario <id>] [--status <s>] [--limit <n>]
  npm run observatory -- show <run-id> --json
  npm run observatory -- runs artifact <run-id> <events|log|scenario> [--tail <lines>] [--raw]
  npm run observatory -- runs compare <run-id> [<run-id> ...]
  npm run observatory -- report <run-id>                   (Markdown for an AI)
  npm run observatory -- export <run-id> [--raw-logs]      (evidence .zip)
  npm run observatory:dashboard -- --control               (Test Lab in the browser)

Audits (one press: build and/or UI scenarios, then findings with the rules
that produced them; Observatory > Record in the dashboard):

  npm run observatory -- audit presets
  npm run observatory -- audit run --preset quick|ui|build|complete [--repeat <n>] [--wait]
  npm run observatory -- audit status [<job-id>]
  npm run observatory -- audit cancel <job-id>
  npm run observatory -- audit list [--preset <p>] [--limit <n>]
  npm run observatory -- audit findings <audit-id> [--severity <s>] [--subsystem <name>]
  npm run observatory -- audit compare <audit-id> [<baseline-audit-id>]
  npm run observatory -- audit report <audit-id>          (Markdown for an AI)
  npm run observatory -- audit timeline <run-id> [--from <ms>] [--to <ms>] [--limit <n>]
  npm run observatory -- audit operation <run-id> <action-index>
  npm run observatory -- audit export <audit-id> [--raw-logs]

  The build and complete presets run an incremental build of
  build/frontend-debug; nothing is cleaned or deleted.

Live recordings (the app's flight recorder; Observatory > Live in Archivist):

  npm run observatory -- live status
  npm run observatory -- live record [--duration 15|30|60|120] [--label <text>]
                                       (start, wait for the end, print the result)
  npm run observatory -- live start [--duration <s>] [--label <text>]
  npm run observatory -- live mark <note>
  npm run observatory -- live stop
  npm run observatory -- live wait [--timeout <s>]
  npm run observatory -- live list [--limit <n>]
  npm run observatory -- live get <run-id>
  npm run observatory -- live spikes <run-id> [--limit <n>]
  npm run observatory -- live window <run-id> --from <ms> --to <ms>
  npm run observatory -- live compare <base-run-id> <run-id>

  status/record/start/mark/stop/wait reach the running Archivist only while
  "Let agents record" is on in its Observatory > Live panel.

  Exit codes: 0 ok, 1 failed, 2 invalid input, 4 another UI test is running;
  tests run --wait: 0 completed, 1 failed, 70 interrupted, 124 timed out,
  130 cancelled.

Records are stored in $ARCHIVIST_OBSERVATORY_DIR, or by default
${observatoryRoot()}`;

async function buildCommand(options) {
  const root = observatoryRoot();
  // --time-trace: the opt-in profiling build in its own directory.
  const timeTrace = options["time-trace"] === true;
  const BUILD_DIR = timeTrace ? TIME_TRACE_DIR : DEFAULT_BUILD_DIR;
  const { runId, dir } = await createRunDir(root, "build");
  const summaryPath = path.join(dir, "summary.json");
  const logPath = path.join(dir, "build.log");
  const createdAt = new Date().toISOString();
  const [before, source, environment] = await Promise.all([
    buildConditionsBefore(BUILD_DIR),
    collectSource(ROOT),
    collectMachine(),
  ]);

  const summary = {
    schemaVersion: SCHEMA_VERSION,
    kind: "build",
    runId,
    label: options.label ?? null,
    createdAt,
    finishedAt: null,
    status: "running",
    exitCode: null,
    totalDurationMs: null,
    clock: "monotonic",
    source,
    environment,
    toolchain: null,
    buildConditions: { buildDir: path.relative(ROOT, BUILD_DIR), before, classification: null, profiling: timeTrace ? "clang-time-trace" : null },
    phases: [],
    errors: [],
    artifacts: { log: "build.log" },
  };
  await writeJsonAtomic(summaryPath, summary);

  const log = openLog(logPath);
  log.write(`Archivist Observatory build capture\nRun: ${runId}\nStarted: ${createdAt}\n`);
  console.log(`Observatory run ${runId}${options.follow ? "" : " (full output in build.log)"}`);

  let state;
  const evidence = {};
  try {
    state = await runCapture(archivistBuildPhases({ root: ROOT, buildDir: BUILD_DIR, evidence, timeTrace }), {
      log,
      echo: options.follow,
      onPhaseDone: (phase) => console.log(`  ${phaseLine(phase)}`),
      onProgress: async (progress) => {
        summary.phases = progress.phases;
        summary.totalDurationMs = progress.totalDurationMs;
        await writeJsonAtomic(summaryPath, summary);
      },
    });
  } catch (error) {
    // A collector bug must not lose what was already measured.
    state = { status: "error", phases: summary.phases, totalDurationMs: summary.totalDurationMs };
    summary.errors.push({ phase: null, message: `Observatory collector error: ${error.message}` });
  }

  summary.phases = state.phases;
  summary.status = state.status;
  summary.totalDurationMs = state.totalDurationMs;
  summary.exitCode = state.status === "error" ? 1 : exitCodeFor(state);
  summary.finishedAt = new Date().toISOString();
  summary.toolchain = await collectToolchain(BUILD_DIR);
  summary.buildConditions.classification = classifyBuild(before, state.phases);
  if (evidence.explain) {
    try {
      const collected = await collectBuildEvidence({ root: ROOT, buildDir: BUILD_DIR, ...evidence });
      await writeJsonAtomic(path.join(dir, "build-evidence.json"), collected.artifact);
      summary.evidence = collected.summary;
      summary.artifacts.evidence = "build-evidence.json";
      if (timeTrace) {
        const compiled = collected.artifact.jobs.filter((job) => job.category === "compile").map((job) => job.output);
        const traces = await summarizeTraces(BUILD_DIR, compiled, { relative: (file) => file.startsWith(`${ROOT}/`) ? file.slice(ROOT.length + 1) : file });
        await writeJsonAtomic(path.join(dir, "time-trace.json"), traces);
        summary.artifacts.timeTrace = "time-trace.json";
        summary.timeTrace = { ...traces, headers: traces.headers.slice(0, 10), templates: traces.templates.slice(0, 10), slowestUnits: traces.slowestUnits.slice(0, 10) };
      }
    } catch (error) {
      summary.errors.push({ phase: null, message: `Build evidence could not be collected: ${error.message}` });
    }
  }
  for (const phase of state.phases) {
    if (phase.error) {
      summary.errors.push({ phase: phase.name, message: phase.error.message });
    }
  }
  await writeJsonAtomic(summaryPath, summary);
  log.write(`\nFinished: ${summary.finishedAt}\nStatus: ${summary.status}\n`);
  await closeLog(log);

  console.log(formatBuildSummary(summary, { summaryPath, logPath }));
  return { exitCode: summary.exitCode, runId };
}

async function scenarioNames() {
  return (await scenarioDefinitions(SCENARIO_DIR)).map((entry) => entry.id);
}

// A test binary set by whoever started the controller (the test suite uses
// it for the stand-in app). Never taken from a request.
function appOverride() {
  return process.env.ARCHIVIST_OBSERVATORY_APP ? path.resolve(process.env.ARCHIVIST_OBSERVATORY_APP) : null;
}

// One UI test at a time, whether it was started here, by a job or by the
// dashboard. A job already holds the lock for its own runs.
async function uiCommand(options, collected = []) {
  if (options.jobId) return uiCommandLocked(options, collected);
  const root = observatoryRoot();
  const lock = await acquireLock(root, { owner: "cli", pid: process.pid });
  if (!lock.ok) {
    console.error(`Another UI test is running (${lock.holder?.jobId ? `job ${lock.holder.jobId}` : `process ${lock.holder?.pid}`}). Wait for it, or: npm run observatory -- tests cancel <job-id>`);
    return 4;
  }
  try {
    return await uiCommandLocked(options, collected);
  } finally {
    await releaseLock(root, { owner: "cli", pid: process.pid });
  }
}

async function uiCommandLocked(options, collected) {
  const available = await scenarioNames();
  const names = options.scenario ? [options.scenario] : available;
  const unknown = names.filter((name) => !available.includes(name));
  if (unknown.length) {
    console.error(`Unknown scenario: ${unknown.join(", ")}. Available: ${available.join(", ")}`);
    return 2;
  }

  let buildRunId = null;
  if (!options["skip-build"]) {
    // The app that runs is the one just built from this checkout.
    const build = await buildCommand({ label: options.label ?? "ui-lab", follow: false });
    if (build.exitCode !== 0) {
      console.error("The build failed, so no UI scenario ran.");
      return build.exitCode;
    }
    buildRunId = build.runId;
  }
  const binary = options.app ? path.resolve(options.app) : appOverride() ?? await findAppBinary(BUILD_DIR);
  if (!binary) {
    console.error(`No Archivist binary in ${path.relative(ROOT, BUILD_DIR)}. Run npm run observatory:build first.`);
    return 2;
  }

  let exitCode = 0;
  for (const name of names) {
    const { exitCode: result, runId } = await uiScenarioCommand(name, { ...options, binary, buildRunId });
    collected.push(runId);
    exitCode ||= result;
    if (result === 130) {
      break;
    }
  }
  return exitCode;
}

async function uiScenarioCommand(name, options) {
  const root = observatoryRoot();
  const scenario = JSON.parse(await readFile(path.join(SCENARIO_DIR, `${name}.json`), "utf8"));
  const { runId, dir } = await createRunDir(root, `ui-${name}`);
  await options.onRunCreated?.(runId);
  const summaryPath = path.join(dir, "summary.json");
  const eventsPath = path.join(dir, "ui-events.json");
  const logPath = path.join(dir, "runtime.log");
  const scenarioPath = path.join(dir, "scenario.json");
  // The exact definition that ran, so later runs can replay it.
  await writeJsonAtomic(scenarioPath, scenario);

  const [source, environment, toolchain, binary] = await Promise.all([
    collectSource(ROOT),
    collectMachine(),
    collectToolchain(BUILD_DIR),
    binaryIdentity(options.binary).catch(() => null),
  ]);
  const summary = {
    schemaVersion: SCHEMA_VERSION,
    kind: "ui-scenario",
    runId,
    label: options.label ?? null,
    createdAt: new Date().toISOString(),
    finishedAt: null,
    status: "running",
    exitCode: null,
    error: null,
    clock: "monotonic",
    source,
    environment,
    build: {
      binary: path.relative(ROOT, options.binary).startsWith("..") ? path.basename(options.binary) : path.relative(ROOT, options.binary),
      ...binary,
      toolchain,
      links: options.buildRunId ? [{ runId: options.buildRunId, relationship: "built-by-this-command" }] : [],
    },
    scenario: {
      id: scenario.id,
      version: scenario.version,
      name: scenario.name,
      seed: scenario.seed ?? null,
      warmupIterations: scenario.warmupIterations ?? 0,
      iterations: scenario.iterations ?? 1,
      definition: "scenario.json",
    },
    app: null,
    actions: null,
    stats: null,
    runtimeTiming: null,
    profile: { location: "temporary directory", removed: false },
    artifacts: { events: "ui-events.json", log: "runtime.log", scenario: "scenario.json" },
  };
  await writeJsonAtomic(summaryPath, summary);
  console.log(`UI Lab: ${scenario.name}, run ${runId}`);

  const profileDir = await createProfile();
  const app = await runApp({
    command: options.binary,
    env: labEnvironment(process.env, { profileDir, scenarioFile: scenarioPath, eventsFile: eventsPath }),
    logFile: logPath,
    timeoutMs: options.timeout ? Number(options.timeout) * 1000 : overallTimeoutMs(scenario),
  });
  const events = await readEvents(eventsPath);
  const outcome = classifyRun(app, events);
  const actions = events?.actions ?? [];

  let logText = "";
  try {
    logText = await readFile(logPath, "utf8");
  } catch {
    // no log is reported below
  }
  const timing = parseRuntimeLog(logText);

  summary.status = outcome.status;
  summary.error = outcome.message;
  summary.exitCode = exitCodeForStatus(outcome.status);
  summary.finishedAt = new Date().toISOString();
  summary.app = {
    exitCode: app.exitCode,
    signal: app.signal,
    durationMs: app.durationMs,
    readyMs: events?.readyMs ?? null,
    driverVersion: events?.driverVersion ?? null,
    driverStatus: events?.status ?? null,
    isolation: events?.isolation ?? null,
  };
  summary.actions = {
    planned: events?.plannedActions ?? null,
    total: actions.length,
    passed: actions.filter((action) => action.ok).length,
    failed: actions.filter((action) => !action.ok).length,
    warmup: actions.filter((action) => action.phase === "warmup").length,
    firstFailure: actions.find((action) => !action.ok) ?? null,
  };
  summary.stats = summarizeActions(actions);
  // What happened inside each action (LiveEvents spans, stalls), if this app build reports them.
  const operations = analyzeOperations(events);
  summary.operations = operations.summary;
  if (operations.artifact) {
    await writeJsonAtomic(path.join(dir, "ui-operations.json"), operations.artifact);
    summary.artifacts.operations = "ui-operations.json";
  }
  summary.runtimeTiming = {
    category: "archivist.wells.timing",
    relationship: "same-process",
    recognizedEvents: timing.events.length,
    unrecognizedLines: timing.unrecognizedLines,
    summary: summarizeEvents(timing.events),
    events: timing.events,
  };

  if (options["keep-profile"]) {
    summary.profile = { location: profileDir, removed: false };
  } else {
    try {
      await removeProfile(profileDir);
      summary.profile.removed = true;
    } catch (error) {
      summary.profile = { location: profileDir, removed: false, error: error.message };
    }
  }
  await writeJsonAtomic(summaryPath, summary);
  console.log(formatUiSummary(summary, { summaryPath, eventsPath, logPath }));
  return { exitCode: summary.exitCode, runId };
}

async function ingestRuntimeCommand(file, options) {
  if (!file) {
    console.error("ingest-runtime needs a log file.\n");
    console.error(USAGE);
    return 2;
  }
  const root = observatoryRoot();
  let text;
  try {
    text = await readFile(file, "utf8");
  } catch (error) {
    console.error(`Cannot read runtime log: ${error.message}`);
    return 2;
  }
  const links = [];
  if (options["build-run"]) {
    try {
      await readSummary(root, options["build-run"]);
    } catch {
      console.error(`No Observatory run named ${options["build-run"]}.`);
      return 2;
    }
    // Only recorded because the developer said so; never inferred.
    links.push({ runId: options["build-run"], relationship: "declared-by-user" });
  }

  const { runId, dir } = await createRunDir(root, "runtime");
  await copyFile(file, path.join(dir, "runtime.log"));
  const parsed = parseRuntimeLog(text);
  const summary = {
    schemaVersion: SCHEMA_VERSION,
    kind: "runtime-log",
    runId,
    label: options.label ?? null,
    createdAt: new Date().toISOString(),
    status: "success",
    links,
    input: { fileName: path.basename(file), totalLines: parsed.totalLines },
    timing: {
      category: "archivist.wells.timing",
      categoryLines: parsed.categoryLines,
      recognizedEvents: parsed.events.length,
      unrecognizedLines: parsed.unrecognizedLines,
      summary: summarizeEvents(parsed.events),
      events: parsed.events,
    },
    artifacts: { log: "runtime.log" },
  };
  const summaryPath = path.join(dir, "summary.json");
  await writeJsonAtomic(summaryPath, summary);
  printRuntimeSummary(summary, summaryPath);
  return 0;
}

function printRuntimeSummary(summary, summaryPath) {
  const { timing } = summary;
  console.log(`\nARCHIVIST OBSERVATORY: runtime log\n${"─".repeat(45)}`);
  console.log(`Events:      ${timing.recognizedEvents} recognized, ${timing.unrecognizedLines.length} unrecognized timing lines`);
  for (const [operation, stats] of Object.entries(timing.summary)) {
    console.log(`${operation.padEnd(12)} ${stats.count}× mean ${stats.meanMs} ms, max ${stats.maxMs} ms (${stats.field})`);
  }
  if (!timing.categoryLines) {
    console.log("No archivist.wells.timing lines. Was QT_LOGGING_RULES=\"archivist.wells.timing=true\" set?");
  }
  if (summary.links.length) {
    console.log(`Build run:   ${summary.links[0].runId} (declared by you)`);
  }
  console.log(`Report:      ${summaryPath}\n${"─".repeat(45)}\n`);
}

async function showCommand(runId) {
  const root = observatoryRoot();
  const id = runId ?? (await listRunIds(root)).at(-1);
  if (!id) {
    console.log(`No Observatory runs in ${runsDir(root)}`);
    return 0;
  }
  const { file, summary } = await readSummary(root, id);
  if (summary.kind === "ui-scenario") {
    const dir = path.dirname(file);
    console.log(formatUiSummary(summary, {
      summaryPath: file,
      eventsPath: path.join(dir, summary.artifacts.events),
      logPath: path.join(dir, summary.artifacts.log),
    }));
  } else if (summary.kind === "build") {
    console.log(formatBuildSummary(summary, { summaryPath: file, logPath: path.join(path.dirname(file), summary.artifacts.log) }));
  } else {
    printRuntimeSummary(summary, file);
  }
  return 0;
}

// Opt-in performance check: record (unless --no-collect), then compare
// everything that has a baseline. Inconclusive results never fail it.
async function checkCommand(options) {
  const root = observatoryRoot();
  if (options.collect) {
    let repeat = Number(options.repeat ?? NaN);
    if (options.repeat !== undefined && (!Number.isInteger(repeat) || repeat < 1 || repeat > 20)) {
      console.error("--repeat must be a whole number from 1 to 20.");
      return 2;
    }
    if (!Number.isInteger(repeat)) {
      // Enough runs for the budgets' evidence rule, by default.
      const { budgets } = await loadBudgets(root, options.budgets);
      repeat = Math.max(1, ...Object.values(budgets.categories).filter((entry) => entry.metrics["settled.median"]).map((entry) => entry.minSamples));
    }
    let skipBuild = options["skip-build"];
    for (let round = 1; round <= repeat; round += 1) {
      console.log(`\nObservatory check: UI round ${round} of ${repeat}`);
      const collected = [];
      const exitCode = await uiCommand({ ...options, "skip-build": skipBuild, label: options.label ?? "check" }, collected);
      // The first round builds (unless --skip-build); later rounds reuse it.
      skipBuild = true;
      if (exitCode === 130 || (exitCode !== 0 && !collected.length)) {
        console.error("Collection stopped; comparing what was recorded so far.");
        break;
      }
    }
  }
  return checkReport(root, options);
}

// ---- control operations (agents and the dashboard share these) -------------

function controlContext() {
  return {
    root: observatoryRoot(), repoRoot: ROOT, buildDir: BUILD_DIR, scenarioDir: SCENARIO_DIR, cliPath: CLI_PATH,
    appBinary: appOverride(), env: process.env,
  };
}

// --json: stdout carries only the JSON document. Anything the existing
// commands print while working goes to stderr instead.
function jsonStdout() {
  const write = process.stdout.write.bind(process.stdout);
  process.stdout.write = (chunk, encoding, callback) => process.stderr.write(chunk, encoding, callback);
  return (value) => write(`${JSON.stringify(value, null, 2)}\n`);
}

async function runOperation(operation, input, { json = false } = {}) {
  const print = json ? jsonStdout() : null;
  const response = await dispatch(controlContext(), operation, input);
  if (print) print(response);
  else if (response.ok) console.log(formatResponse(response));
  else console.error(formatResponse(response));
  return responseExitCode(response);
}

const JOB_EXIT = { completed: 0, failed: 1, interrupted: 70, "timed-out": 124, cancelled: 130 };

async function testsCommand(action, args, values) {
  if (action === "run") {
    const input = { scenario: values.scenario ?? "all", repeat: values.repeat ?? 1, build: values.build };
    if (!values.wait) return runOperation("observatory.tests.run", input, values);
    const print = values.json ? jsonStdout() : null;
    const ctx = controlContext();
    let response = await dispatch(ctx, "observatory.tests.run", input);
    if (response.ok) {
      const { jobId } = response.result.job;
      console.error(`Job ${jobId} started; waiting for it to finish.`);
      let last = "";
      do {
        await new Promise((resolve) => setTimeout(resolve, 1000));
        response = await dispatch(ctx, "observatory.tests.status", { jobId });
        const current = response.result?.job?.progress?.current;
        const line = current ? `${current.scenario ?? ""} round ${current.round ?? ""}: ${current.phase ?? ""} ${current.actionsDone ?? "?"}/${current.actionsPlanned ?? "?"} actions` : response.result?.job?.status;
        if (line !== last) console.error(line);
        last = line;
      } while (response.ok && ACTIVE.has(response.result.job.status));
    }
    if (print) print(response);
    else console.log(formatResponse(response));
    return response.ok ? JOB_EXIT[response.result.job.status] ?? 1 : responseExitCode(response);
  }
  if (action === "status") return runOperation("observatory.tests.status", { jobId: args[0] }, values);
  if (action === "cancel") return runOperation("observatory.tests.cancel", { jobId: args[0] }, values);
  if (action === "list" || !action) return runOperation("observatory.tests.list", { limit: values.limit }, values);
  console.error(`Unknown tests action "${action}". Use run, status, cancel or list.`);
  return 2;
}

async function auditCommand(action, args, values) {
  switch (action ?? "list") {
    case "presets":
      return runOperation("observatory.audit.presets", {}, values);
    case "run": {
      const input = { preset: values.preset, repeat: values.repeat ?? undefined };
      if (!values.wait) return runOperation("observatory.audit.run", input, values);
      const print = values.json ? jsonStdout() : null;
      const ctx = controlContext();
      let response = await dispatch(ctx, "observatory.audit.run", input);
      if (response.ok) {
        const { jobId } = response.result.job;
        console.error(`Audit job ${jobId} started; waiting for it to finish.`);
        let last = "";
        do {
          await new Promise((resolve) => setTimeout(resolve, 1000));
          response = await dispatch(ctx, "observatory.audit.status", { jobId });
          const current = response.result?.job?.progress?.current;
          const line = current ? `${current.scenario ?? ""}${current.round ? ` round ${current.round}` : ""}: ${current.phase ?? ""}` : response.result?.job?.status;
          if (line !== last) console.error(line);
          last = line;
        } while (response.ok && ACTIVE.has(response.result.job.status));
      }
      if (print) print(response);
      else console.log(formatResponse(response));
      return response.ok ? JOB_EXIT[response.result.job.status] ?? 1 : responseExitCode(response);
    }
    case "status":
      return runOperation("observatory.audit.status", { jobId: args[0] }, values);
    case "cancel":
      return runOperation("observatory.audit.cancel", { jobId: args[0] }, values);
    case "list":
      return runOperation("observatory.audit.list", { preset: values.preset, limit: values.limit }, values);
    case "findings":
      return runOperation("observatory.audit.findings", { auditId: args[0], severity: values.severity, subsystem: values.subsystem }, values);
    case "compare":
      return runOperation("observatory.audit.compare", { auditId: args[0], baselineAuditId: args[1] }, values);
    case "report": {
      if (values.json) return runOperation("observatory.audit.report", { auditId: args[0] }, values);
      const response = await dispatch(controlContext(), "observatory.audit.report", { auditId: args[0] });
      if (!response.ok) {
        console.error(formatResponse(response));
        return responseExitCode(response);
      }
      process.stdout.write(response.result.markdown);
      return 0;
    }
    case "timeline":
      return runOperation("observatory.audit.timeline", { runId: args[0], fromMs: values.from, toMs: values.to, limit: values.limit }, values);
    case "operation":
      return runOperation("observatory.audit.operation", { runId: args[0], action: args[1] }, values);
    case "export":
      return runOperation("observatory.audit.export", { auditId: args[0], includeRawLogs: values["raw-logs"] }, values);
    default:
      console.error(`Unknown audit action "${action}". Use presets, run, status, cancel, list, findings, compare, report, timeline, operation or export.`);
      return 2;
  }
}

async function liveCommand(action, args, values) {
  const duration = values.duration ?? undefined;
  switch (action ?? "status") {
    case "status":
      return runOperation("observatory.live.status", {}, values);
    case "start":
      return runOperation("observatory.live.start", { durationSeconds: duration, label: values.label }, values);
    case "stop":
      return runOperation("observatory.live.stop", {}, values);
    case "mark":
      return runOperation("observatory.live.mark", { label: args.join(" ") || values.label }, values);
    case "wait":
      return runOperation("observatory.live.wait", { timeoutSeconds: values.timeout }, values);
    case "record": {
      const print = values.json ? jsonStdout() : null;
      const ctx = controlContext();
      let response = await dispatch(ctx, "observatory.live.start", { durationSeconds: duration, label: values.label });
      if (response.ok) {
        const seconds = response.result.durationSeconds ?? 30;
        console.error(`Recording ${response.result.runId} for ${seconds} s. Use Archivist now; it stops by itself.`);
        response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: Math.min(300, seconds + 60) });
      }
      if (print) print(response);
      else if (response.ok) console.log(formatResponse(response));
      else console.error(formatResponse(response));
      return responseExitCode(response);
    }
    case "list":
      return runOperation("observatory.live.list", { limit: values.limit }, values);
    case "get":
      return runOperation("observatory.live.get", { runId: args[0] }, values);
    case "spikes":
      return runOperation("observatory.live.spikes", { runId: args[0], limit: values.limit }, values);
    case "window":
      return runOperation("observatory.live.window", { runId: args[0], fromMs: values.from, toMs: values.to }, values);
    case "compare":
      return runOperation("observatory.live.compare", { baseRunId: args[0], runId: args[1] }, values);
    default:
      console.error(`Unknown live action "${action}". Use status, record, start, mark, stop, wait, list, get, spikes, window or compare.`);
      return 2;
  }
}

async function runsCommand(action, args, values) {
  if (action === "list" || !action) {
    return runOperation("observatory.runs.list", { kind: values.kind, scenario: values.scenario, status: values.status, limit: values.limit }, values);
  }
  if (action === "get") return runOperation("observatory.runs.get", { runId: args[0] }, values);
  if (action === "artifact") return runOperation("observatory.runs.artifact", { runId: args[0], name: args[1], tailLines: values.tail, raw: values.raw }, values);
  if (action === "compare") return runOperation("observatory.runs.compare", { runIds: args }, values);
  console.error(`Unknown runs action "${action}". Use list, get, artifact or compare.`);
  return 2;
}

// `control <operation>`: the input as JSON on stdin, the response as JSON
// on stdout. Archivist's IDE host runs this for the in-app panel.
async function controlCommandFromStdin(operation) {
  let text = "";
  if (!process.stdin.isTTY) {
    for await (const chunk of process.stdin) {
      text += chunk;
      if (text.length > 64 * 1024) break;
    }
  }
  let input = {};
  const print = jsonStdout();
  try {
    input = text.trim() ? JSON.parse(text) : {};
  } catch (error) {
    print({ schema: "archivist.observatory.response", schemaVersion: 1, operation, ok: false, error: { code: "invalid-input", message: `The input is not valid JSON: ${error.message}` } });
    return 2;
  }
  const response = await dispatch(controlContext(), operation, input);
  print(response);
  return responseExitCode(response);
}

// The background half of `tests run`: runs the job's scenarios through the
// ordinary UI Lab runner and keeps job.json up to date. Started only by
// control/jobs.mjs startJob.
async function jobRunCommand(jobId) {
  const root = observatoryRoot();
  const loaded = await readJob(root, jobId);
  if (!loaded?.job) {
    console.error(`No readable job ${jobId}.`);
    return 2;
  }
  const { job } = loaded;
  if (!ACTIVE.has(job.status)) {
    console.error(`Job ${jobId} is already ${job.status}.`);
    return 2;
  }
  let cancelled = false;
  const onSignal = () => {
    cancelled = true;
  };
  const signals = ["SIGINT", "SIGTERM", "SIGHUP"];
  signals.forEach((name) => process.on(name, onSignal));
  const save = () => writeJob(root, job);
  setStatus(job, "running", { pid: process.pid, startedAt: new Date().toISOString() });
  await save();
  let stopped = false;
  try {
    if (job.request.build) {
      job.current = { phase: "building", scenario: null, round: null, runId: null };
      await save();
      const build = await buildCommand({ label: `test-lab ${jobId}`, follow: false });
      job.buildRunId = build.runId;
      if (build.exitCode === 130) cancelled = true;
      else if (build.exitCode !== 0) job.error = "The build failed, so no scenario ran.";
      stopped = build.exitCode !== 0;
    }
    for (let round = 1; round <= job.request.repeat && !stopped; round += 1) {
      for (const scenario of job.request.scenarios) {
        if (cancelled) break;
        job.current = { phase: "starting app", scenario, round, runId: null };
        await save();
        const collected = [];
        const exitCode = await uiCommand({
          scenario, "skip-build": true, label: `test-lab ${jobId}`, jobId,
          onRunCreated: async (runId) => {
            job.current = { phase: "running scenario", scenario, round, runId };
            await save();
          },
        }, collected);
        const runId = collected[0] ?? null;
        let status = null;
        if (runId) {
          try {
            status = (await readSummary(root, runId)).summary.status;
          } catch {
            status = "unreadable";
          }
        }
        job.runs.push({ runId, scenario, round, status, exitCode });
        await save();
        if (exitCode === 130) cancelled = true;
        if (!runId) {
          job.error = "The scenario did not start (no Archivist binary?). See job.log.";
          stopped = true;
          break;
        }
      }
      if (cancelled) break;
    }
  } catch (error) {
    job.error = error.message;
    stopped = true;
  }
  const statuses = job.runs.map((entry) => entry.status);
  const status = cancelled ? "cancelled"
    : statuses.includes("timeout") ? "timed-out"
    : stopped || job.runs.length < job.plan.totalRuns || statuses.some((entry) => entry !== "passed") ? "failed"
    : "completed";
  if (cancelled) job.cancelRequestedAt ??= new Date().toISOString();
  job.current = null;
  if (job.request.audit) {
    // The audit is written whatever happened, from the runs that finished.
    job.current = { phase: "writing findings", scenario: null, round: null, runId: null };
    await save();
    const written = await writeAuditRecord(root, job, { repoRoot: ROOT, status });
    job.auditRunId = written.runId ?? null;
    if (written.error) job.error = [job.error, `The audit record could not be written: ${written.error}`].filter(Boolean).join(" ");
    job.current = null;
  }
  setStatus(job, status, { exitCode: JOB_EXIT[status] });
  await save();
  await releaseLock(root, { jobId });
  signals.forEach((name) => process.off(name, onSignal));
  return JOB_EXIT[status];
}

const DEFAULT_DASHBOARD_PORT = 4317;

async function dashboardCommand(options) {
  const port = options.port === undefined ? DEFAULT_DASHBOARD_PORT : Number(options.port);
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    console.error(`--port must be a number from 0 to 65535, got ${options.port}`);
    return 2;
  }
  const root = observatoryRoot();
  // --control: the page may also run tests and write exports, but only with
  // this session's key, which is in the link printed below.
  const token = options.control ? newControlToken() : null;
  const server = createDashboardServer({ root, port, control: token ? { token, ctx: controlContext() } : null });
  try {
    await new Promise((resolve, reject) => {
      server.once("error", reject);
      // Bound to the loopback interface only: nothing on the network can reach it.
      server.listen(port, "127.0.0.1", resolve);
    });
  } catch (error) {
    if (error.code === "EADDRINUSE") {
      console.error(`Port ${port} is in use. Try: npm run observatory:dashboard -- --port ${port + 1}`);
      return 1;
    }
    throw error;
  }
  const base = `http://127.0.0.1:${server.address().port}/`;
  const url = token ? `${base}#control=${token}&to=lab` : base;
  console.log(token
    ? `Observatory dashboard with controls: ${url}\nOnly a page opened from this link can run tests. Records: ${root}\nPress Ctrl+C to stop.`
    : `Observatory dashboard: ${url}\nReading records (read-only) from ${root}\nAdd --control to run tests from the page.\nPress Ctrl+C to stop.`);
  if (options.open && process.platform === "darwin") {
    spawn("open", [url], { stdio: "ignore", detached: true }).on("error", () => {}).unref();
  }
  await new Promise((resolve) => {
    const stop = () => server.close(() => resolve());
    process.once("SIGINT", stop);
    process.once("SIGTERM", stop);
  });
  return 0;
}

async function main(argv) {
  const { values, positionals } = parseArgs({
    args: argv,
    allowPositionals: true,
    allowNegative: true,
    options: {
      label: { type: "string" },
      preset: { type: "string" },
      severity: { type: "string" },
      subsystem: { type: "string" },
      follow: { type: "boolean", default: false },
      "time-trace": { type: "boolean", default: false },
      "build-run": { type: "string" },
      scenario: { type: "string" },
      "skip-build": { type: "boolean", default: false },
      "keep-profile": { type: "boolean", default: false },
      timeout: { type: "string" },
      app: { type: "string" },
      port: { type: "string" },
      note: { type: "string" },
      json: { type: "boolean", default: false },
      strict: { type: "boolean", default: false },
      collect: { type: "boolean", default: true },
      compare: { type: "boolean", default: true },
      repeat: { type: "string" },
      budgets: { type: "string" },
      open: { type: "boolean", default: true },
      control: { type: "boolean", default: false },
      build: { type: "boolean", default: false },
      wait: { type: "boolean", default: false },
      raw: { type: "boolean", default: false },
      "raw-logs": { type: "boolean", default: false },
      kind: { type: "string" },
      status: { type: "string" },
      limit: { type: "string" },
      tail: { type: "string" },
      duration: { type: "string" },
      from: { type: "string" },
      to: { type: "string" },
      help: { type: "boolean", short: "h", default: false },
    },
  });
  const [command, argument] = positionals;
  if (values.help || !command) {
    console.log(USAGE);
    return command || values.help ? 0 : 2;
  }
  switch (command) {
    case "build": {
      const build = await buildCommand(values);
      if (build.exitCode === 0 && values.compare) await compareAfterRun(observatoryRoot(), [build.runId]);
      return build.exitCode;
    }
    case "ui": {
      const print = values.json ? jsonStdout() : null;
      const collected = [];
      const exitCode = await uiCommand(values, collected);
      if (print) {
        const ctx = controlContext();
        const runs = [];
        for (const runId of collected) runs.push((await dispatch(ctx, "observatory.runs.get", { runId })).result?.run ?? { runId, status: "unreadable" });
        const comparison = collected.length ? await dispatch(ctx, "observatory.runs.compare", { runIds: collected }) : null;
        print({
          schema: "archivist.observatory.response", schemaVersion: 1, operation: "observatory.ui", ok: exitCode === 0,
          result: { exitCode, runs, comparison: comparison?.ok ? comparison.result.report : null },
          ...(exitCode === 0 ? {} : { error: { code: exitCode === 2 ? "invalid-input" : exitCode === 4 ? "busy" : "test-failed", message: `The UI test exited with code ${exitCode}; see the runs' status and error.` } }),
        });
      } else if (collected.length && values.compare) {
        await compareAfterRun(observatoryRoot(), collected);
      }
      return exitCode;
    }
    case "baseline":
      return baselineCommand(observatoryRoot(), positionals.slice(1), values);
    case "check":
      return checkCommand(values);
    case "ingest-runtime":
      return ingestRuntimeCommand(argument, values);
    case "show":
      if (values.json) return runOperation("observatory.runs.get", { runId: argument ?? (await listRunIds(observatoryRoot())).at(-1) }, values);
      return showCommand(argument);
    case "capabilities":
      return runOperation("observatory.capabilities", {}, values);
    case "scenarios":
      return runOperation("observatory.scenarios.list", {}, values);
    case "tests":
      return testsCommand(argument, positionals.slice(2), values);
    case "runs":
      return runsCommand(argument, positionals.slice(2), values);
    case "live":
      return liveCommand(argument, positionals.slice(2), values);
    case "audit":
      return auditCommand(argument, positionals.slice(2), values);
    case "report": {
      if (values.json) return runOperation("observatory.reports.generate", { runId: argument }, values);
      const response = await dispatch(controlContext(), "observatory.reports.generate", { runId: argument });
      if (!response.ok) {
        console.error(formatResponse(response));
        return responseExitCode(response);
      }
      process.stdout.write(response.result.markdown);
      return 0;
    }
    case "export":
      return runOperation("observatory.reports.export", { runId: argument, includeRawLogs: values["raw-logs"] }, values);
    case "control":
      return controlCommandFromStdin(argument);
    case "job-run":
      return jobRunCommand(argument);
    case "dashboard":
      return dashboardCommand(values);
    default:
      console.error(`Unknown command: ${command}\n\n${USAGE}`);
      return 2;
  }
}

process.exitCode = await main(process.argv.slice(2));
