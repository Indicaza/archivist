// The Observatory control bridge: operations, jobs, reports, exports and the
// HTTP gate. Tests run the real CLI and the stand-in app (fixtures/fake-app.mjs);
// nothing here needs Qt.
import assert from "node:assert/strict";
import { execFile, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdir, readdir, readFile, writeFile } from "node:fs/promises";
import { request } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { inflateRawSync } from "node:zlib";
import { after, before, describe, test } from "node:test";
import { checkControlRequest } from "../control/gate.mjs";
import { acquireLock, isAlive, lockFile, refreshJob } from "../control/jobs.mjs";
import { dispatch, ERROR_CODES, OPERATIONS, validate } from "../control/operations.mjs";
import { crc32 } from "../control/zip.mjs";
import { createDashboardServer } from "../dashboard/server.mjs";
import { diagnosticReport } from "../dashboard/public/diagnostic.mjs";
import { makeRedactor } from "../dashboard/public/redact.mjs";
import { BUILD_DIR, CLI_PATH, REPO_ROOT, SCENARIO_DIR } from "../paths.mjs";
import { fixtureBuild, fixtureUiRun, writeFixtureRuns } from "./fixtures/dashboard-runs.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE_APP = path.join(HERE, "fixtures", "fake-app.mjs");

function cli(root, args, { mode = "pass", input = null } = {}) {
  return new Promise((resolve) => {
    const child = execFile(process.execPath, [CLI_PATH, ...args], {
      env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root, ARCHIVIST_OBSERVATORY_APP: FAKE_APP, FAKE_APP_MODE: mode },
      timeout: 60000,
    }, (error, stdout, stderr) => resolve({ code: error ? error.code : 0, stdout, stderr }));
    if (input !== null) child.stdin.end(input);
  });
}

function json(result) {
  try {
    return JSON.parse(result.stdout);
  } catch (error) {
    throw new Error(`stdout is not JSON (${error.message}):\n${result.stdout}\nstderr:\n${result.stderr}`);
  }
}

function ctxFor(root, extra = {}) {
  return { root, repoRoot: REPO_ROOT, buildDir: BUILD_DIR, scenarioDir: SCENARIO_DIR, cliPath: CLI_PATH, appBinary: FAKE_APP, env: { ...process.env, FAKE_APP_MODE: "pass", ARCHIVIST_OBSERVATORY_APP: FAKE_APP }, ...extra };
}

async function waitForJob(root, jobId, timeoutMs = 30000) {
  const end = Date.now() + timeoutMs;
  for (;;) {
    const response = await dispatch(ctxFor(root), "observatory.tests.status", { jobId });
    if (!["queued", "starting", "running"].includes(response.result?.job?.status)) return response;
    if (Date.now() > end) throw new Error(`job ${jobId} still ${response.result.job.status}`);
    await new Promise((resolve) => setTimeout(resolve, 150));
  }
}

async function hashRuns(root) {
  const hashes = {};
  for (const id of await readdir(path.join(root, "runs"))) {
    hashes[id] = createHash("sha256").update(await readFile(path.join(root, "runs", id, "summary.json"))).digest("hex");
  }
  return hashes;
}

describe("capabilities and validation", () => {
  test("capabilities describe every operation, its input and side effects, as JSON only", async () => {
    const root = await tempDir();
    const result = await cli(root, ["capabilities", "--json"]);
    assert.equal(result.code, 0, result.stderr);
    const body = json(result);
    assert.equal(body.schema, "archivist.observatory.response");
    assert.equal(body.schemaVersion, 1);
    assert.equal(body.operation, "observatory.capabilities");
    assert.equal(body.ok, true);
    const names = body.result.operations.map((op) => op.name);
    assert.deepEqual(names, Object.keys(OPERATIONS));
    for (const op of body.result.operations) {
      assert.ok(op.description && op.cli && op.input && op.sideEffects, op.name);
    }
    assert.deepEqual(Object.keys(body.result.errorCodes), Object.keys(ERROR_CODES));
    assert.equal(body.result.limits.concurrentUiTests, 1);
    assert.equal(body.result.target.exists, true);
    assert.deepEqual(body.result.scenarios.map((entry) => entry.id), ["sidebar", "workspace"]);
  });

  test("scenarios are listed with what they do and how many actions they plan", async () => {
    const response = await dispatch(ctxFor(await tempDir()), "observatory.scenarios.list", {});
    assert.ok(response.ok);
    const sidebar = response.result.scenarios.find((entry) => entry.id === "sidebar");
    assert.equal(sidebar.name, "Sidebar Stress");
    assert.ok(sidebar.plannedActions > 0);
    assert.ok(sidebar.description && sidebar.initialConditions);
  });

  test("inputs are validated before anything runs; unknown names and fields are refused", async () => {
    const root = await tempDir();
    const ctx = ctxFor(root);
    for (const [input, fragment] of [
      [{ scenario: "../../etc/passwd" }, "scenario"],
      [{ scenario: "sidebar; rm -rf /" }, "scenario"],
      [{ repeat: 0 }, "repeat"],
      [{ repeat: 11 }, "repeat"],
      [{ build: "yes" }, "build"],
      [{ app: "/bin/sh" }, "unknown parameter \"app\""],
      [{ command: "ls" }, "unknown parameter \"command\""],
    ]) {
      const response = await dispatch(ctx, "observatory.tests.run", input);
      assert.equal(response.ok, false, JSON.stringify(input));
      assert.equal(response.error.code, "invalid-input");
      assert.match(response.error.message, new RegExp(fragment.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    }
    assert.equal((await dispatch(ctx, "observatory.tests.run", { scenario: "nonexistent" })).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.shell.exec", {})).error.code, "unknown-operation");
    assert.equal((await dispatch(ctx, "constructor", {})).error.code, "unknown-operation");
    assert.equal((await dispatch(ctx, "observatory.runs.get", { runId: "../../secrets" })).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.runs.get", "not an object")).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.tests.cancel", {})).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.tests.cancel", { jobId: "123" })).error.code, "invalid-input");
    assert.deepEqual(validate({ n: { type: "integer", min: 1, max: 3, default: 2 } }, {}), { n: 2 });
    // The CLI turns invalid input into exit code 2 and a JSON error.
    const result = await cli(root, ["tests", "run", "--repeat", "50", "--json"]);
    assert.equal(result.code, 2);
    assert.equal(json(result).error.code, "invalid-input");
  });

  test("control reads its input as JSON on stdin and always answers JSON", async () => {
    const root = await tempDir();
    let result = await cli(root, ["control", "observatory.scenarios.list"], { input: "{}" });
    assert.equal(result.code, 0);
    assert.ok(json(result).ok);
    result = await cli(root, ["control", "observatory.tests.run"], { input: "{ nope" });
    assert.equal(result.code, 2);
    assert.equal(json(result).error.code, "invalid-input");
    result = await cli(root, ["control", "observatory.nothing"], { input: "{}" });
    assert.equal(result.code, 2);
    assert.equal(json(result).error.code, "unknown-operation");
  });
});

describe("test jobs", () => {
  test("a job runs each scenario and round, records the runs and frees the lock", async () => {
    const root = await tempDir();
    const result = await cli(root, ["tests", "run", "--scenario", "sidebar", "--repeat", "2", "--wait", "--json"]);
    assert.equal(result.code, 0, result.stderr);
    const { job } = json(result).result;
    assert.equal(job.status, "completed");
    assert.deepEqual(job.history.map((entry) => entry.status).filter((status, index, list) => list.indexOf(status) === index), ["queued", "starting", "running", "completed"].filter((status) => job.history.some((entry) => entry.status === status)));
    assert.equal(job.runs.length, 2);
    assert.equal(job.results.length, 2);
    assert.ok(job.results.every((run) => run.status === "passed" && run.scenario === "sidebar"));
    assert.equal(job.progress.runsDone, 2);
    assert.equal(job.target.binary.endsWith("fake-app.mjs"), true);
    assert.ok(job.target.source.commit);
    await assert.rejects(readFile(lockFile(root)));
    // Status and list can be read later without repeating the test.
    const status = await dispatch(ctxFor(root), "observatory.tests.status", {});
    assert.equal(status.result.job.jobId, job.jobId);
    const list = await dispatch(ctxFor(root), "observatory.tests.list", {});
    assert.equal(list.result.jobs[0].status, "completed");
  });

  test("a functional failure and a driver timeout are reported as such", async () => {
    let root = await tempDir();
    let result = await cli(root, ["tests", "run", "--scenario", "sidebar", "--wait", "--json"], { mode: "fail" });
    assert.equal(result.code, 1);
    let { job } = json(result).result;
    assert.equal(job.status, "failed");
    assert.equal(job.results[0].status, "failed");
    root = await tempDir();
    result = await cli(root, ["tests", "run", "--scenario", "sidebar", "--wait", "--json"], { mode: "driver-timeout" });
    assert.equal(result.code, 124);
    ({ job } = json(result).result);
    assert.equal(job.status, "timed-out");
    assert.equal(job.results[0].status, "timeout");
  });

  test("only one UI test at a time; cancel stops only the job's own runner and app", async () => {
    const root = await tempDir();
    const ctx = ctxFor(root, { env: { ...process.env, FAKE_APP_MODE: "hang", ARCHIVIST_OBSERVATORY_APP: FAKE_APP } });
    const started = await dispatch(ctx, "observatory.tests.run", { scenario: "workspace" });
    assert.ok(started.ok, JSON.stringify(started.error));
    const { jobId } = started.result.job;
    // Wait until the app is running its scenario.
    for (let i = 0; i < 100; i += 1) {
      const status = await dispatch(ctx, "observatory.tests.status", { jobId });
      if (status.result.job.progress.current?.actionsPlanned != null) break;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    const running = await dispatch(ctx, "observatory.tests.status", { jobId });
    assert.equal(running.result.job.status, "running");
    assert.equal(running.result.job.progress.current.phase, "running scenario");
    assert.equal(running.result.job.progress.current.actionsPlanned, 4);
    assert.equal(running.result.job.progress.current.actionsDone, 0);

    const second = await dispatch(ctx, "observatory.tests.run", { scenario: "sidebar" });
    assert.equal(second.error.code, "busy");
    const plain = await cli(root, ["ui", "--scenario", "sidebar", "--skip-build"]);
    assert.equal(plain.code, 4);
    assert.match(plain.stderr, /Another UI test is running/);

    const bystander = spawn(process.execPath, ["-e", "setInterval(() => {}, 1000)"], { stdio: "ignore" });
    try {
      const cancelled = await dispatch(ctx, "observatory.tests.cancel", { jobId });
      assert.ok(cancelled.ok);
      const done = await waitForJob(root, jobId);
      assert.equal(done.result.job.status, "cancelled", JSON.stringify(done.result.job.history));
      assert.equal(done.result.job.results[0].status, "interrupted");
      assert.ok(done.result.job.cancelRequestedAt);
      // The runner saves its final status just before it exits.
      const runnerPid = started.result.job.pid ?? done.result.job.pid;
      for (let i = 0; i < 50 && isAlive(runnerPid); i += 1) await new Promise((resolve) => setTimeout(resolve, 100));
      assert.equal(isAlive(runnerPid), false);
      assert.equal(isAlive(bystander.pid), true, "an unrelated process was signalled");
      // The cancelled run's record survives and can be reported on.
      const report = await dispatch(ctx, "observatory.reports.generate", { runId: done.result.job.runs[0].runId });
      assert.ok(report.ok);
      assert.match(report.result.markdown, /interrupted/);
      // Cancelling a finished job is refused, not repeated.
      assert.equal((await dispatch(ctx, "observatory.tests.cancel", { jobId })).error.code, "not-running");
      await assert.rejects(readFile(lockFile(root)));
    } finally {
      bystander.kill();
    }
  });

  test("a job whose process is gone is reported as interrupted, and its lock is freed", async () => {
    const root = await tempDir();
    const jobId = "20261030T090000-000Z-job-abcdef";
    await mkdir(path.join(root, "jobs", jobId), { recursive: true });
    const dead = spawn(process.execPath, ["-e", ""]);
    await new Promise((resolve) => dead.on("exit", resolve));
    await writeFile(path.join(root, "jobs", jobId, "job.json"), JSON.stringify({
      schema: "archivist.observatory.job", schemaVersion: 1, jobId, status: "running", createdAt: "2026-10-30T09:00:00.000Z",
      startedAt: "2026-10-30T09:00:01.000Z", pid: dead.pid, plan: { scenarios: ["sidebar"], repeat: 1, totalRuns: 1 }, runs: [], history: [],
    }));
    await writeFile(lockFile(root), JSON.stringify({ owner: "job", jobId, pid: dead.pid, since: "2026-10-30T09:00:00.000Z" }));
    const status = await dispatch(ctxFor(root), "observatory.tests.status", { jobId });
    assert.equal(status.result.job.status, "interrupted");
    assert.match(status.result.job.error, /stopped without finishing/);
    await assert.rejects(readFile(lockFile(root)));
  });

  test("a live process that isn't the job's runner is never treated as the job", async () => {
    const root = await tempDir();
    const jobId = "20261030T090000-000Z-job-123456";
    await mkdir(path.join(root, "jobs", jobId), { recursive: true });
    const stranger = spawn(process.execPath, ["-e", "setInterval(() => {}, 1000)"], { stdio: "ignore" });
    try {
      await writeFile(path.join(root, "jobs", jobId, "job.json"), JSON.stringify({
        schema: "archivist.observatory.job", schemaVersion: 1, jobId, status: "running", createdAt: "2026-10-30T09:00:00.000Z",
        pid: stranger.pid, plan: { totalRuns: 1 }, runs: [], history: [],
      }));
      const response = await dispatch(ctxFor(root), "observatory.tests.cancel", { jobId });
      assert.equal(response.error.code, "not-running");
      assert.equal(isAlive(stranger.pid), true);
      assert.equal((await refreshJob(root, jobId)).job.status, "interrupted");
      // A stale lock left by a CLI process that has exited is taken over.
      await writeFile(lockFile(root), JSON.stringify({ owner: "cli", pid: 999999, since: "2026-10-30T09:00:00.000Z" }));
      assert.equal((await acquireLock(root, { owner: "cli", pid: process.pid })).ok, true);
    } finally {
      stranger.kill();
    }
  });

  test("a damaged job record is reported, not guessed at", async () => {
    const root = await tempDir();
    const jobId = "20261030T090000-000Z-job-0000ff";
    await mkdir(path.join(root, "jobs", jobId), { recursive: true });
    await writeFile(path.join(root, "jobs", jobId, "job.json"), "{ broken");
    const response = await dispatch(ctxFor(root), "observatory.tests.status", { jobId });
    assert.equal(response.error.code, "unreadable-record");
    assert.equal((await dispatch(ctxFor(root), "observatory.tests.status", { jobId: "20261030T090000-000Z-job-000000" })).error.code, "not-found");
  });
});

describe("structured results for agents", () => {
  let root;
  let uiRunId;
  before(async () => {
    root = await tempDir();
    const result = await cli(root, ["ui", "--scenario", "sidebar", "--skip-build", "--json"], { mode: "fail" });
    assert.equal(result.code, 1);
    const body = json(result);
    assert.equal(body.operation, "observatory.ui");
    assert.equal(body.ok, false);
    assert.equal(body.error.code, "test-failed");
    uiRunId = body.result.runs[0].runId;
    assert.equal(body.result.runs[0].status, "failed");
    await writeFixtureRuns(root, {
      "20261001T090000-000Z-build-00000a": { summary: fixtureBuild("20261001T090000-000Z-build-00000a", { commit: "a".repeat(40), total: 9000, phases: [1000, 1500, 500, 6000], status: "failure" }), files: { "build.log": "error: expected ';'\napi_key=sk-abcdefghijklmnopqrstuvwxyz\n" } },
      "20261002T090000-000Z-ui-sidebar-00000b": { summary: { raw: "{ not json" }, files: {} },
    });
  });

  test("--json prints one JSON document and nothing else on stdout", async () => {
    for (const args of [["show", uiRunId, "--json"], ["runs", "list", "--json"], ["report", uiRunId, "--json"], ["scenarios", "list", "--json"], ["tests", "list", "--json"]]) {
      const result = await cli(root, args);
      assert.equal(result.code, 0, `${args.join(" ")}: ${result.stderr}`);
      const body = json(result);
      assert.equal(body.schema, "archivist.observatory.response", args.join(" "));
    }
  });

  test("runs can be listed, filtered and read without repeating a test", async () => {
    const ctx = ctxFor(root);
    const all = await dispatch(ctx, "observatory.runs.list", {});
    assert.equal(all.result.total, 3);
    const ui = await dispatch(ctx, "observatory.runs.list", { kind: "ui-scenario", status: "failed" });
    assert.deepEqual(ui.result.runs.map((run) => run.runId), [uiRunId]);
    assert.equal(ui.result.runs[0].category, "ui:sidebar");
    const one = await dispatch(ctx, "observatory.runs.get", { runId: uiRunId });
    assert.equal(one.result.summary.kind, "ui-scenario");
    assert.deepEqual(one.result.artifacts.map((entry) => entry.name).sort(), ["events", "log", "scenario"]);
    assert.ok(one.result.artifacts.every((entry) => entry.available && entry.bytes > 0));
  });

  test("missing and damaged records give clear errors", async () => {
    const ctx = ctxFor(root);
    assert.equal((await dispatch(ctx, "observatory.runs.get", { runId: "20261003T090000-000Z-build-0000aa" })).error.code, "not-found");
    assert.equal((await dispatch(ctx, "observatory.runs.get", { runId: "20261002T090000-000Z-ui-sidebar-00000b" })).error.code, "unreadable-record");
    assert.equal((await dispatch(ctx, "observatory.reports.generate", { runId: "20261002T090000-000Z-ui-sidebar-00000b" })).error.code, "unreadable-record");
    assert.equal((await dispatch(ctx, "observatory.reports.export", { runId: "20261003T090000-000Z-build-0000aa" })).error.code, "not-found");
    const result = await cli(root, ["show", "20261003T090000-000Z-build-0000aa", "--json"]);
    assert.equal(result.code, 1);
    assert.equal(json(result).error.code, "not-found");
  });

  test("artifacts are read in bounded, redacted pieces, and only ones the run lists", async () => {
    const ctx = ctxFor(root);
    const log = await dispatch(ctx, "observatory.runs.artifact", { runId: "20261001T090000-000Z-build-00000a", name: "log", tailLines: 1 });
    assert.ok(log.ok);
    assert.equal(log.result.lines.length, 1);
    assert.equal(log.result.lines[0], "api_key=<redacted>");
    const raw = await dispatch(ctx, "observatory.runs.artifact", { runId: "20261001T090000-000Z-build-00000a", name: "log", raw: true });
    assert.match(raw.result.lines.join("\n"), /sk-abcdefghijklmnopqrstuvwxyz/);
    assert.equal((await dispatch(ctx, "observatory.runs.artifact", { runId: uiRunId, name: "../summary" })).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.runs.artifact", { runId: uiRunId, name: "secrets" })).error.code, "not-found");
  });

  test("runs compare uses the baseline rules and saves nothing", async () => {
    const response = await dispatch(ctxFor(root), "observatory.runs.compare", { runIds: [uiRunId] });
    assert.ok(response.ok);
    assert.equal(response.result.report.schema, "archivist.observatory.regression-report");
    assert.equal(response.result.report.categories[0].status, "no-baseline");
    await assert.rejects(readdir(path.join(root, "reports")));
  });
});

describe("diagnostic reports (Copy for AI)", () => {
  const ROOT = "/Users/someone/Library/Application Support/Archivist/Observatory";
  const redact = makeRedactor({ root: ROOT, home: "/Users/someone" });

  test("a UI report is compact, factual, and says what is missing", () => {
    const summary = fixtureUiRun("20261025T090000-000Z-ui-sidebar-00000f", { settled: 40, status: "failed", failed: 1, error: `1 action failed, see ${ROOT}/runs/x/runtime.log` });
    const events = [
      { index: 3, phase: "measured", op: "drag", label: "resize explorer", iteration: 1, ok: false, error: "timed out waiting for state", settledMs: null, frames: { maxIntervalMs: 120 } },
      { index: 4, phase: "measured", op: "click", label: "open library", iteration: 1, ok: true, settledMs: 220, frames: { maxIntervalMs: 180 } },
      { index: 5, phase: "measured", op: "click", label: "open library", iteration: 2, ok: true, settledMs: 30, frames: { maxIntervalMs: 16.7 } },
    ];
    const scenario = { description: "Opens the library.", initialConditions: "Fresh profile.", steps: [{ op: "click" }, { op: "drag" }], methods: { click: "input: synthetic click", drag: "input: synthetic drag", unused: "never" } };
    const { markdown, facts } = diagnosticReport({ runId: summary.runId, summary, events, scenario, logText: `ok\nerror: token=abc123 in /Users/someone/Documents/private.md\n`, redact });
    assert.ok(markdown.length < 6000, `report is ${markdown.length} bytes`);
    for (const heading of ["## Identity", "## Experiment", "## Result", "## Baseline comparison", "## Evidence", "## Observed together (correlation, not a cause)", "## Hypotheses (not verified)", "## Missing evidence", "## Reproduce"]) {
      assert.ok(markdown.includes(heading), heading);
    }
    assert.match(markdown, /Status: failed/);
    assert.match(markdown, /action 3 \(drag, resize explorer\): timed out waiting for state/);
    assert.match(markdown, /#4 open library \(iteration 1\): settled 220 ms, longest frame gap 180 ms/);
    assert.match(markdown, /median 40.0 ms, p95 88.0 ms, max 120 ms \(n=8\)/);
    assert.match(markdown, /No CPU profile, trace or QML profiler data/);
    assert.match(markdown, /npm run observatory:ui -- --scenario sidebar --skip-build/);
    // Code paths only for the failed and slowest steps, labelled as hypotheses.
    assert.match(markdown, /drag: input: synthetic drag/);
    assert.doesNotMatch(markdown, /unused: never/);
    // Redaction: no Observatory folder, home folder, user name or secret.
    assert.doesNotMatch(markdown, /someone|abc123|private\.md|Application Support/);
    assert.match(markdown, /<observatory>\/runs\/x\/runtime\.log/);
    assert.match(markdown, /token=<redacted>/);
    // Never claims a cause.
    assert.doesNotMatch(markdown, /root cause|caused by|because of/i);
    assert.equal(facts.schema, "archivist.observatory.diagnostic");
    assert.equal(facts.evidence.correlations.length, 1);
  });

  test("redaction hides private paths, names and secrets but keeps repository paths and hashes", () => {
    const r = makeRedactor({ root: ROOT, repo: "/Users/someone/Projects/Archivist", home: "/Users/someone" });
    const cases = [
      [`${ROOT}/runs/x/summary.json`, "<observatory>/runs/x/summary.json"],
      ["/Users/someone/Projects/Archivist/frontend/src/main.cpp:12: error: boom", "<repo>/frontend/src/main.cpp:12: error: boom"],
      ["open ~/Documents/Taxes 2025.pdf failed", "open ~/<path>"],
      ["/Users/other/Secret Folder/a.txt: not found", "/Users/<user>/<path>: not found"],
      ["file:///Users/someone/Notes/x.md", "file://~/<path>"],
      ["mail someone@example.com", "mail <email>"],
      ["Authorization: Bearer abc.def.ghi", "Authorization: <redacted> <redacted>"],
      ["password=hunter2 and api-key: xyz", "password=<redacted> and api-key: <redacted>"],
      ["key sk-abcdefghijklmnopqrstuvwxyz0123", "key <redacted>"],
      ["commit " + "a1".repeat(20), "commit " + "a1".repeat(20)],
      ["blob QWxhZGRpbjpvcGVuIHNlc2FtZQ9xYmFzZTY0c3RyaW5n", "blob <redacted>"],
    ];
    for (const [input, expected] of cases) assert.equal(r(input), expected, input);
  });

  test("a build report lists the failed phase, its errors and measured compile steps", () => {
    const summary = fixtureBuild("20261023T090000-000Z-build-00000d", { commit: "b".repeat(40), total: 8000, phases: [1000, 1500, 500, 5000], status: "failure" });
    const { markdown } = diagnosticReport({ runId: summary.runId, summary, redact });
    assert.match(markdown, /Status: failure, total 8\.00 s, failed in native-build/);
    assert.match(markdown, /error: expected ';' after expression/);
    assert.match(markdown, /main\.cpp: 2\.00 s/);
    assert.match(markdown, /npm run observatory:build/);
  });

  test("an unreadable or future record still gives a short, honest report", () => {
    const { markdown } = diagnosticReport({ runId: "20261029T090000-000Z-build-000015", summary: { schemaVersion: 99, kind: "build", status: "success" }, redact });
    assert.match(markdown, /Record problem: schemaVersion 99/);
  });
});

describe("evidence export", () => {
  function readZip(buffer) {
    const end = buffer.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
    const count = buffer.readUInt16LE(end + 10);
    let offset = buffer.readUInt32LE(end + 16);
    const files = {};
    for (let i = 0; i < count; i += 1) {
      assert.equal(buffer.readUInt32LE(offset), 0x02014b50);
      const crc = buffer.readUInt32LE(offset + 16);
      const size = buffer.readUInt32LE(offset + 20);
      const nameLength = buffer.readUInt16LE(offset + 28);
      const local = buffer.readUInt32LE(offset + 42);
      const name = buffer.toString("utf8", offset + 46, offset + 46 + nameLength);
      const localName = buffer.readUInt16LE(local + 26);
      const data = inflateRawSync(buffer.subarray(local + 30 + localName, local + 30 + localName + size));
      assert.equal(crc32(data), crc, name);
      files[name] = data;
      offset += 46 + nameLength;
    }
    return files;
  }

  test("an export is a redacted folder and zip with a manifest that checks out", async () => {
    const root = await tempDir();
    const ran = await cli(root, ["ui", "--scenario", "sidebar", "--skip-build", "--json"], { mode: "fail" });
    const runId = json(ran).result.runs[0].runId;
    const result = await cli(root, ["export", runId, "--json"]);
    assert.equal(result.code, 0, result.stderr);
    const { exportId, dir, zip, manifest } = json(result).result;
    assert.equal(manifest.schema, "archivist.observatory.evidence");
    assert.deepEqual(manifest.readFirst, ["report.md", "diagnostic.json"]);
    assert.equal(manifest.redaction.rawLogsIncluded, false);
    const names = manifest.files.map((file) => file.path);
    for (const name of ["report.md", "diagnostic.json", "summary.json", "ui-events.json", "scenario.json", "log-excerpt.txt"]) assert.ok(names.includes(name), name);
    assert.ok(!names.some((name) => name.startsWith("raw/")));
    for (const file of manifest.files) {
      const data = await readFile(path.join(dir, file.path));
      assert.equal(createHash("sha256").update(data).digest("hex"), file.sha256, file.path);
      assert.equal(data.length, file.bytes);
    }
    const unzipped = readZip(await readFile(zip));
    assert.deepEqual(Object.keys(unzipped).sort(), [...names, "manifest.json"].map((name) => `${exportId}/${name}`).sort());
    // The Observatory folder's path never appears in the redacted files.
    for (const [name, data] of Object.entries(unzipped)) assert.ok(!data.toString().includes(root), `${name} contains the folder path`);
  });

  test("full logs are included only when asked, and marked as not redacted", async () => {
    const root = await tempDir();
    const ran = await cli(root, ["ui", "--scenario", "sidebar", "--skip-build", "--json"]);
    const runId = json(ran).result.runs[0].runId;
    const response = await dispatch(ctxFor(root), "observatory.reports.export", { runId, includeRawLogs: true });
    assert.ok(response.ok);
    const raw = response.result.manifest.files.find((file) => file.path === "raw/runtime.log");
    assert.equal(raw.redacted, false);
    assert.equal(raw.sensitive, true);
    assert.equal(response.result.manifest.redaction.rawLogsIncluded, true);
  });

  test("controls never change recorded runs or baselines", async () => {
    const root = await tempDir();
    await cli(root, ["tests", "run", "--scenario", "sidebar", "--repeat", "3", "--wait", "--json"]);
    const ids = (await readdir(path.join(root, "runs"))).sort();
    await cli(root, ["baseline", "set", ...ids]);
    const baselines = await readFile(path.join(root, "baselines.json"), "utf8");
    const before = await hashRuns(root);
    const ctx = ctxFor(root);
    for (const id of ids) {
      await dispatch(ctx, "observatory.reports.generate", { runId: id, withRunIds: ids });
      await dispatch(ctx, "observatory.reports.export", { runId: id });
      await dispatch(ctx, "observatory.runs.compare", { runIds: ids });
    }
    assert.deepEqual(await hashRuns(root), before);
    assert.equal(await readFile(path.join(root, "baselines.json"), "utf8"), baselines);
  });
});

describe("HTTP controls", () => {
  let readOnly;
  let controlled;
  const TOKEN = "k".repeat(32);

  function call(server, pathname, { method = "POST", headers = {}, body = "{}" } = {}) {
    const port = server.address().port;
    return new Promise((resolve, reject) => {
      const req = request({ host: "127.0.0.1", port, path: pathname, method, headers: { host: `127.0.0.1:${port}`, ...headers } }, (res) => {
        let text = "";
        res.on("data", (chunk) => { text += chunk; });
        res.on("end", () => resolve({ status: res.statusCode, headers: res.headers, body: text ? JSON.parse(text) : null }));
      });
      req.on("error", reject);
      if (method !== "GET" && method !== "OPTIONS") req.end(body);
      else req.end();
    });
  }

  const good = (server) => ({ origin: `http://127.0.0.1:${server.address().port}`, "content-type": "application/json", "x-observatory-token": TOKEN });

  before(async () => {
    const root = await tempDir();
    readOnly = createDashboardServer({ root, port: 0 });
    controlled = createDashboardServer({ root, port: 0, control: { token: TOKEN, ctx: ctxFor(root) } });
    await Promise.all([readOnly, controlled].map((server) => new Promise((resolve) => server.listen(0, "127.0.0.1", resolve))));
  });
  after(() => {
    readOnly.close();
    controlled.close();
  });

  test("a read-only dashboard has no controls", async () => {
    const response = await call(readOnly, "/api/control/observatory.capabilities", { headers: good(readOnly) });
    assert.equal(response.status, 404);
    assert.equal(response.body.error.code, "controls-disabled");
    assert.equal((await call(readOnly, "/api/runs", { method: "GET" })).status, 200);
  });

  test("a request with the key from the page's own origin is answered", async () => {
    const response = await call(controlled, "/api/control/observatory.capabilities", { headers: good(controlled) });
    assert.equal(response.status, 200);
    assert.equal(response.body.ok, true);
    assert.equal((await call(controlled, "/api/runs", { method: "GET" })).status, 200);
  });

  test("other origins, missing keys, forms, preflights and foreign hosts are refused", async () => {
    const base = good(controlled);
    const cases = [
      [{ ...base, "x-observatory-token": undefined }, 401, "unauthorized"],
      [{ ...base, "x-observatory-token": "k".repeat(31) + "j" }, 401, "unauthorized"],
      [{ ...base, origin: "http://evil.example" }, 403, "forbidden-origin"],
      [{ ...base, origin: undefined }, 403, "forbidden-origin"],
      [{ ...base, "sec-fetch-site": "cross-site" }, 403, "forbidden-origin"],
      [{ ...base, "content-type": "text/plain" }, 400, "invalid-input"],
      [{ ...base, "content-type": "application/x-www-form-urlencoded" }, 400, "invalid-input"],
    ];
    for (const [headers, status, code] of cases) {
      const clean = Object.fromEntries(Object.entries(headers).filter(([, value]) => value !== undefined));
      const response = await call(controlled, "/api/control/observatory.tests.run", { headers: clean });
      assert.equal(response.status, status, JSON.stringify(headers));
      assert.equal(response.body.error.code, code);
    }
    const preflight = await call(controlled, "/api/control/observatory.tests.run", { method: "OPTIONS", headers: { origin: "http://evil.example", "access-control-request-method": "POST" } });
    assert.notEqual(preflight.status, 200);
    assert.equal(preflight.headers["access-control-allow-origin"], undefined);
    const foreignHost = await call(controlled, "/api/control/observatory.capabilities", { headers: { ...base, host: "evil.example" } });
    assert.equal(foreignHost.status, 403);
    const get = await call(controlled, "/api/control/observatory.capabilities", { method: "GET", headers: base });
    assert.equal(get.status, 400);
    const badJson = await call(controlled, "/api/control/observatory.capabilities", { headers: base, body: "{ nope" });
    assert.equal(badJson.body.error.code, "invalid-input");
    const unknown = await call(controlled, "/api/control/observatory.shell", { headers: base });
    assert.equal(unknown.status, 404);
    assert.equal(unknown.body.error.code, "unknown-operation");
  });

  test("the gate itself refuses everything without a key", () => {
    const origins = new Set(["http://127.0.0.1:1"]);
    assert.equal(checkControlRequest({ method: "POST", headers: {} }, { token: null, origins }).code, "controls-disabled");
    assert.equal(checkControlRequest({ method: "POST", headers: { origin: "http://127.0.0.1:1", "content-type": "application/json", "x-observatory-token": "" } }, { token: "abc", origins }).code, "unauthorized");
  });
});
