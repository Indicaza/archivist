// Audits: presets run through the real job runner and the stand-in app, the
// findings rules, and audit-to-audit comparison with the regression rules.
// The acceptance tests from the Lighthouse brief:
//   - UI: a deliberately slow operation is found, named and attributed;
//   - regression: a baseline audit vs a slowed-down one is a regression,
//     a single run never is, and incompatible audits are refused;
//   - build: a changed shared header is reported with its fan-out;
//   - end to end: run, poll, findings, timeline, operation, report, export.
import assert from "node:assert/strict";
import { execFile, execFileSync, spawnSync } from "node:child_process";
import { mkdir, readdir, readFile, utimes, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, test } from "node:test";
import { collectBuildEvidence, explainCollector } from "../build-evidence.mjs";
import { dispatch } from "../control/operations.mjs";
import { compareAudits, evaluateFindings, PRESETS, RULES } from "../dashboard/public/audit.mjs";
import { normalizeRun } from "../dashboard/public/normalize.mjs";
import { CATEGORIES } from "../dashboard/public/regression.mjs";
import { ninjaLogSize } from "../ninja.mjs";
import { BUILD_DIR, CLI_PATH, REPO_ROOT, SCENARIO_DIR } from "../paths.mjs";
import { fixtureUiRun, writeFixtureRuns } from "./fixtures/dashboard-runs.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE_APP = path.join(HERE, "fixtures", "fake-app.mjs");
const has = (command) => spawnSync(command, ["--version"], { stdio: "ignore" }).status === 0;
const budgets = JSON.parse(await readFile(path.join(HERE, "..", "budgets.json"), "utf8"));

function cli(root, args, env = {}) {
  return new Promise((resolve) => {
    execFile(process.execPath, [CLI_PATH, ...args], {
      env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root, ARCHIVIST_OBSERVATORY_APP: FAKE_APP, FAKE_APP_MODE: "slow", ...env },
      timeout: 120000,
    }, (error, stdout, stderr) => resolve({ code: error ? error.code : 0, stdout, stderr }));
  });
}

const json = (result) => {
  try {
    return JSON.parse(result.stdout);
  } catch {
    throw new Error(`not JSON:\n${result.stdout}\n${result.stderr}`);
  }
};

function ctxFor(root, env = {}) {
  return { root, repoRoot: REPO_ROOT, buildDir: BUILD_DIR, scenarioDir: SCENARIO_DIR, cliPath: CLI_PATH, appBinary: FAKE_APP, env: { ...process.env, ARCHIVIST_OBSERVATORY_APP: FAKE_APP, FAKE_APP_MODE: "slow", ...env } };
}

async function runAudit(root, preset, env = {}, extra = []) {
  const result = await cli(root, ["audit", "run", "--preset", preset, "--wait", "--json", ...extra], env);
  const response = json(result);
  assert.ok(response.ok, JSON.stringify(response.error ?? response));
  return response.result;
}

// A ui-scenario summary with operations, for rule tests.
function uiSummary({ scenario = "chat", status = "passed", steps, operations = null, frameGap = 16.7, startup = null }) {
  return {
    kind: "ui-scenario", status, scenario: { id: scenario, name: scenario, version: 1 },
    actions: { failed: status === "passed" ? 0 : 1, firstFailure: status === "passed" ? null : { index: 2, label: "open", error: "timed out" } },
    stats: { measured: { frameMaxIntervalMs: { medianMs: frameGap } }, steps: steps.map(([step, label, settled]) => ({ step, label, settledMs: { medianMs: settled } })) },
    operations: operations === false ? { collected: false, reason: "old build" } : { collected: true, steps: operations ?? [], startup },
  };
}

describe("findings rules", () => {
  test("every rule has an id, a subsystem and a threshold in words; presets name real scenarios", async () => {
    const ids = RULES.map((rule) => rule.id);
    assert.equal(new Set(ids).size, ids.length);
    assert.ok(RULES.every((rule) => rule.title && rule.subsystem && rule.when && rule.basis));
    const scenarios = (await readdir(SCENARIO_DIR)).map((name) => name.replace(/\.json$/, ""));
    for (const preset of Object.values(PRESETS)) assert.ok(preset.scenarios.every((id) => scenarios.includes(id)), preset.id);
    assert.ok(!PRESETS.build.description.includes("clean build") || /Nothing is cleaned/.test(PRESETS.build.description));
    for (const id of ["startup", "sidebar", "workspace", "chat"]) assert.ok(CATEGORIES[`ui:${id}`], `ui:${id} has a regression category`);
  });

  test("a slow operation is reported once, as the child that holds the time, inside its parent", () => {
    const operations = [{ step: 1, label: "open big", runs: 5, stalls: 2, unattributedMedianMs: 10, operations: [
      { name: "chat.open-conversation", parent: null, n: 5, medianMs: 260, maxMs: 300 },
      { name: "chat.publish-messages", parent: "chat.open-conversation", n: 5, medianMs: 210, maxMs: 240 },
      { name: "chat.load-message-page", parent: "chat.open-conversation", n: 5, medianMs: 30, maxMs: 31 },
    ] }];
    const runs = [1, 2, 3].map((n) => ({ runId: `r${n}`, summary: uiSummary({ steps: [[1, "open big", 280], [2, "open small", 20]], operations }) }));
    const findings = evaluateFindings(runs);
    const slowOps = findings.filter((finding) => finding.rule === "ui.slow-operation");
    assert.equal(slowOps.length, 1, "open-conversation is explained by publish-messages; load-message-page is under 50 ms");
    assert.equal(slowOps[0].operation, "chat.publish-messages");
    assert.equal(slowOps[0].subsystem, "chat");
    assert.equal(slowOps[0].severity, "high");
    assert.match(slowOps[0].summary, /inside chat\.open-conversation \(260 ms\)/);
    assert.deepEqual(slowOps[0].evidence.map((item) => item.runId), ["r1", "r2", "r3"]);
    assert.equal(slowOps[0].confidence.level, "consistent");
    const step = findings.find((finding) => finding.rule === "ui.slow-step");
    assert.match(step.summary, /slowest operation inside it is chat\.publish-messages/);
    assert.ok(!findings.some((finding) => finding.id.includes("open small")), "a 20 ms step is not a finding");
    assert.ok(findings.some((finding) => finding.rule === "ui.stalls"));
    for (const finding of findings) {
      assert.ok(finding.id && finding.title && finding.subsystem && finding.severity && finding.nextStep && finding.confidence, finding.id);
      assert.ok(RULES.some((rule) => rule.id === finding.rule));
    }
  });

  test("one run says so; failures, missing operations, unexplained time and frame gaps", () => {
    const single = evaluateFindings([{ runId: "a", summary: uiSummary({ steps: [[0, "x", 150]], operations: [{ step: 0, label: "x", runs: 1, stalls: 0, unattributedMedianMs: 120, operations: [] }], frameGap: 50 }) }]);
    assert.equal(single.find((finding) => finding.rule === "ui.slow-step").confidence.level, "single-run");
    assert.ok(single.some((finding) => finding.rule === "ui.unattributed"), "120 of 150 ms outside every span");
    assert.match(single.find((finding) => finding.rule === "ui.frame-gap").summary, /60 Hz/);
    const failed = evaluateFindings([{ runId: "f", summary: uiSummary({ status: "failed", steps: [[0, "x", 500]] }) }]);
    assert.deepEqual(failed.map((finding) => finding.rule), ["ui.failed-actions"], "a failed run's timings are not used");
    const old = evaluateFindings([{ runId: "o", summary: uiSummary({ steps: [[0, "x", 10]], operations: false }) }]);
    assert.deepEqual(old.map((finding) => finding.rule), ["ui.no-operations"]);
    const startup = evaluateFindings([{ runId: "s", summary: uiSummary({ scenario: "startup", steps: [[0, "noop", 5]], startup: { stages: [{ name: "qml-load", durationMs: 1200 }, { name: "profile", durationMs: 4 }] } }) }]);
    assert.deepEqual(startup.map((finding) => [finding.rule, finding.severity]), [["startup.slow-stage", "high"]]);
  });

  test("build rules read the job evidence", () => {
    const evidence = {
      jobs: 40, timing: { wallMs: 20000, summedJobMs: 60000, peakParallelJobs: 8 },
      byCategory: { compile: { jobs: 30, summedMs: 40000 }, link: { jobs: 2, summedMs: 6000 }, archive: { jobs: 2, summedMs: 500 }, autogen: { jobs: 6, summedMs: 9500 } },
      slowestCompiles: [{ id: 3, output: "a.cpp.o", source: "src/a.cpp", durationMs: 9000 }],
      criticalPath: { available: true, lengthMs: 17000, jobs: [{ output: "a.cpp.o", source: "src/a.cpp" }, { output: "app" }] },
      rebuild: { causes: [{ input: "src/shared.h", inputKind: "header", outputs: 14, compiles: 12, includedBy: { objects: 12 } }, { input: "CMakeLists.txt", inputKind: "cmake", outputs: 1, compiles: 0 }] },
      compilerCache: { configured: true, tool: "ccache", observed: { hits: 2, misses: 18, uncacheable: 0, hitRate: 0.1 } },
    };
    const rules = evaluateFindings([{ runId: "b", summary: { kind: "build", status: "success", evidence } }]).map((finding) => finding.rule).sort();
    assert.deepEqual(rules, ["build.autogen-share", "build.cache-misses", "build.chain-bound", "build.cmake-rerun", "build.dominant-unit", "build.header-fanout", "build.link-share"]);
    assert.deepEqual(evaluateFindings([{ runId: "b", summary: { kind: "build", status: "failure", phases: [{ name: "native-build", status: "failure" }] } }]).map((finding) => finding.rule), ["build.failed"]);
    assert.deepEqual(evaluateFindings([{ runId: "b", summary: { kind: "build", status: "success" } }]).map((finding) => finding.rule), ["build.missing-evidence"]);
  });

  test("build acceptance: a changed header many files include is a finding with its fan-out", { skip: !(has("cmake") && has("ninja")) && "cmake or ninja not installed" }, async () => {
    const dir = await tempDir();
    const src = path.join(dir, "src");
    const build = path.join(dir, "build");
    await mkdir(src);
    const names = Array.from({ length: 12 }, (_, index) => `u${index}`);
    await writeFile(path.join(src, "CMakeLists.txt"), `cmake_minimum_required(VERSION 3.20)\nproject(Fx CXX)\nadd_library(core STATIC ${names.map((name) => `${name}.cpp`).join(" ")})\nadd_executable(app main.cpp lone.cpp)\ntarget_link_libraries(app core)\n`);
    await writeFile(path.join(src, "shared.h"), "#pragma once\ninline int shared() { return 1; }\n");
    for (const name of names) await writeFile(path.join(src, `${name}.cpp`), `#include "shared.h"\nint f_${name}() { return shared(); }\n`);
    await writeFile(path.join(src, "main.cpp"), "int main() { return 0; }\n");
    await writeFile(path.join(src, "lone.cpp"), "int lone() { return 2; }\n");
    execFileSync("cmake", ["-S", src, "-B", build, "-G", "Ninja"], { stdio: "ignore" });
    execFileSync("ninja", ["-C", build], { stdio: "ignore" });
    const later = new Date(Date.now() + 5000);
    await utimes(path.join(src, "shared.h"), later, later);
    const explain = explainCollector();
    const dry = spawnSync("ninja", ["-C", build, "-n", "-d", "explain"], { encoding: "utf8" });
    for (const line of `${dry.stdout}\n${dry.stderr}`.split("\n")) explain.line(line);
    const logBefore = await ninjaLogSize(path.join(build, ".ninja_log"));
    execFileSync("ninja", ["-C", build], { stdio: "ignore" });
    const { summary: evidence } = await collectBuildEvidence({ root: src, buildDir: build, logBefore, explain: explain.result() });
    const findings = evaluateFindings([{ runId: "build-1", summary: { kind: "build", status: "success", evidence } }]);
    const fanout = findings.find((finding) => finding.rule === "build.header-fanout");
    assert.ok(fanout, JSON.stringify(findings.map((finding) => finding.rule)));
    assert.equal(fanout.id, "build.header-fanout:shared.h");
    assert.equal(fanout.impact.value, 12);
    assert.match(fanout.summary, /Changing shared\.h rebuilt 12 files \(12 objects include it\)/);
  });
});

describe("audit comparison", () => {
  const MAC = { platform: "darwin", osVersion: "25.0.0", architecture: "arm64", cpuModel: "Fixture M", logicalCpus: 10, totalMemoryBytes: 1 };
  const audit = (runId, preset, runIds, rulesVersion = 1) => normalizeRun(runId, { schemaVersion: 1, kind: "audit", runId, status: "completed", preset, rulesVersion, children: runIds.map((id) => ({ runId: id })), findings: [] });
  const ui = (id, settled, environment = MAC) => normalizeRun(id, fixtureUiRun(id, { scenario: "chat", settled, environment }));
  const ids = (day, letter, count) => Array.from({ length: count }, (_, index) => `202601${day}T00000${index}-000Z-ui-chat-${letter.repeat(5)}${index}`);

  test("three slowed runs against three baseline runs is a regression; one run each never is", () => {
    const before = ids("01", "a", 3);
    const after = ids("02", "b", 3);
    const runs = [...before.map((id, n) => ui(id, 20 + n)), ...after.map((id, n) => ui(id, 40 + n))];
    const result = compareAudits({ audit: audit("20260102T000000-000Z-audit-bbbbbb", "ui", after), baseline: audit("20260101T000000-000Z-audit-aaaaaa", "ui", before), runs, budgets });
    assert.equal(result.status, "compared");
    const chat = result.categories.find((category) => category.category === "ui:chat");
    assert.equal(chat.verdict, "regression");
    assert.equal(result.verdict, "regression");

    const single = compareAudits({ audit: audit("20260102T000000-000Z-audit-bbbbbb", "quick", after.slice(0, 1)), baseline: audit("20260101T000000-000Z-audit-aaaaaa", "quick", before.slice(0, 1)), runs, budgets });
    const one = single.categories.find((category) => category.category === "ui:chat");
    assert.equal(one.verdict, "insufficient-evidence", "doubling in one run is still only a lead");
    assert.ok(one.metrics.some((metric) => metric.overBudget && metric.verdict === "insufficient-evidence"));
  });

  test("incompatible audits are refused, not compared", () => {
    const before = ids("01", "a", 3);
    const after = ids("02", "b", 3);
    const runs = [...before.map((id) => ui(id, 20)), ...after.map((id) => ui(id, 60, { ...MAC, cpuModel: "Another CPU" }))];
    const machine = compareAudits({ audit: audit("20260102T000000-000Z-audit-bbbbbb", "ui", after), baseline: audit("20260101T000000-000Z-audit-aaaaaa", "ui", before), runs, budgets });
    assert.equal(machine.verdict, "incompatible");
    assert.match(machine.categories[0].reasons.join(" "), /cpuModel: Fixture M vs Another CPU/);
    assert.equal(compareAudits({ audit: audit("x2", "ui", after), baseline: audit("x1", "quick", before), runs, budgets }).status, "incompatible");
    assert.match(compareAudits({ audit: audit("x2", "ui", after, 2), baseline: audit("x1", "ui", before), runs, budgets }).blockers[0], /rules v1 vs v2/);
  });
});

describe("audits end to end", () => {
  test("quick audit: run, findings, timeline, operation tree, report and export", async () => {
    const root = await tempDir();
    const presets = await dispatch(ctxFor(root), "observatory.audit.presets", {});
    assert.ok(presets.result.presets.find((preset) => preset.id === "quick").ready);
    const { job, audit } = await runAudit(root, "quick");
    assert.equal(job.status, "completed");
    assert.equal(job.kind, "audit");
    assert.ok(job.auditRunId);
    assert.equal(audit.auditId, job.auditRunId);
    assert.deepEqual(job.results.map((run) => run.scenario), ["startup", "workspace"]);

    const findings = json(await cli(root, ["audit", "findings", job.auditRunId, "--json"])).result;
    const slow = findings.findings.find((finding) => finding.rule === "ui.slow-operation" && finding.scenario === "workspace");
    assert.equal(slow.operation, "chat.publish-messages");
    assert.equal(slow.confidence.level, "single-run");
    assert.ok(findings.findings.some((finding) => finding.rule === "startup.slow-stage" && finding.id === "startup.slow-stage:qml-load"));
    assert.equal(findings.rules.length, RULES.length);
    const high = json(await cli(root, ["audit", "findings", job.auditRunId, "--severity", "high", "--json"])).result.findings;
    assert.ok(high.length && high.every((finding) => finding.severity === "high"));

    const workspaceRun = slow.evidence[0].runId;
    const timeline = json(await cli(root, ["audit", "timeline", workspaceRun, "--from", "350", "--to", "1300", "--json"])).result;
    assert.deepEqual(timeline.actions.map((action) => action.index), [1, 2, 3], "actions overlapping 350..1300 ms");
    assert.equal(timeline.stalls.length, 2);
    const operation = json(await cli(root, ["audit", "operation", workspaceRun, "3", "--json"])).result;
    assert.equal(operation.action.operations[0].name, "chat.open-conversation");
    assert.deepEqual(operation.action.operations[0].children.map((child) => child.name), ["chat.load-message-page", "chat.publish-messages"]);
    assert.equal((await cli(root, ["audit", "operation", workspaceRun, "99", "--json"])).code, 1);

    const report = await cli(root, ["audit", "report", job.auditRunId]);
    assert.equal(report.code, 0);
    assert.match(report.stdout, /# Archivist Observatory audit: Quick/);
    assert.match(report.stdout, /rule `ui\.slow-operation`/);
    assert.match(report.stdout, /No earlier audit with this preset/);
    assert.match(report.stdout, /one round is never enough|Fewer than three rounds/);
    assert.match(report.stdout, /\| ui\.stalls \|/);

    const exported = json(await cli(root, ["audit", "export", job.auditRunId, "--json"])).result;
    const files = exported.manifest.files.map((file) => file.path);
    assert.ok(files.includes("report.md") && files.includes("diagnostic.json") && files.includes("summary.json"));
    for (const child of job.results) {
      assert.ok(files.includes(`runs/${child.runId}/report.md`), child.runId);
      assert.ok(files.includes(`runs/${child.runId}/ui-operations.json`), child.runId);
    }
    assert.ok(!exported.manifest.files.some((file) => file.sensitive), "no raw logs unless asked");

    const list = json(await cli(root, ["audit", "list", "--json"])).result;
    assert.equal(list.audits[0].auditId, job.auditRunId);
    assert.equal(json(await cli(root, ["runs", "list", "--kind", "audit", "--json"])).result.runs[0].preset, "quick");
  });

  test("regression acceptance: a Full UI audit, then the same with the slow operations doubled", async () => {
    const root = await tempDir();
    const first = await runAudit(root, "ui", {}, ["--repeat", "3"]);
    const second = await runAudit(root, "ui", { FAKE_APP_SCALE: "2" }, ["--repeat", "3"]);
    const comparison = json(await cli(root, ["audit", "compare", second.job.auditRunId, "--json"])).result;
    assert.equal(comparison.baselineAuditId, first.job.auditRunId, "the previous audit of the same preset");
    assert.equal(comparison.verdict, "regression");
    const chat = comparison.categories.find((category) => category.category === "ui:chat");
    assert.equal(chat.verdict, "regression");
    assert.ok(chat.steps.some((step) => step.verdict === "regression" && step.label.includes("step 1")));
    const report = await cli(root, ["audit", "report", second.job.auditRunId]);
    assert.match(report.stdout, /Overall: regression/);

    // A quick audit (one round) compared with another is never a regression.
    await runAudit(root, "quick");
    const quick = await runAudit(root, "quick", { FAKE_APP_SCALE: "3" });
    const single = json(await cli(root, ["audit", "compare", quick.job.auditRunId, "--json"])).result;
    assert.notEqual(single.verdict, "regression");
    // And a quick audit isn't compared with a Full UI one.
    const crossed = json(await cli(root, ["audit", "compare", quick.job.auditRunId, first.job.auditRunId, "--json"])).result;
    assert.equal(crossed.status, "incompatible");
  });

  test("a cancelled audit is still written, marked cancelled", async () => {
    const root = await tempDir();
    const ctx = ctxFor(root, { FAKE_APP_MODE: "hang" });
    const started = await dispatch(ctx, "observatory.audit.run", { preset: "quick" });
    assert.ok(started.ok, JSON.stringify(started.error));
    const { jobId } = started.result.job;
    for (let i = 0; i < 100; i += 1) {
      if ((await dispatch(ctx, "observatory.audit.status", { jobId })).result.job.progress.current?.actionsPlanned != null) break;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    assert.ok((await dispatch(ctx, "observatory.audit.cancel", { jobId })).ok);
    let status;
    for (let i = 0; i < 200; i += 1) {
      status = await dispatch(ctx, "observatory.audit.status", { jobId });
      if (!["queued", "starting", "running"].includes(status.result.job.status)) break;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    assert.equal(status.result.job.status, "cancelled");
    assert.equal(status.result.audit.status, "cancelled");
    assert.ok(status.result.audit.counts.high >= 1, "the interrupted run is a failed-actions finding");
  });

  test("bad input is refused before anything runs", async () => {
    const root = await tempDir();
    assert.equal((await dispatch(ctxFor(root), "observatory.audit.run", { preset: "everything" })).error.code, "invalid-input");
    assert.equal((await dispatch(ctxFor(root), "observatory.audit.findings", { auditId: "nope" })).error.code, "invalid-input");
    await writeFixtureRuns(root, { "20260101T000000-000Z-ui-chat-aaaaaa": { summary: fixtureUiRun("20260101T000000-000Z-ui-chat-aaaaaa", { scenario: "chat", settled: 10 }), files: {} } });
    assert.equal((await dispatch(ctxFor(root), "observatory.audit.findings", { auditId: "20260101T000000-000Z-ui-chat-aaaaaa" })).error.code, "invalid-input");
    assert.equal((await dispatch(ctxFor(root), "observatory.audit.timeline", { runId: "20260101T000000-000Z-ui-chat-aaaaaa" })).error.code, "not-found", "no operations recorded");
  });
});
