import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile, readdir, stat, writeFile } from "node:fs/promises";
import { request } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, test } from "node:test";
import { baselineEntry, baselinesPath, readBaselines, removeBaseline, setBaseline } from "../baselines.mjs";
import { analyze, loadBudgets, saveReport } from "../comparisons.mjs";
import { loadRuns } from "../dashboard/records.mjs";
import { normalizeRun } from "../dashboard/public/normalize.mjs";
import {
  buildReport, categoryOf, compareMetric, METRIC_DEFINITIONS_VERSION, REPORT_SCHEMA, resolveBudgets, selectCandidates,
} from "../dashboard/public/regression.mjs";
import { createDashboardServer } from "../dashboard/server.mjs";
import { formatRegressionReport } from "../regression-report.mjs";
import { fixtureBuild, fixtureUiRun, writeFixtureRuns } from "./fixtures/dashboard-runs.mjs";
import { tempDir } from "./helpers.mjs";

const CLI = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "cli.mjs");
const OTHER_MAC = { platform: "darwin", osVersion: "25.0.0", architecture: "x64", cpuModel: "Fixture Intel", logicalCpus: 8, totalMemoryBytes: 1, nodeVersion: "v24.0.0" };

// Fixture run ids, in time order: day `d`, hour `h`.
function id(day, hour, kind, n) {
  return `202611${String(day).padStart(2, "0")}T${String(hour).padStart(2, "0")}0000-000Z-${kind}-${n.toString(16).padStart(6, "0")}`;
}

function sidebar(day, hour, n, settled, extra = {}) {
  return fixtureUiRun(id(day, hour, "ui-sidebar", n), { settled, ...extra });
}

// Three baseline sidebar runs (~30 ms), then three slower ones (~45 ms),
// one failed run and one from another machine.
function scenarioRuns() {
  const runs = [
    sidebar(1, 9, 1, 30), sidebar(1, 10, 2, 31), sidebar(1, 11, 3, 29),
    sidebar(2, 9, 4, 45), sidebar(2, 10, 5, 46), sidebar(2, 11, 6, 44),
    sidebar(2, 12, 7, 60, { status: "failed", failed: 1, error: "1 action failed" }),
    sidebar(2, 13, 8, 30, { environment: OTHER_MAC }),
    fixtureBuild(id(1, 9, "build", 9), { commit: "c".repeat(40), total: 60000, phases: [9000, 3000, 2000, 46000], classification: "clean" }),
    fixtureBuild(id(2, 9, "build", 10), { commit: "c".repeat(40), total: 12000, phases: [1000, 1500, 500, 9000], classification: "incremental" }),
  ];
  return Object.fromEntries(runs.map((summary) => [summary.runId, { summary, files: {} }]));
}

async function fixtureRoot(runs = scenarioRuns()) {
  const root = await tempDir();
  await writeFixtureRuns(root, runs);
  return root;
}

async function snapshotRuns(root) {
  const dir = path.join(root, "runs");
  const out = {};
  for (const name of await readdir(dir)) {
    const file = path.join(dir, name, "summary.json");
    out[name] = { text: await readFile(file, "utf8"), mtime: (await stat(file)).mtimeMs };
  }
  return out;
}

const BASE = [id(1, 9, "ui-sidebar", 1), id(1, 10, "ui-sidebar", 2), id(1, 11, "ui-sidebar", 3)];
const SLOWER = [id(2, 9, "ui-sidebar", 4), id(2, 10, "ui-sidebar", 5), id(2, 11, "ui-sidebar", 6)];
const FAILED = id(2, 12, "ui-sidebar", 7);
const OTHER = id(2, 13, "ui-sidebar", 8);
const CLEAN = id(1, 9, "build", 9);
const INCREMENTAL = id(2, 9, "build", 10);

const budget = (extra = {}) => ({ direction: "lower", regressionPercent: 10, improvementPercent: 10, minAbsolute: 2, minSamples: 3, ...extra });

describe("baselines", () => {
  test("are saved in the Observatory folder and read back", async () => {
    const root = await fixtureRoot();
    const { entry } = await setBaseline(root, BASE, { note: "before sidebar work" });
    assert.equal(entry.category, "ui:sidebar");
    const { data, error } = await readBaselines(root);
    assert.equal(error, null);
    const saved = data.baselines["ui:sidebar"];
    assert.deepEqual(saved.runIds, BASE);
    assert.equal(saved.note, "before sidebar work");
    assert.equal(saved.metricDefinitionsVersion, METRIC_DEFINITIONS_VERSION);
    assert.ok(saved.metricsMeasured.includes("settled.median"));
    assert.equal(saved.reference.commit, "a".repeat(40));
    assert.equal(saved.reference.machine.cpuModel, "Fixture M-series");
    assert.deepEqual(saved.reference.scenario, { id: "sidebar", version: 1, driverVersion: 1 });
    // Survives a "restart": a fresh read of the same folder.
    assert.deepEqual((await readBaselines(root)).data, data);
  });

  test("replacing or removing keeps the old one in history and never touches runs", async () => {
    const root = await fixtureRoot();
    const before = await snapshotRuns(root);
    await setBaseline(root, BASE);
    await setBaseline(root, SLOWER);
    await setBaseline(root, [CLEAN]);
    let { data } = await readBaselines(root);
    assert.deepEqual(data.baselines["ui:sidebar"].runIds, SLOWER);
    assert.equal(data.history.length, 1);
    assert.equal(data.history[0].change, "replaced");
    assert.deepEqual(data.history[0].runIds, BASE);
    await removeBaseline(root, "ui:sidebar");
    ({ data } = await readBaselines(root));
    assert.equal(data.baselines["ui:sidebar"], undefined);
    assert.ok(data.baselines["build:clean"]);
    assert.equal(data.history.at(-1).change, "removed");
    assert.deepEqual(await snapshotRuns(root), before);
    await assert.rejects(removeBaseline(root, "ui:sidebar"), /no ui:sidebar baseline/);
  });

  test("only successful, readable runs of one workload can be a baseline", async () => {
    const root = await fixtureRoot();
    await assert.rejects(setBaseline(root, [FAILED]), /did not succeed/);
    await assert.rejects(setBaseline(root, [BASE[0], CLEAN]), /one kind of workload/);
    await assert.rejects(setBaseline(root, [BASE[0], OTHER]), /differ in/);
    await assert.rejects(setBaseline(root, [BASE[0], BASE[0]]), /listed twice/);
    await assert.rejects(setBaseline(root, ["not-a-run"]), /not an Observatory run id/);
    await assert.rejects(setBaseline(root, [id(9, 9, "build", 99)]), /missing/);
    await assert.rejects(setBaseline(root, []), /at least one run/);
    const unclassified = fixtureBuild(id(3, 9, "build", 11), { commit: "d".repeat(40), total: 1, phases: [1, 0, 0, 0] });
    unclassified.buildConditions.classification = null;
    assert.throws(() => baselineEntry([normalizeRun(unclassified.runId, unclassified)]), /wasn't classified/);
    assert.equal((await readBaselines(root)).data.history.length, 0);
  });

  test("a damaged or newer baselines file is never overwritten", async () => {
    const root = await fixtureRoot();
    await writeFile(baselinesPath(root), "{ not json");
    await assert.rejects(setBaseline(root, BASE), /not valid JSON.*was not changed/s);
    assert.equal(await readFile(baselinesPath(root), "utf8"), "{ not json");
    await writeFile(baselinesPath(root), JSON.stringify({ schema: "archivist.observatory.baselines", schemaVersion: 2, baselines: {} }));
    await assert.rejects(setBaseline(root, BASE), /schemaVersion 2/);
    assert.match((await readBaselines(root)).error, /schemaVersion 2/);
  });
});

describe("comparing one metric", () => {
  test("lower is better: slower past the budget, with no overlap, is a regression", () => {
    const result = compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [30, 31, 29], candidateValues: [45, 46, 44] });
    assert.equal(result.verdict, "regression");
    assert.equal(result.delta, 15);
    assert.equal(result.deltaPercent, 50);
    assert.equal(result.overBudget, true);
    assert.deepEqual([result.baseline.min, result.baseline.max, result.candidate.n], [29, 31, 3]);
  });

  test("higher is better flips the direction", () => {
    const slower = compareMetric({ key: "k", label: "k", budget: budget({ direction: "higher" }), baselineValues: [30, 31, 29], candidateValues: [45, 46, 44] });
    assert.equal(slower.verdict, "improvement");
    const lower = compareMetric({ key: "k", label: "k", budget: budget({ direction: "higher" }), baselineValues: [45, 46, 44], candidateValues: [30, 31, 29] });
    assert.equal(lower.verdict, "regression");
  });

  test("within the budget, or under the minimum absolute change, is no meaningful change", () => {
    assert.equal(compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [30, 30, 30], candidateValues: [32, 32, 32] }).verdict, "no-meaningful-change");
    // +50% but only 1 ms: under minAbsolute.
    assert.equal(compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [2, 2, 2], candidateValues: [3, 3, 3] }).verdict, "no-meaningful-change");
  });

  test("a change past the budget with overlapping runs needs more evidence", () => {
    const result = compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [30, 50, 29], candidateValues: [45, 46, 28] });
    assert.equal(result.verdict, "insufficient-evidence");
    assert.equal(result.overBudget, true);
    assert.match(result.reasons.join(), /overlap/);
  });

  test("one run each is never a regression, however large the change", () => {
    const result = compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [30], candidateValues: [90] });
    assert.equal(result.verdict, "insufficient-evidence");
    assert.equal(result.overBudget, true);
    assert.equal(result.deltaPercent, 200);
    assert.match(result.reasons.join(), /1 baseline and 1 candidate run; this budget needs at least 3/);
  });

  test("missing measurements are reported, not filled in", () => {
    const missing = compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [null, null, null], candidateValues: [30, 30, 30] });
    assert.equal(missing.verdict, "insufficient-evidence");
    assert.equal(missing.delta, null);
    assert.match(missing.reasons[0], /not measured in the baseline/);
    const ignored = compareMetric({ key: "k", label: "k", budget: budget(), missingData: "ignore", baselineValues: [30, 30, 30], candidateValues: [null, undefined, Number.NaN] });
    assert.equal(ignored.verdict, "not-measured");
    // Partly missing: only the measured runs count, so this is short of evidence.
    assert.equal(compareMetric({ key: "k", label: "k", budget: budget(), baselineValues: [30, null, 31], candidateValues: [45, 46, 44] }).verdict, "insufficient-evidence");
  });
});

describe("budgets", () => {
  test("the repository budgets are valid", async () => {
    const root = await tempDir();
    const { budgets, source } = await loadBudgets(root);
    assert.deepEqual(budgets.problems, []);
    assert.match(source, /scripts\/observatory\/budgets\.json$/);
    assert.deepEqual(Object.keys(budgets.categories).sort(), ["build:clean", "build:incremental", "build:no-change", "ui:ai-pipeline", "ui:ai-provider", "ui:ai-turn", "ui:chat", "ui:sidebar", "ui:startup", "ui:workspace"]);
    assert.ok(budgets.categories["ui:sidebar"].compatibility.includes("scenarioVersion"));
    for (const id of ["ui:ai-pipeline", "ui:ai-turn", "ui:ai-provider"]) {
      assert.ok(budgets.categories[id].compatibility.includes("aiProvider"), `${id}: a mocked and a real provider are never compared`);
    }
  });

  test("a local budgets file overrides the repository's", async () => {
    const root = await tempDir();
    await writeFile(path.join(root, "budgets.json"), JSON.stringify({ schemaVersion: 1, categories: { "ui:sidebar": { minSamples: 1, metrics: { "settled.median": { direction: "lower", regressionPercent: 5 } } } } }));
    const { budgets, source } = await loadBudgets(root);
    assert.equal(source, path.join(root, "budgets.json"));
    assert.equal(budgets.categories["ui:sidebar"].metrics["settled.median"].minSamples, 1);
  });

  test("mistakes are listed and those entries left out", () => {
    const resolved = resolveBudgets({
      schemaVersion: 1,
      categories: {
        "build:weird": { metrics: {} },
        "ui:sidebar": {
          compatibility: ["platform", "colour"],
          metrics: {
            "settled.median": { direction: "sideways", regressionPercent: 10 },
            "settled.p95": { direction: "lower", regressionPercent: -1 },
            nonsense: { direction: "lower", regressionPercent: 10 },
            "state.median": { direction: "lower", regressionPercent: 10 },
          },
        },
      },
    });
    assert.equal(resolved.problems.length, 5);
    assert.deepEqual(Object.keys(resolved.categories), ["ui:sidebar"]);
    assert.deepEqual(Object.keys(resolved.categories["ui:sidebar"].metrics), ["state.median"]);
    assert.deepEqual(resolveBudgets({ schemaVersion: 9 }).categories, {});
    assert.match(resolveBudgets(null).problems[0], /not a JSON object/);
  });
});

describe("comparing groups of runs", () => {
  async function report(root, options) {
    return analyze(root, options);
  }

  test("newest compatible runs since the baseline are compared, failures kept apart", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const result = await report(root);
    const category = result.categories.find((entry) => entry.category === "ui:sidebar");
    assert.equal(category.status, "compared");
    assert.deepEqual([...category.candidate.runIds].sort(), [...SLOWER, FAILED].sort());
    assert.deepEqual(category.skipped.map((skip) => skip.runId), [OTHER]);
    assert.match(category.skipped[0].reason, /incompatible: .*cpuModel/);
    assert.deepEqual(category.functional.failedRuns.map((failure) => failure.runId), [FAILED]);
    const median = category.metrics.find((metric) => metric.key === "settled.median");
    assert.equal(median.verdict, "regression");
    assert.deepEqual(median.candidate.values.sort(), [44, 45, 46]);
    // appDurationMs is the same in every fixture run.
    assert.equal(category.metrics.find((metric) => metric.key === "appDurationMs").verdict, "no-meaningful-change");
    assert.ok(category.steps.length >= 2);
    assert.equal(category.verdict, "regression");
    assert.equal(result.summary.functionalFailures, 1);
    assert.ok(result.summary.regressions >= 1);
  });

  test("categories without a baseline say so, and clean and incremental builds stay apart", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, [CLEAN]);
    const result = await report(root);
    const clean = result.categories.find((entry) => entry.category === "build:clean");
    assert.equal(clean.candidate.runIds.length, 0, "an incremental build is never a clean-build candidate");
    assert.equal(clean.status, "no-candidates");
    const incremental = result.categories.find((entry) => entry.category === "build:incremental");
    assert.equal(incremental.status, "no-baseline");
    const explicit = await report(root, { candidates: { "build:incremental": [INCREMENTAL] } });
    assert.equal(explicit.categories[0].status, "no-baseline");
  });

  test("named runs from another machine are incompatible, not compared", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const result = await report(root, { candidates: { "ui:sidebar": [OTHER] } });
    assert.equal(result.categories[0].verdict, "incompatible");
    assert.deepEqual(result.categories[0].metrics, []);
    assert.equal(result.summary.incompatibleCategories, 1);
  });

  test("a changed scenario version is incompatible", () => {
    const runs = [sidebar(1, 9, 1, 30), sidebar(2, 9, 2, 30, { version: 2 })].map((summary) => normalizeRun(summary.runId, summary));
    const budgets = resolveBudgets({ schemaVersion: 1, categories: { "ui:sidebar": { minSamples: 1, compatibility: ["platform"], metrics: { "settled.median": { direction: "lower", regressionPercent: 10 } } } } });
    const entry = baselineEntry([runs[0]]);
    const selection = selectCandidates(runs, entry, budgets.categories["ui:sidebar"], 3);
    assert.equal(selection.usable.length, 0);
    assert.match(selection.incompatible[0].problems[0], /scenarioVersion: 1 vs 2/);
  });

  test("a deleted baseline run is reported", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const runs = (await loadRuns(root)).filter((run) => run.runId !== BASE[0]);
    const { data } = await readBaselines(root);
    const result = buildReport({ runs, baselines: data, budgets: (await loadBudgets(root)).budgets });
    const category = result.categories.find((entry) => entry.category === "ui:sidebar");
    assert.match(category.reasons.join(), /can't be read any more/);
    // Two baseline runs left: short of the three the budget needs.
    assert.equal(category.metrics.find((metric) => metric.key === "settled.median").verdict, "insufficient-evidence");
  });

  test("a baseline from older metric definitions is not compared", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const { data } = await readBaselines(root);
    data.baselines["ui:sidebar"].metricDefinitionsVersion = 0;
    const result = buildReport({ runs: await loadRuns(root), baselines: data, budgets: (await loadBudgets(root)).budgets });
    assert.equal(result.categories.find((entry) => entry.category === "ui:sidebar").verdict, "incompatible");
  });
});

describe("reports", () => {
  test("readable and machine-readable reports name the runs and the evidence", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const report = await analyze(root);
    assert.equal(report.schema, REPORT_SCHEMA);
    assert.match(report.method, /not a statistical significance test/);
    const text = formatRegressionReport(report, { reportPath: "/tmp/report.json" });
    assert.match(text, /SIDEBAR STRESS/);
    assert.match(text, /Action latency, median \(settled\) +\+50\.0% +\+15\.0 ms +n=3\/3 +REGRESSION/);
    assert.match(text, new RegExp(BASE[0]));
    assert.match(text, /FUNCTIONAL FAILURE .*failed/);
    assert.match(text, new RegExp(`Skipped ${OTHER}: incompatible`));
    assert.match(text, /NO BASELINE|no baseline/i);
    assert.doesNotMatch(text, /statistically significant/i);
    const saved = await saveReport(root, report, text, "test");
    assert.deepEqual(JSON.parse(await readFile(saved.jsonPath, "utf8")), report);
    assert.match(await readFile(saved.textPath, "utf8"), /ARCHIVIST OBSERVATORY/);
  });

  test("one run each reads as needing more runs, not as a regression", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, [BASE[0]]);
    const report = await analyze(root, { candidates: { "ui:sidebar": [SLOWER[0]] } });
    assert.equal(report.summary.regressions, 0);
    assert.ok(report.summary.unconfirmedOverBudget >= 1);
    const text = formatRegressionReport(report);
    assert.match(text, /NEEDS MORE RUNS \(over budget, unconfirmed\)/);
  });
});

function cli(args, env) {
  return new Promise((resolve) => {
    execFile(process.execPath, [CLI, ...args], { env: { ...process.env, ...env } }, (error, stdout, stderr) => {
      resolve({ code: error ? error.code : 0, stdout, stderr });
    });
  });
}

describe("command line", () => {
  test("baseline set / list / compare and check --no-collect", async () => {
    const root = await fixtureRoot();
    const env = { ARCHIVIST_OBSERVATORY_DIR: root };
    const before = await snapshotRuns(root);

    const set = await cli(["baseline", "set", ...BASE, "--note", "fixture"], env);
    assert.equal(set.code, 0, set.stderr);
    assert.match(set.stdout, /Baseline set\.\nSidebar Stress \(ui:sidebar\)/);
    assert.match((await cli(["baseline", "list"], env)).stdout, /Runs: +20261101T090000/);

    const refused = await cli(["baseline", "set", FAILED], env);
    assert.equal(refused.code, 1);
    assert.match(refused.stderr, /did not succeed/);

    const compareOne = await cli(["baseline", "compare", SLOWER[0], "--json"], env);
    assert.equal(compareOne.code, 0, compareOne.stderr);
    const json = JSON.parse(compareOne.stdout);
    assert.equal(json.categories[0].category, "ui:sidebar");
    assert.equal(json.summary.regressions, 0, "one run is not enough evidence");

    // The failed run counts as a functional failure: exit 1, strict or not.
    assert.equal((await cli(["check", "--no-collect"], env)).code, 1);

    // Without the failed run: a confirmed regression fails only with --strict.
    const clean = await fixtureRoot(Object.fromEntries(Object.entries(scenarioRuns()).filter(([runId]) => runId !== FAILED)));
    const cleanEnv = { ARCHIVIST_OBSERVATORY_DIR: clean };
    await cli(["baseline", "set", ...BASE], cleanEnv);
    const lenient = await cli(["check", "--no-collect"], cleanEnv);
    assert.equal(lenient.code, 0, lenient.stderr);
    assert.match(lenient.stdout, /REGRESSION/);
    assert.match(lenient.stdout, /Report: +.*report\.json/);
    assert.equal((await cli(["check", "--no-collect", "--strict"], cleanEnv)).code, 5);
    const reports = await readdir(path.join(clean, "reports"));
    assert.ok(reports.some((name) => name.includes("-check-")));

    assert.deepEqual(await snapshotRuns(root), before, "no run record changed");
    assert.equal((await cli(["baseline", "frobnicate"], env)).code, 2);
  });
});

describe("dashboard host", () => {
  test("serves baselines and budgets read-only", async () => {
    const root = await fixtureRoot();
    await setBaseline(root, BASE);
    const server = createDashboardServer({ root, port: 0 });
    await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
    const { port } = server.address();
    const get = (method = "GET") => new Promise((resolve, reject) => {
      const req = request({ host: "127.0.0.1", port, path: "/api/baselines", method, headers: { host: `127.0.0.1:${port}` } }, (res) => {
        const chunks = [];
        res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => resolve({ status: res.statusCode, text: Buffer.concat(chunks).toString("utf8") }));
      });
      req.on("error", reject);
      req.end();
    });
    try {
      const response = await get();
      assert.equal(response.status, 200);
      const body = JSON.parse(response.text);
      assert.deepEqual(body.baselines.baselines["ui:sidebar"].runIds, BASE);
      assert.equal(body.budgets.schemaVersion, 1);
      assert.match(body.budgetsSource, /budgets\.json$/);
      assert.equal((await get("POST")).status, 405);
      assert.equal((await get("DELETE")).status, 405);
    } finally {
      await new Promise((resolve) => server.close(resolve));
    }
  });

  test("categories are assigned only to classified builds and UI scenarios", () => {
    const runtime = normalizeRun("20261101T090000-000Z-runtime-000001", { schemaVersion: 1, kind: "runtime-log", status: "success" });
    assert.equal(categoryOf(runtime), null);
    const build = fixtureBuild(id(1, 9, "build", 1), { commit: "a".repeat(40), total: 1, phases: [1, 0, 0, 0], classification: "no-change" });
    assert.equal(categoryOf(normalizeRun(build.runId, build)), "build:no-change");
  });
});
