#!/usr/bin/env node
// TEST FIXTURES ONLY. These are made-up Observatory runs for the dashboard's
// tests and for previewing the page. They are not measurements of Archivist.
// Every run is labelled "fixture: ..." and lives on the fixture/* branch, so
// it can't be mistaken for a real record.
//
// Write them to a scratch folder (never your real Observatory folder):
//   node scripts/observatory/test/fixtures/dashboard-runs.mjs <empty-dir>
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const MAC = {
  platform: "darwin", osVersion: "25.0.0", architecture: "arm64", cpuModel: "Fixture M-series",
  logicalCpus: 10, totalMemoryBytes: 34359738368, nodeVersion: "v24.0.0",
};
const OTHER_MAC = { ...MAC, cpuModel: "Fixture Intel", architecture: "x64", logicalCpus: 8 };
const TOOLCHAIN = {
  buildType: "Debug", generator: "Ninja", cmakeVersion: "3.30.0", ninjaVersion: "1.12.1",
  compiler: { id: "AppleClang", version: "17.0.0", path: "/usr/bin/c++" }, qtVersion: "6.8.0",
  compilerCache: { launcher: null, ccacheInstalled: false, sccacheInstalled: false },
};
const COMMIT_A = "a".repeat(40);
const COMMIT_B = "b".repeat(40);

function source(commit, branch = "fixture/main", dirty = false) {
  return { commit, branch, dirty, changedFiles: dirty ? 2 : 0 };
}

function phase(name, startOffsetMs, durationMs, extra = {}) {
  return { name, status: "success", exitCode: 0, signal: null, startOffsetMs, durationMs, details: {}, error: null, ...extra };
}

export function fixtureBuild(runId, { commit, total, phases, status = "success", classification = "incremental", cacheReused = true, environment = MAC, workSteps = 12 }) {
  const [ide, configure, plan, native] = phases;
  const list = [
    phase("ide-bundle", 0, ide, { details: { cacheReused, cacheReusedBasis: "inferred-from-output" } }),
    phase("configure", ide, configure),
    phase("build-plan", ide + configure, plan, { details: { plannedWorkSteps: workSteps } }),
    phase("native-build", ide + configure + plan, native, {
      details: {
        ninjaLog: {
          basis: "measured-by-ninja", stepsRecorded: workSteps + 3, bookkeepingSteps: 3, workSteps,
          sumOfStepDurationsMs: native * 3,
          slowestSteps: [
            { output: "src/App/CMakeFiles/archivist_app.dir/main.cpp.o", outputCount: 1, durationMs: Math.round(native * 0.4) },
            { output: "src/Core/CMakeFiles/archivist_core.dir/app_profile.cpp.o", outputCount: 1, durationMs: Math.round(native * 0.2) },
          ],
        },
      },
    }),
  ];
  const errors = [];
  if (status === "failure") {
    list[3] = {
      ...list[3], status: "failure", exitCode: 1,
      error: { message: "native-build exited with code 1", excerpt: ["error: expected ';' after expression", "<script>alert('log text is shown as text')</script>"] },
    };
    errors.push({ phase: "native-build", message: "native-build exited with code 1" });
  }
  return {
    schemaVersion: 1, kind: "build", runId, label: `fixture: ${status === "failure" ? "failed build" : `${classification} build`}`,
    createdAt: isoFromId(runId), finishedAt: isoFromId(runId), status, exitCode: status === "success" ? 0 : 1, totalDurationMs: total,
    clock: "monotonic", source: source(commit), environment, toolchain: TOOLCHAIN,
    buildConditions: { buildDir: "build/frontend-debug", before: {}, classification: { value: classification, basis: "fixture" } },
    phases: list, errors, artifacts: { log: "build.log" },
  };
}

function timing(n, median, p95, max) {
  return { n, medianMs: median, p95Ms: p95, maxMs: max };
}

export function fixtureUiRun(runId, { scenario = "sidebar", version = 1, status = "passed", settled, failed = 0, commit = COMMIT_A, environment = MAC, error = null }) {
  const actions = 12;
  const steps = [
    { step: 0, op: "click", label: "open library", method: "input", actions: 4, passed: 4, failed: 0, settledMs: timing(4, settled, null, settled * 1.4), stateMs: timing(4, 2, null, 3) },
    { step: 1, op: "drag", label: "resize explorer", method: "input", actions: 4, passed: 4 - failed, failed, settledMs: timing(4, settled * 2, null, settled * 3), stateMs: timing(4, 4, null, 6) },
  ];
  return {
    schemaVersion: 1, kind: "ui-scenario", runId, label: `fixture: ${scenario} ${status}`,
    createdAt: isoFromId(runId), finishedAt: isoFromId(runId), status, exitCode: status === "passed" ? 0 : 1, error,
    clock: "monotonic", source: source(commit), environment,
    build: { binary: "build/frontend-debug/Archivist.app", sizeBytes: 1000, modifiedAt: isoFromId(runId), sha256: commit.slice(0, 8).padEnd(64, "0"), toolchain: TOOLCHAIN, links: [] },
    scenario: { id: scenario, version, name: scenario === "sidebar" ? "Sidebar Stress" : "Workspace Stress", seed: null, warmupIterations: 1, iterations: 3, definition: "scenario.json" },
    app: { exitCode: status === "passed" ? 0 : 1, signal: null, durationMs: 4200, readyMs: 900, driverVersion: 1, driverStatus: status, isolation: { ok: true, checks: [] } },
    actions: { planned: actions, total: actions, passed: actions - failed, failed, warmup: 2, firstFailure: failed ? { index: 7, label: "resize explorer", error: "timed out waiting for state" } : null },
    stats: {
      phases: {},
      measured: {
        actions: 8, passed: 8 - failed, failed, timedOut: failed, settleTimedOut: 0,
        dispatchMs: timing(8, 0.5, 0.9, 1.1), stateMs: timing(8, 3, 6, 7),
        settledMs: timing(8, settled, settled * 2.2, settled * 3), frameMaxIntervalMs: timing(8, 16.7, 33.4, 50),
      },
      steps,
    },
    runtimeTiming: { category: "archivist.wells.timing", relationship: "same-process", recognizedEvents: 2, unrecognizedLines: [], summary: {}, events: [] },
    profile: { location: "temporary directory", removed: true },
    artifacts: { events: "ui-events.json", log: "runtime.log", scenario: "scenario.json" },
  };
}

function isoFromId(runId) {
  const m = runId.match(/^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})-(\d{3})Z/);
  return `${m[1]}-${m[2]}-${m[3]}T${m[4]}:${m[5]}:${m[6]}.${m[7]}Z`;
}

// runId -> { summary (object, or a raw string for a broken file), files }
export function dashboardFixtures() {
  const runs = {};
  const add = (summary, files = {}) => { runs[summary.runId] = { summary, files }; };
  const buildLog = (lines) => ({ "build.log": lines.join("\n") + "\n" });

  add(fixtureBuild("20261020T090000-000Z-build-00000a", { commit: COMMIT_A, total: 64000, phases: [9000, 3000, 2000, 50000], classification: "clean", cacheReused: false, workSteps: 120 }),
    buildLog(["fixture build log", ...Array.from({ length: 30 }, (_, i) => `[${i + 1}/120] Building CXX object fixture_${i}.o`)]));
  add(fixtureBuild("20261021T090000-000Z-build-00000b", { commit: COMMIT_A, total: 12000, phases: [1000, 1500, 500, 9000] }),
    buildLog(["fixture build log", "[1/12] Building CXX object main.cpp.o"]));
  add(fixtureBuild("20261022T090000-000Z-build-00000c", { commit: COMMIT_B, total: 10000, phases: [1000, 1500, 500, 7000] }),
    buildLog(["fixture build log", "[1/12] Building CXX object main.cpp.o"]));
  add(fixtureBuild("20261023T090000-000Z-build-00000d", { commit: COMMIT_B, total: 8000, phases: [1000, 1500, 500, 5000], status: "failure" }),
    buildLog(["fixture build log", "error: expected ';' after expression", "<script>alert('log text is shown as text')</script>"]));
  add(fixtureBuild("20261024T090000-000Z-build-00000e", { commit: COMMIT_B, total: 15000, phases: [1000, 1500, 500, 12000], environment: OTHER_MAC }),
    buildLog(["fixture build log on another machine"]));

  const uiFiles = (status) => ({
    "ui-events.json": JSON.stringify({ schema: "archivist.ui-lab.events", schemaVersion: 1, status, actions: [] }),
    "runtime.log": "fixture runtime log\narchivist.wells.timing: fixture\n",
    "scenario.json": JSON.stringify({ id: "fixture" }),
  });
  add(fixtureUiRun("20261025T090000-000Z-ui-sidebar-00000f", { settled: 40, commit: COMMIT_A }), uiFiles("passed"));
  add(fixtureUiRun("20261026T090000-000Z-ui-sidebar-000010", { settled: 30, commit: COMMIT_B }), uiFiles("passed"));
  add(fixtureUiRun("20261026T100000-000Z-ui-sidebar-000011", { settled: 30, version: 2, commit: COMMIT_B }), uiFiles("passed"));
  add(fixtureUiRun("20261027T090000-000Z-ui-workspace-000012", {
    scenario: "workspace", status: "failed", settled: 60, failed: 1, commit: COMMIT_B, error: "1 action failed",
  }), uiFiles("failed"));

  // A run that never finished, one that can't be read, one from a future
  // schema, and an imported runtime log.
  const running = fixtureBuild("20261028T090000-000Z-build-000013", { commit: COMMIT_B, total: null, phases: [1000, 0, 0, 0] });
  add({ ...running, status: "running", label: "fixture: interrupted build", finishedAt: null, totalDurationMs: null, phases: running.phases.slice(0, 1) });
  add({ runId: "20261028T100000-000Z-ui-sidebar-000014", raw: "{ this is not json" });
  add({ schemaVersion: 99, kind: "build", runId: "20261029T090000-000Z-build-000015", label: "fixture: future schema", status: "success" });
  add({
    schemaVersion: 1, kind: "runtime-log", runId: "20261029T100000-000Z-runtime-000016", label: "fixture: runtime log",
    createdAt: "2026-10-29T10:00:00.000Z", finishedAt: "2026-10-29T10:00:00.000Z", status: "success",
    source: source(COMMIT_B), environment: MAC,
    timing: { recognizedEvents: 3, summary: { minimize: { field: "publishMs", count: 3, meanMs: 2.5, maxMs: 4 } } },
    artifacts: { log: "runtime.log" },
  }, { "runtime.log": "fixture runtime log\n" });
  return runs;
}

export async function writeDashboardFixtures(root) {
  return writeFixtureRuns(root, dashboardFixtures());
}

// runId -> { summary, files } written under <root>/runs.
export async function writeFixtureRuns(root, runs) {
  for (const [runId, { summary, files }] of Object.entries(runs)) {
    const dir = path.join(root, "runs", runId);
    await mkdir(dir, { recursive: true });
    await writeFile(path.join(dir, "summary.json"), summary.raw ?? JSON.stringify(summary, null, 2));
    for (const [name, text] of Object.entries(files)) {
      await writeFile(path.join(dir, name), text);
    }
  }
  return Object.keys(runs);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const root = process.argv[2];
  if (!root) {
    console.error("Usage: node scripts/observatory/test/fixtures/dashboard-runs.mjs <empty-dir>");
    process.exit(2);
  }
  const ids = await writeDashboardFixtures(path.resolve(root));
  console.log(`Wrote ${ids.length} fixture runs to ${path.resolve(root)}/runs`);
}
