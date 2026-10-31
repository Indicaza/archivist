import assert from "node:assert/strict";
import { mkdir, readFile, readdir, rm, stat, symlink, writeFile } from "node:fs/promises";
import { request } from "node:http";
import path from "node:path";
import { after, before, describe, test } from "node:test";
import { compareRuns, SAME_WITHIN_PCT } from "../dashboard/public/compare.mjs";
import { filterOptions, filterRuns } from "../dashboard/public/filter.mjs";
import { normalizeRun, SUPPORTED_SCHEMA_VERSION } from "../dashboard/public/normalize.mjs";
import { previewText } from "../dashboard/public/preview.mjs";
import { artifactPath, baselineRunIds, listRawRunsForPage, loadRuns, loadRunsById } from "../dashboard/records.mjs";
import { createDashboardServer, PAGE_DIR, PAGE_FILES } from "../dashboard/server.mjs";
import { SCHEMA_VERSION } from "../storage.mjs";
import { dashboardFixtures, writeDashboardFixtures } from "./fixtures/dashboard-runs.mjs";
import { tempDir } from "./helpers.mjs";

const ID = {
  cleanBuild: "20261020T090000-000Z-build-00000a",
  buildA: "20261021T090000-000Z-build-00000b",
  buildB: "20261022T090000-000Z-build-00000c",
  failedBuild: "20261023T090000-000Z-build-00000d",
  otherMachine: "20261024T090000-000Z-build-00000e",
  sidebarA: "20261025T090000-000Z-ui-sidebar-00000f",
  sidebarB: "20261026T090000-000Z-ui-sidebar-000010",
  sidebarV2: "20261026T100000-000Z-ui-sidebar-000011",
  workspace: "20261027T090000-000Z-ui-workspace-000012",
  running: "20261028T090000-000Z-build-000013",
  broken: "20261028T100000-000Z-ui-sidebar-000014",
  future: "20261029T090000-000Z-build-000015",
  runtime: "20261029T100000-000Z-runtime-000016",
};

let root;
let runs;
const byId = (id) => runs.find((run) => run.runId === id);

before(async () => {
  root = await tempDir();
  await writeDashboardFixtures(root);
  runs = await loadRuns(root);
});

describe("loading records", () => {
  test("lists every run, newest first, including ones it can't read", () => {
    assert.equal(runs.length, Object.keys(dashboardFixtures()).length);
    assert.deepEqual(runs.map((run) => run.runId), [...runs.map((run) => run.runId)].sort().reverse());
  });

  test("fixtures are labelled as fixtures", () => {
    for (const run of runs.filter((entry) => entry.readable)) {
      assert.match(run.label, /^fixture: /);
    }
  });

  test("reads build measurements as recorded", () => {
    const run = byId(ID.buildA);
    assert.equal(run.kind, "build");
    assert.equal(run.build.totalMs, 12000);
    assert.deepEqual(run.build.phases, { "ide-bundle": 1000, configure: 1500, "build-plan": 500, "native-build": 9000 });
    assert.equal(run.build.ideCacheReused, true);
    assert.equal(run.build.ninjaWorkSteps, 12);
    assert.equal(run.machine.cpuModel, "Fixture M-series");
    assert.equal(run.config.buildType, "Debug");
    assert.equal(byId(ID.cleanBuild).build.classification, "clean");
  });

  test("a failed build names the phase that failed", () => {
    const run = byId(ID.failedBuild);
    assert.equal(run.status, "failure");
    assert.equal(run.build.failedPhase, "native-build");
    assert.match(run.error, /exited with code 1/);
  });

  test("reads UI measurements, keeping missing percentiles missing", () => {
    const run = byId(ID.sidebarA);
    assert.equal(run.ui.scenarioId, "sidebar");
    assert.equal(run.ui.scenarioVersion, 1);
    assert.equal(run.ui.settled.medianMs, 40);
    assert.equal(run.ui.frameGap.medianMs, 16.7);
    assert.equal(run.ui.steps.length, 2);
    assert.equal(run.ui.steps[1].label, "resize explorer");
    const sparse = normalizeRun(ID.sidebarA, {
      schemaVersion: 1, kind: "ui-scenario", status: "passed", scenario: { id: "sidebar", version: 1 },
      stats: { measured: { settledMs: { n: 1, medianMs: 5, p95Ms: null, maxMs: 5 } } },
    });
    assert.equal(sparse.ui.settled.p95Ms, null);
    assert.equal(sparse.ui.dispatch, null);
    assert.equal(sparse.ui.failedActions, null);
    assert.deepEqual(sparse.ui.steps, []);
  });

  test("a run that never finished is incomplete, not successful", () => {
    const run = byId(ID.running);
    assert.equal(run.status, "incomplete");
    assert.equal(run.build.totalMs, null);
    assert.equal(run.build.phases["native-build"], null);
    assert.ok(run.problems.some((problem) => /never finished/.test(problem)));
  });

  test("an unreadable summary is listed with its kind from the run id", () => {
    const run = byId(ID.broken);
    assert.equal(run.readable, false);
    assert.equal(run.status, "unreadable");
    assert.equal(run.kind, "ui-scenario");
    assert.match(run.problems[0], /unreadable/);
  });

  test("a newer schema is listed but not interpreted", () => {
    const run = byId(ID.future);
    assert.equal(run.supported, false);
    assert.equal(run.build, undefined);
    assert.match(run.problems[0], /schemaVersion 99/);
  });

  test("the dashboard reads the schema version Observatory writes", () => {
    assert.equal(SUPPORTED_SCHEMA_VERSION, SCHEMA_VERSION);
  });

  test("non-object summaries and a missing runs folder are handled", async () => {
    assert.equal(normalizeRun(ID.buildA, [1, 2]).readable, false);
    assert.equal(normalizeRun(ID.buildA, null).status, "unreadable");
    assert.deepEqual(await loadRuns(path.join(root, "does-not-exist")), []);
  });

  test("folders that aren't run ids are ignored", async () => {
    const other = await tempDir();
    await writeDashboardFixtures(other);
    await mkdir(path.join(other, "runs", "not-a-run"));
    await mkdir(path.join(other, "runs", "..hidden"));
    assert.equal((await loadRuns(other)).length, runs.length);
  });

  test("loading never writes to the records", async () => {
    const dir = path.join(root, "runs", ID.buildA);
    const beforeFiles = await readdir(dir);
    const beforeTime = (await stat(path.join(dir, "summary.json"))).mtimeMs;
    await loadRuns(root);
    assert.deepEqual(await readdir(dir), beforeFiles);
    assert.equal((await stat(path.join(dir, "summary.json"))).mtimeMs, beforeTime);
  });
});

describe("long histories", () => {
  const ids = [1, 2, 3, 4, 5].map((n) => `2026100${n}T000000-000Z-build-00000${n}`);
  let dir;
  before(async () => {
    dir = await tempDir();
    for (const id of ids) {
      await mkdir(path.join(dir, "runs", id), { recursive: true });
      await writeFile(path.join(dir, "runs", id, "summary.json"), JSON.stringify({ schemaVersion: 1, kind: "build" }));
    }
    await writeFile(path.join(dir, "baselines.json"), JSON.stringify({ schema: "archivist.observatory.baselines", baselines: { "build.incremental": { runIds: [ids[0], "20261099T000000-000Z-build-00dead"] } } }));
  });

  test("the page gets the newest runs plus the baseline's, and how many were left out (as the app does)", async () => {
    const keep = await baselineRunIds(dir);
    const listing = await listRawRunsForPage(dir, { keep, limit: 2 });
    assert.deepEqual(listing.runs.map((run) => run.runId), [ids[4], ids[3], ids[0]]);
    assert.deepEqual({ total: listing.total, omitted: listing.omitted, limit: listing.limit }, { total: 5, omitted: 2, limit: 2 });
    assert.equal((await listRawRunsForPage(dir, { keep })).omitted, 0);
  });

  test("reading runs by id reads only those, and a missing one comes back unreadable", async () => {
    const runs = await loadRunsById(dir, [ids[2], ids[2], "20261099T000000-000Z-build-00dead"]);
    assert.deepEqual(runs.map((run) => [run.runId, run.readable]), [[ids[2], true], ["20261099T000000-000Z-build-00dead", false]]);
  });
});

describe("filtering", () => {
  test("by kind, status and scenario", () => {
    assert.equal(filterRuns(runs, { kind: "build" }).length, 7);
    assert.deepEqual(filterRuns(runs, { status: "failed" }).map((run) => run.runId), [ID.workspace]);
    assert.deepEqual(filterRuns(runs, { scenario: "workspace" }).map((run) => run.runId), [ID.workspace]);
  });

  test("by branch and commit, case-insensitive and partial", () => {
    assert.equal(filterRuns(runs, { branch: "FIXTURE/main" }).length, runs.filter((run) => run.source?.branch).length);
    assert.equal(filterRuns(runs, { commit: "aaaaaaa" }).length, 3);
  });

  test("by date, with a bare end date covering that whole day", () => {
    const day = filterRuns(runs, { from: "2026-10-26", to: "2026-10-26" });
    assert.deepEqual(day.map((run) => run.runId).sort(), [ID.sidebarB, ID.sidebarV2]);
    assert.equal(filterRuns(runs, { from: "2026-11-01" }).length, 0);
  });

  test("by free text over label and id", () => {
    assert.deepEqual(filterRuns(runs, { q: "interrupted" }).map((run) => run.runId), [ID.running]);
  });

  test("no filters keep everything; options list what exists", () => {
    assert.equal(filterRuns(runs, {}).length, runs.length);
    const options = filterOptions(runs);
    assert.ok(options.kinds.includes("runtime-log"));
    assert.deepEqual([...options.scenarios].sort(), ["sidebar", "workspace"]);
    assert.ok(options.statuses.includes("unreadable"));
  });
});

describe("comparison", () => {
  test("two builds of the same kind give deltas and verdicts", () => {
    const result = compareRuns(byId(ID.buildA), byId(ID.buildB));
    assert.equal(result.comparable, true);
    assert.deepEqual(result.blockers, []);
    const total = result.metrics.find((entry) => entry.key === "totalMs");
    assert.equal(total.delta, -2000);
    assert.equal(total.deltaPct, -16.7);
    assert.equal(total.verdict, "better");
    const ide = result.metrics.find((entry) => entry.key === "phase.ide-bundle");
    assert.equal(ide.verdict, "same");
    assert.equal(ide.delta, 0);
    assert.match(result.note, /noise/);
  });

  test("slower is worse, and small changes are 'same'", () => {
    const slower = compareRuns(byId(ID.buildB), byId(ID.buildA));
    assert.equal(slower.metrics.find((entry) => entry.key === "totalMs").verdict, "worse");
    const a = structuredClone(byId(ID.buildA));
    const b = structuredClone(a);
    b.build.totalMs = a.build.totalMs * (1 + (SAME_WITHIN_PCT - 0.5) / 100);
    assert.equal(compareRuns(a, b).metrics.find((entry) => entry.key === "totalMs").verdict, "same");
  });

  test("missing measurements are reported, not invented", () => {
    const result = compareRuns(byId(ID.buildA), byId(ID.running));
    const native = result.metrics.find((entry) => entry.key === "phase.native-build");
    assert.equal(native.verdict, "missing");
    assert.deepEqual(native.missingIn, ["candidate"]);
    assert.equal(native.delta, null);
    assert.equal(native.deltaPct, null);
    assert.ok(result.totals.missing >= 1);
    assert.ok(result.warnings.some((warning) => warning.code === "status"));
  });

  test("a zero baseline gives an absolute delta but no percentage", () => {
    const a = structuredClone(byId(ID.buildA));
    const b = structuredClone(a);
    a.build.phases["build-plan"] = 0;
    b.build.phases["build-plan"] = 50;
    const plan = compareRuns(a, b).metrics.find((entry) => entry.key === "phase.build-plan");
    assert.equal(plan.delta, 50);
    assert.equal(plan.deltaPct, null);
    assert.equal(plan.verdict, "worse");
  });

  test("different hardware, build kind and failure are warned about", () => {
    const machine = compareRuns(byId(ID.buildB), byId(ID.otherMachine));
    assert.equal(machine.comparable, true);
    assert.ok(machine.warnings.some((warning) => warning.code === "machine"));
    const kinds = compareRuns(byId(ID.cleanBuild), byId(ID.buildA));
    assert.ok(kinds.warnings.some((warning) => warning.code === "classification"));
    assert.ok(kinds.warnings.some((warning) => warning.code === "ide-cache"));
    const failed = compareRuns(byId(ID.buildB), byId(ID.failedBuild));
    assert.ok(failed.warnings.some((warning) => warning.code === "status" && /Candidate/.test(warning.message)));
    const debug = structuredClone(byId(ID.buildB));
    debug.config.buildType = "Release";
    assert.ok(compareRuns(byId(ID.buildB), debug).warnings.some((warning) => warning.code === "build-type"));
  });

  test("two runs of the same UI scenario compare per metric and per step", () => {
    const result = compareRuns(byId(ID.sidebarA), byId(ID.sidebarB));
    assert.equal(result.comparable, true);
    const settled = result.metrics.find((entry) => entry.key === "settled.median");
    assert.equal(settled.delta, -10);
    assert.equal(settled.verdict, "better");
    assert.ok(result.metrics.some((entry) => entry.key === "step.1" && entry.label === "resize explorer"));
    assert.ok(result.metrics.some((entry) => entry.key === "frameGap.median"));
  });

  test("different scenarios or scenario versions can't be compared", () => {
    const scenario = compareRuns(byId(ID.sidebarB), byId(ID.workspace));
    assert.equal(scenario.comparable, false);
    assert.equal(scenario.blockers[0].code, "scenario");
    assert.deepEqual(scenario.metrics, []);
    const version = compareRuns(byId(ID.sidebarB), byId(ID.sidebarV2));
    assert.equal(version.comparable, false);
    assert.equal(version.blockers[0].code, "scenario-version");
  });

  test("different kinds, unreadable runs and newer schemas can't be compared", () => {
    assert.equal(compareRuns(byId(ID.buildA), byId(ID.sidebarA)).blockers[0].code, "kind");
    assert.equal(compareRuns(byId(ID.sidebarA), byId(ID.broken)).blockers[0].code, "unreadable");
    assert.equal(compareRuns(byId(ID.buildA), byId(ID.future)).blockers[0].code, "unreadable");
    assert.equal(compareRuns(byId(ID.runtime), byId(ID.runtime)).comparable, false);
  });
});

describe("artifacts and log previews", () => {
  test("only artifacts the summary lists, inside the run folder", () => {
    const dir = path.join(root, "runs", ID.buildA);
    assert.equal(artifactPath(dir, { artifacts: { log: "build.log" } }, "log"), path.join(dir, "build.log"));
    assert.equal(artifactPath(dir, { artifacts: { log: "../../secret" } }, "log"), null);
    assert.equal(artifactPath(dir, { artifacts: { log: "/etc/passwd" } }, "log"), null);
    assert.equal(artifactPath(dir, { artifacts: {} }, "summary"), null);
    assert.equal(artifactPath(dir, { artifacts: { log: "build.log" } }, "__proto__"), null);
  });

  test("previews are bounded and searchable", () => {
    const text = Array.from({ length: 5000 }, (_, i) => `line ${i + 1}${i % 1000 === 0 ? " error" : ""}`).join("\n");
    const first = previewText(text);
    assert.equal(first.totalLines, 5000);
    assert.equal(first.lines.length, 400);
    assert.equal(first.truncated, true);
    assert.equal(previewText(text, { limit: 999999 }).lines.length, 2000);
    const page = previewText(text, { offset: 4990, limit: 50 });
    assert.equal(page.lines[0].n, 4991);
    assert.equal(page.truncated, false);
    const found = previewText(text, { q: "ERROR" });
    assert.equal(found.matchingLines, 5);
    assert.deepEqual(found.lines.map((line) => line.n), [1, 1001, 2001, 3001, 4001]);
    assert.equal(previewText(text, { offset: "nope", limit: "-3" }).lines.length, 1);
  });
});

describe("local server", () => {
  let server;
  let port;
  let data;
  const realFetch = globalThis.fetch;

  before(async () => {
    server = createDashboardServer({ root, port: 0 });
    await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
    port = server.address().port;
    // The page's data layer, run against this server as the page would.
    globalThis.fetch = (url, options) => realFetch(new URL(url, `http://127.0.0.1:${port}/`), options);
    data = await import("../dashboard/public/data.js");
  });
  after(() => {
    globalThis.fetch = realFetch;
    return new Promise((resolve) => server.close(resolve));
  });

  function get(pathname, { method = "GET", host } = {}) {
    return new Promise((resolve, reject) => {
      const req = request({ host: "127.0.0.1", port, path: pathname, method, headers: { host: host ?? `127.0.0.1:${port}` } }, (res) => {
        const chunks = [];
        res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => {
          const text = Buffer.concat(chunks).toString("utf8");
          let json = null;
          try { json = JSON.parse(text); } catch { /* not JSON */ }
          resolve({ status: res.statusCode, headers: res.headers, text, json });
        });
      });
      req.on("error", reject);
      req.end();
    });
  }

  test("serves the page's files and nothing else from its folder", async () => {
    const page = await get("/");
    assert.equal(page.status, 200);
    assert.match(page.headers["content-security-policy"], /default-src 'self'/);
    assert.equal(page.headers["x-content-type-options"], "nosniff");
    for (const file of Object.keys(PAGE_FILES)) {
      assert.equal((await get(`/${file}`)).status, 200, file);
    }
    assert.deepEqual((await readdir(PAGE_DIR)).sort(), Object.keys(PAGE_FILES).sort(), "every page file is listed");
    assert.equal((await get("/../server.mjs")).status, 404);
    assert.equal((await get("/server.mjs")).status, 404);
    assert.equal((await get("/../../etc/passwd")).status, 404);
  });

  test("lists raw summaries, unreadable ones with their error", async () => {
    const list = await get("/api/runs");
    assert.equal(list.json.root, root);
    assert.equal(list.json.omitted, 0);
    assert.equal(list.json.total, runs.length);
    assert.equal(list.json.runs.length, runs.length);
    const broken = list.json.runs.find((entry) => entry.runId === ID.broken);
    assert.equal(broken.summary, null);
    assert.match(broken.error, /unreadable/);
    assert.equal(list.json.runs.find((entry) => entry.runId === ID.buildA).summary.totalDurationMs, 12000);
  });

  test("answers only requests addressed to localhost", async () => {
    assert.equal((await get("/api/runs", { host: "evil.example" })).status, 403);
    assert.equal((await get("/api/runs", { host: `evil.example:${port}` })).status, 403);
    assert.equal((await get("/api/runs", { host: `localhost:${port}` })).status, 200);
  });

  test("is read-only", async () => {
    for (const method of ["POST", "PUT", "DELETE", "PATCH"]) {
      assert.equal((await get("/api/runs", { method })).status, 405);
    }
  });

  test("serves a run's listed artifacts as text", async () => {
    const raw = await get(`/api/runs/${ID.failedBuild}/artifacts/log`);
    assert.equal(raw.status, 200);
    assert.match(raw.headers["content-type"], /^text\/plain/);
    assert.equal(raw.text, await readFile(path.join(root, "runs", ID.failedBuild, "build.log"), "utf8"));
  });

  test("refuses anything outside the runs' listed files", async () => {
    assert.equal((await get("/api/runs/..%2F..%2Fetc/artifacts/log")).status, 400);
    assert.equal((await get("/api/runs/not-a-run/artifacts/log")).status, 400);
    assert.equal((await get(`/api/runs/${ID.buildA}/artifacts/summary`)).status, 404);
    assert.equal((await get(`/api/runs/${ID.buildA}/artifacts/__proto__`)).status, 404);
    assert.equal((await get(`/api/runs/${ID.buildA}/artifacts/..%2F..%2Fsummary.json`)).status, 404);
    assert.equal((await get(`/api/runs/${ID.broken}/artifacts/log`)).status, 404);
    assert.equal((await get(`/api/runs/${ID.buildA}`)).status, 404);
  });

  test("the page's data layer normalizes, previews and compares", async () => {
    const loaded = await data.loadRuns();
    assert.equal(loaded.root, root);
    assert.deepEqual(loaded.runs, runs);
    assert.ok(loaded.options.kinds.includes("build"));

    const details = data.runDetails(ID.sidebarA);
    assert.equal(details.run.ui.scenarioId, "sidebar");
    assert.deepEqual(details.artifacts.sort(), ["events", "log", "scenario"]);
    assert.equal((await data.runEvents(ID.sidebarA)).status, "passed");
    await assert.rejects(data.runEvents(ID.buildA), /no UI events/);

    const log = await data.artifactPreview(ID.failedBuild, "log", { q: "script" });
    assert.equal(log.file, "build.log");
    assert.equal(log.matchingLines, 1);
    assert.match(log.lines[0].text, /<script>/);
    await assert.rejects(data.artifactPreview(ID.buildA, "summary"), /lists no such artifact/);
    await assert.rejects(data.artifactPreview(ID.running, "log"), /missing/);

    assert.equal(data.runDetails(ID.broken).run.status, "unreadable");
    assert.equal(data.compare(ID.buildA, ID.buildB).comparison.comparable, true);
    assert.equal(data.compare(ID.sidebarB, ID.sidebarV2).comparison.comparable, false);
    assert.equal(data.compare(ID.buildA, "20261231T000000-000Z-build-ffffff").comparison.blockers[0].code, "unreadable");
  });

  test("won't follow a listed artifact that links outside its run", async () => {
    const outside = path.join(root, "outside.txt");
    await writeFile(outside, "private");
    const dir = path.join(root, "runs", ID.runtime);
    await rm(path.join(dir, "runtime.log"));
    await symlink(outside, path.join(dir, "runtime.log"));
    assert.equal((await get(`/api/runs/${ID.runtime}/artifacts/log`)).status, 404);
  });
});
