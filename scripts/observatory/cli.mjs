#!/usr/bin/env node
// Archivist Observatory: local build and runtime measurement records.
//   node scripts/observatory/cli.mjs build [--label <text>] [--follow]
//   node scripts/observatory/cli.mjs ingest-runtime <log> [--label <text>] [--build-run <runId>]
//   node scripts/observatory/cli.mjs show [runId]
import { copyFile, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { archivistBuildPhases, buildConditionsBefore, classifyBuild } from "./build-phases.mjs";
import { exitCodeFor, runCapture } from "./capture.mjs";
import { collectMachine, collectSource, collectToolchain } from "./metadata.mjs";
import { phaseLine, formatBuildSummary } from "./report.mjs";
import { parseRuntimeLog, summarizeEvents } from "./runtime-log.mjs";
import {
  SCHEMA_VERSION, closeLog, createRunDir, listRunIds, observatoryRoot, openLog, readSummary, runsDir, writeJsonAtomic,
} from "./storage.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const BUILD_DIR = path.join(ROOT, "build", "frontend-debug");

const USAGE = `Archivist Observatory

  npm run observatory:build [-- --label <text>] [--follow]
  npm run observatory -- ingest-runtime <log> [--label <text>] [--build-run <runId>]
  npm run observatory -- show [runId]

Records are stored in $ARCHIVIST_OBSERVATORY_DIR, or by default
${observatoryRoot()}`;

async function buildCommand(options) {
  const root = observatoryRoot();
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
    buildConditions: { buildDir: path.relative(ROOT, BUILD_DIR), before, classification: null },
    phases: [],
    errors: [],
    artifacts: { log: "build.log" },
  };
  await writeJsonAtomic(summaryPath, summary);

  const log = openLog(logPath);
  log.write(`Archivist Observatory build capture\nRun: ${runId}\nStarted: ${createdAt}\n`);
  console.log(`Observatory run ${runId}${options.follow ? "" : " (full output in build.log)"}`);

  let state;
  try {
    state = await runCapture(archivistBuildPhases({ root: ROOT, buildDir: BUILD_DIR }), {
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
  for (const phase of state.phases) {
    if (phase.error) {
      summary.errors.push({ phase: phase.name, message: phase.error.message });
    }
  }
  await writeJsonAtomic(summaryPath, summary);
  log.write(`\nFinished: ${summary.finishedAt}\nStatus: ${summary.status}\n`);
  await closeLog(log);

  console.log(formatBuildSummary(summary, { summaryPath, logPath }));
  return summary.exitCode;
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
  if (summary.kind === "build") {
    console.log(formatBuildSummary(summary, { summaryPath: file, logPath: path.join(path.dirname(file), summary.artifacts.log) }));
  } else {
    printRuntimeSummary(summary, file);
  }
  return 0;
}

async function main(argv) {
  const { values, positionals } = parseArgs({
    args: argv,
    allowPositionals: true,
    options: {
      label: { type: "string" },
      follow: { type: "boolean", default: false },
      "build-run": { type: "string" },
      help: { type: "boolean", short: "h", default: false },
    },
  });
  const [command, argument] = positionals;
  if (values.help || !command) {
    console.log(USAGE);
    return command || values.help ? 0 : 2;
  }
  switch (command) {
    case "build":
      return buildCommand(values);
    case "ingest-runtime":
      return ingestRuntimeCommand(argument, values);
    case "show":
      return showCommand(argument);
    default:
      console.error(`Unknown command: ${command}\n\n${USAGE}`);
      return 2;
  }
}

process.exitCode = await main(process.argv.slice(2));
