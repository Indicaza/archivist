import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readdir, readFile, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { classifyRun, labEnvironment, removeProfile, scenarioForRun } from "../ui-lab.mjs";
import { describe, percentile, summarizeActions } from "../ui-stats.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CLI = path.join(HERE, "..", "cli.mjs");
const FAKE_APP = path.join(HERE, "fixtures", "fake-app.mjs");

function runUi(mode, root, extra = []) {
  return new Promise((resolve) => {
    execFile(process.execPath, [CLI, "ui", "--scenario", "sidebar", "--skip-build", "--app", FAKE_APP, ...extra], {
      env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root, FAKE_APP_MODE: mode, ARCHIVIST_LEGACY_WORKSPACE: "1" },
    }, (error, stdout, stderr) => resolve({ code: error ? error.code : 0, stdout, stderr }));
  });
}

async function onlyRun(root) {
  const [runId] = await readdir(path.join(root, "runs"));
  const dir = path.join(root, "runs", runId);
  return { dir, summary: JSON.parse(await readFile(path.join(dir, "summary.json"), "utf8")) };
}

test("a passing scenario is recorded with stats, timings, the scenario and the log", async () => {
  const root = await tempDir();
  const result = await runUi("pass", root);
  assert.equal(result.code, 0, result.stderr);
  assert.match(result.stdout, /UI TORTURE LAB/);
  assert.match(result.stdout, /Result:\s+PASS/);
  const { dir, summary } = await onlyRun(root);
  assert.equal(summary.schemaVersion, 1);
  assert.equal(summary.kind, "ui-scenario");
  assert.equal(summary.status, "passed");
  assert.equal(summary.scenario.id, "sidebar");
  assert.equal(summary.actions.total, 4);
  assert.equal(summary.stats.measured.actions, 2);
  assert.equal(summary.stats.measured.settledMs.medianMs, 30);
  assert.equal(summary.stats.measured.settledMs.maxMs, 50);
  assert.equal(summary.runtimeTiming.recognizedEvents, 1);
  assert.equal(summary.profile.removed, true);
  assert.ok(summary.build.sha256);
  const scenario = JSON.parse(await readFile(path.join(dir, "scenario.json"), "utf8"));
  assert.equal(scenario.name, "Sidebar Stress");
  const log = await readFile(path.join(dir, "runtime.log"), "utf8");
  // The app ran in its own profile, without the caller's Archivist switches.
  assert.match(log, /env-check db=\S+archivist-ui-lab-\S+archivist\.db legacy=unset/);
});

test("a functional failure fails the run but keeps the report", async () => {
  const root = await tempDir();
  const result = await runUi("fail", root);
  assert.equal(result.code, 1);
  const { summary } = await onlyRun(root);
  assert.equal(summary.status, "failed");
  assert.equal(summary.actions.failed, 1);
  assert.match(summary.actions.firstFailure.error, /timed out/);
  assert.match(result.stdout, /Result:\s+FAILED/);
});

test("a crash, a refusal and a hang are each detected", async () => {
  const crash = await tempDir();
  assert.equal((await runUi("crash", crash)).code, 70);
  assert.equal((await onlyRun(crash)).summary.status, "crashed");

  const refuse = await tempDir();
  assert.equal((await runUi("refuse", refuse)).code, 3);
  assert.equal((await onlyRun(refuse)).summary.status, "refused");

  const hang = await tempDir();
  const started = Date.now();
  assert.equal((await runUi("hang", hang, ["--timeout", "1"])).code, 124);
  assert.ok(Date.now() - started < 15000);
  const { summary } = await onlyRun(hang);
  assert.equal(summary.status, "timeout");
  assert.equal(summary.app.signal, "SIGTERM");
});

test("an unknown scenario is a usage error", async () => {
  const root = await tempDir();
  const result = await new Promise((resolve) => {
    execFile(process.execPath, [CLI, "ui", "--scenario", "nope", "--skip-build"], { env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root } },
      (error, stdout, stderr) => resolve({ code: error ? error.code : 0, stderr }));
  });
  assert.equal(result.code, 2);
  assert.match(result.stderr, /Available: ai-pipeline, ai-provider, ai-turn, chat, sidebar, startup, workspace/);
});

test("smoke scenario is shorter and never mutates the canonical workload", () => {
  const original = { id: "workspace", name: "Workspace", version: 2, initialConditions: "Fresh profile.", warmupIterations: 1, iterations: 5, settleQuietMs: 120 };
  const fast = scenarioForRun(original, { fast: true, background: true });
  assert.equal(original.iterations, 5);
  assert.equal(original.version, 2);
  assert.equal(fast.iterations, 2);
  assert.equal(fast.warmupIterations, 0);
  assert.equal(fast.settleQuietMs, 70);
  assert.equal(fast.version, 11002);
  assert.match(fast.name, /smoke/);
  assert.match(fast.initialConditions, /frame presentation may differ/);
  const full = scenarioForRun(original);
  assert.deepEqual(full, original);
  assert.notEqual(fast.version, scenarioForRun(original, { fast: true }).version);
  assert.notEqual(fast.version, scenarioForRun(original, { background: true }).version);
});

test("the lab environment isolates the profile and drops Archivist switches", () => {
  const env = labEnvironment({ PATH: "/bin", ARCHIVIST_DB_PATH: "/real.db", ARCHIVIST_LEGACY_WORKSPACE: "1", QT_LOGGING_RULES: "x" },
    { profileDir: "/tmp/archivist-ui-lab-1", scenarioFile: "/r/scenario.json", eventsFile: "/r/ui-events.json" });
  assert.equal(env.PATH, "/bin");
  assert.equal(env.ARCHIVIST_DB_PATH, "/tmp/archivist-ui-lab-1/archivist.db");
  assert.equal(env.ARCHIVIST_LEGACY_WORKSPACE, undefined);
  assert.equal(env.QT_LOGGING_RULES, "archivist.wells.timing=true");
});

test("only directories the runner made are removed", async () => {
  const other = await tempDir();
  await assert.rejects(removeProfile(other), /not a UI Lab profile/);
  assert.ok((await stat(other)).isDirectory());
});

test("runs are classified from the exit and the driver's report", () => {
  const app = { exitCode: 0, signal: null, timedOut: false, interruptedBy: null, spawnError: null };
  assert.equal(classifyRun(app, { status: "passed" }).status, "passed");
  assert.equal(classifyRun({ ...app, exitCode: 1 }, { status: "failed" }).status, "failed");
  assert.equal(classifyRun({ ...app, exitCode: null, signal: "SIGSEGV" }, { status: "running" }).status, "crashed");
  assert.equal(classifyRun({ ...app, exitCode: 0 }, { status: "running" }).status, "crashed");
  assert.equal(classifyRun(app, null).status, "error");
  assert.equal(classifyRun({ ...app, interruptedBy: "SIGINT" }, null).status, "interrupted");
});

test("statistics use measured actions only and leave p95 out for small samples", () => {
  assert.equal(percentile([1, 2, 3, 4], 50), 2);
  assert.equal(percentile([], 50), null);
  assert.deepEqual(describe([5, 1, null, 3]), { n: 3, medianMs: 3, p95Ms: null, maxMs: 5 });
  const twenty = describe(Array.from({ length: 20 }, (_, index) => index + 1));
  assert.equal(twenty.p95Ms, 19);
  const actions = [
    { phase: "warmup", step: 0, op: "click", ok: true, settledMs: 500 },
    { phase: "measured", step: 0, op: "click", label: "open", ok: true, settledMs: 10, frames: { maxIntervalMs: 16 } },
    { phase: "measured", step: 0, op: "click", label: "open", ok: false, settledMs: null },
    { phase: "measured", step: 1, op: "click", label: "close", ok: true, settledMs: 20 },
  ];
  const stats = summarizeActions(actions);
  assert.equal(stats.measured.actions, 3);
  assert.equal(stats.measured.failed, 1);
  assert.equal(stats.measured.settledMs.maxMs, 20);
  assert.equal(stats.phases.warmup.actions, 1);
  assert.equal(stats.steps.length, 2);
  assert.equal(stats.steps[0].label, "open");
});

test("scenario definitions are complete and use known operations", async () => {
  const dir = path.join(HERE, "..", "ui-scenarios");
  const ops = new Set(["click", "dragSidebarEdge", "returnToWorkspace", "setPanelOpen", "openView", "applyLayout", "activateView", "minimizeView", "restoreView", "noop", "seedChats", "refreshChats", "selectChat", "sendChatMessage", "cancelChatRun"]);
  const expectations = new Set(["panelOpen", "panelId", "homeActive", "sidebarSettled", "explorerWidth", "currentLayoutId", "focusedView", "minimized", "viewsOpen", "genieIdle", "seeded", "chatsListed", "chatShown", "runSettled", "runStreaming"]);
  for (const file of await readdir(dir)) {
    const scenario = JSON.parse(await readFile(path.join(dir, file), "utf8"));
    assert.equal(`${scenario.id}.json`, file);
    assert.ok(Number.isInteger(scenario.version) && scenario.version > 0);
    assert.ok(scenario.name && scenario.initialConditions && scenario.iterations > 0);
    for (const step of [...scenario.setup, ...scenario.steps, ...scenario.teardown]) {
      assert.ok(ops.has(step.op), `${file}: unknown op ${step.op}`);
      assert.ok(step.label, `${file}: every step has a label`);
      for (const name of Object.keys(step.expect ?? {})) {
        assert.ok(expectations.has(name), `${file}: unknown expectation ${name}`);
      }
    }
  }
});

// The UI acceptance test: an app run with a deliberately slow operation in
// step 1. The slow operation, its parent, the stall during it and startup
// all come back on one timeline, attributed to the right action.
test("operations inside each action are attributed, nested and not double-counted", async () => {
  const root = await tempDir();
  const result = await runUi("slow", root);
  assert.equal(result.code, 0, result.stderr);
  const { dir, summary } = await onlyRun(root);
  assert.equal(summary.operations.collected, true);
  assert.equal(summary.operations.slowest[0].name, "chat.open-conversation");
  assert.ok(summary.operations.slowest.some((op) => op.name === "chat.publish-messages" && op.durationMs === 210 && op.action === 3));
  const measuredStep = summary.operations.steps.find((entry) => entry.step === 1);
  assert.equal(measuredStep.runs, 1, "measured iterations only");
  assert.equal(measuredStep.operations[0].name, "chat.open-conversation");
  assert.equal(measuredStep.stalls, 1);
  assert.equal(summary.operations.startup.stages[0].name, "qt-init");
  assert.equal(summary.operations.startup.firstFrameMs, 1380);
  assert.equal(summary.operations.startup.readyMs, 1500);
  assert.equal(summary.artifacts.operations, "ui-operations.json");

  const artifact = JSON.parse(await readFile(path.join(dir, "ui-operations.json"), "utf8"));
  const action = artifact.actions.find((entry) => entry.index === 3);
  const [open] = action.operations;
  assert.equal(open.name, "chat.open-conversation");
  assert.deepEqual(open.children.map((child) => child.name), ["chat.load-message-page", "chat.publish-messages"]);
  assert.equal(open.selfMs, 20, "260 ms minus the 240 ms of its two children");
  assert.equal(action.window.basis, "settled");
  assert.equal(action.window.ms, 280, "the response ends at the last frame (280 ms), not after the 20 ms quiet wait");
  assert.equal(action.timings.elapsedMs, 300);
  assert.equal(action.timings.quietWaitMs, 20);
  assert.equal(action.coveredMs, 260);
  assert.equal(action.unattributedMs, 20, "the 280 ms window minus the union of its spans, not the sum");
  assert.equal(action.coveredMs + action.unattributedMs, action.window.ms);
  assert.equal(action.afterWindowMs, 0);
  const step = summary.operations.steps.find((entry) => entry.step === 1);
  assert.equal(step.windowMedianMs, 280);
  assert.ok(step.unattributedMedianMs <= step.windowMedianMs);
  assert.equal(summary.operations.schemaVersion, 2);
  assert.deepEqual(action.stalls, [{ atMs: 45, lateMs: 190 }]);
});

test("runs from an app that doesn't report operations say so", async () => {
  const root = await tempDir();
  await runUi("pass", root);
  const { summary } = await onlyRun(root);
  assert.equal(summary.operations.collected, false);
  assert.equal(summary.artifacts.operations, undefined);
});
