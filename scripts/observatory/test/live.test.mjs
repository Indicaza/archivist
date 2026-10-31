// Live recordings: the shared analysis (live.mjs), the report, the export, and
// the observatory.live.* operations against a stand-in app recorder
// (fixtures/fake-live-app.mjs). The recording in fixtures/live/ was made by
// the real native recorder; nothing here needs Qt.
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile, stat, writeFile, mkdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, test } from "node:test";
import { accessFile, callLive, readAccess } from "../control/live.mjs";
import { dispatch, ERROR_CODES, exitCodeFor, HTTP_STATUS, OPERATIONS } from "../control/operations.mjs";
import { PAGE_FILES } from "../dashboard/server.mjs";
import { diagnosticReport } from "../dashboard/public/diagnostic.mjs";
import { compareLive, context, liveModel, rangeDetail, spikesWithContext, summarySentence } from "../dashboard/public/live.mjs";
import { kindFromId, normalizeRun } from "../dashboard/public/normalize.mjs";
import { makeRedactor } from "../dashboard/public/redact.mjs";
import { BUILD_DIR, CLI_PATH, REPO_ROOT, SCENARIO_DIR } from "../paths.mjs";
import { startFakeLiveApp } from "./fixtures/fake-live-app.mjs";
import { LIVE_CALM_ID, LIVE_FIXTURE_ID, LIVE_FUTURE_ID, LIVE_PARTIAL_ID, liveFixture, writeLiveFixtures } from "./fixtures/live-runs.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
// Words that would claim a cause. Reports say "occurred during" instead.
const CAUSAL = /\b(caused|causes|because|due to|root cause|led to|resulted in|the reason)\b/i;

function ctxFor(root, extra = {}) {
  return { root, repoRoot: REPO_ROOT, buildDir: BUILD_DIR, scenarioDir: SCENARIO_DIR, cliPath: CLI_PATH, env: process.env, livePollMs: 20, ...extra };
}

function cli(root, args) {
  return new Promise((resolve) => {
    execFile(process.execPath, [CLI_PATH, ...args], { env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root }, timeout: 60000 },
      (error, stdout, stderr) => resolve({ code: error ? error.code : 0, stdout, stderr }));
  });
}

describe("live analysis", () => {
  const fixture = liveFixture();
  const model = liveModel(fixture);

  test("reads windows, frames, probes, samples and events of a real recording", () => {
    assert.equal(model.windows.length, 2);
    assert.equal(model.windows[1].detachedMs < model.durationMs, true, "the second window closed during the recording");
    assert.equal(model.frames[1].length + model.frames[2].length, fixture.summary.live.stats.frames.total);
    assert.ok(model.probes.length > 50);
    assert.ok(model.samples.every((sample) => sample.footprintBytes === null), "footprint is macOS-only and recorded as missing");
    assert.deepEqual(model.events.map((event) => event.atMs), [...model.events.map((event) => event.atMs)].sort((a, b) => a - b));
  });

  test("puts each moment next to what was running, as correlation only", () => {
    const spikes = spikesWithContext(model);
    const stall = spikes.find((spike) => spike.type === "event-loop-stall");
    assert.ok(stall.durationMs > 300);
    assert.ok(stall.observedTogether.some((line) => line.startsWith("Occurred during chat switch conversation")));
    const publish = spikes.find((spike) => spike.type === "frame-gap" && spike.durationMs < 200);
    assert.ok(publish.observedTogether.some((line) => /workspace publish .*longest stage: views/.test(line)));
    for (const spike of spikes) for (const line of spike.observedTogether) assert.doesNotMatch(line, CAUSAL);
    assert.doesNotMatch(summarySentence(model), CAUSAL);
    assert.match(summarySentence(model), /occurred during chat switch conversation/);
  });

  test("a time range gives frames, stalls, resources and operations inside it", () => {
    const detail = rangeDetail(model, 3200, 3800);
    assert.ok(detail.eventLoop.stalls.length === 1);
    assert.ok(detail.windows.every((window) => window.maxIntervalMs > 300));
    assert.deepEqual(detail.events.map((event) => event.name), ["switch-conversation"]);
    assert.equal(context(model, 0, 100).during.length, 0, "the recording's own start marker is not an operation");
  });

  test("an empty or missing recording still produces a model and a sentence", () => {
    const bare = liveModel({ summary: { status: "running", live: {} } });
    assert.deepEqual(bare.spikes, []);
    assert.match(summarySentence(bare), /no frame gap/);
  });

  test("compare refuses different measurement versions and labels itself as not a significance test", () => {
    const newer = liveModel({ ...fixture, summary: { ...fixture.summary, live: { ...fixture.summary.live, measurementVersion: 2 } } });
    const refused = compareLive(model, newer);
    assert.equal(refused.compatible, false);
    assert.deepEqual(refused.metrics, []);
    const same = compareLive(model, model);
    assert.equal(same.compatible, true);
    assert.ok(same.metrics.every((metric) => metric.delta === 0 || metric.delta === null));
    assert.ok(same.notes.some((note) => /significance/.test(note)));
  });

  test("normalize knows live recordings", () => {
    assert.equal(kindFromId(LIVE_FIXTURE_ID), "live-session");
    const run = normalizeRun(LIVE_FIXTURE_ID, fixture.summary);
    assert.equal(run.kind, "live-session");
    assert.equal(run.live.frameGaps, 3);
    assert.equal(run.live.stalls, 1);
  });
});

describe("live report", () => {
  test("Copy for AI has the recording, measurements, moments, correlations, notes and what is missing; notes are redacted", () => {
    const fixture = liveFixture();
    fixture.events.events.push({ kind: "annotation", category: "note", name: "opened /Users/zach/Secret/plan.md", atMs: 4000, endMs: null, durationMs: null, ok: null });
    const report = diagnosticReport({
      runId: LIVE_FIXTURE_ID, summary: fixture.summary, live: { metrics: fixture.metrics, events: fixture.events },
      redact: makeRedactor({ home: "/Users/zach" }),
    });
    const text = report.markdown;
    for (const heading of ["## Recording", "## Result", "## Measurements", "## Worst moments (measured)", "## Observed together (correlation, not a cause)", "## Operations recorded", "## Notes added while recording", "## Missing evidence", "## Reproduce"]) {
      assert.ok(text.includes(heading), `missing ${heading}`);
    }
    assert.ok(!text.includes("/Users/zach"), "the note's home path is redacted");
    assert.match(text, /not GPU time/);
    assert.match(text, /phys_footprint\) is not available/);
    assert.match(text, /live window .* --from <ms> --to <ms>/);
    assert.ok(!text.includes("## Baseline comparison"));
    const prose = text.split("\n").filter((line) => !line.startsWith("npm ") && !line.includes("does not identify causes"));
    for (const line of prose) assert.doesNotMatch(line, CAUSAL, line);
    assert.equal(report.facts.evidence.liveCorrelations, true);
    assert.equal(report.facts.liveMissing, undefined, "folded into missingEvidence");
  });

  test("a recording cut short by a crash says so", async () => {
    const root = await tempDir();
    await writeLiveFixtures(root);
    const response = await dispatch(ctxFor(root), "observatory.reports.generate", { runId: LIVE_PARTIAL_ID });
    assert.ok(response.ok, JSON.stringify(response.error));
    assert.match(response.result.markdown, /PARTIAL/);
    assert.match(response.result.markdown, /metrics\.json is missing/);
  });

  test("the export holds metrics.json and a redacted events.json", async () => {
    const root = await tempDir();
    await writeLiveFixtures(root);
    const events = path.join(root, "runs", LIVE_FIXTURE_ID, "events.json");
    const document = JSON.parse(await readFile(events, "utf8"));
    document.events.push({ kind: "annotation", category: "note", name: `see ${root}/runs`, atMs: 10, endMs: null, durationMs: null, ok: null });
    await writeFile(events, JSON.stringify(document));
    const response = await dispatch(ctxFor(root), "observatory.reports.export", { runId: LIVE_FIXTURE_ID });
    assert.ok(response.ok, JSON.stringify(response.error));
    const names = response.result.manifest.files.map((file) => file.path ?? file.name);
    assert.ok(names.includes("metrics.json"));
    assert.ok(names.includes("events.json"));
    assert.ok(!names.includes("ui-events.json"));
    const exported = await readFile(path.join(response.result.dir, "events.json"), "utf8");
    assert.ok(!exported.includes(root), "the Observatory folder is redacted in notes");
  });
});

describe("live operations", () => {
  test("every live operation is declared with input and side effects, and its errors have codes and statuses", () => {
    const names = Object.keys(OPERATIONS).filter((name) => name.startsWith("observatory.live."));
    assert.deepEqual(names.sort(), ["compare", "get", "list", "mark", "spikes", "start", "status", "stop", "wait", "window"].map((op) => `observatory.live.${op}`));
    for (const name of names) assert.ok(OPERATIONS[name].sideEffects && OPERATIONS[name].cli.includes(" live "));
    for (const code of ["not-recording", "agent-access-off", "app-not-running", "app-unreachable"]) {
      assert.ok(ERROR_CODES[code]);
      assert.ok(HTTP_STATUS[code]);
    }
  });

  test("status says unavailable (not an error) while agent access is off", async () => {
    const root = await tempDir();
    const response = await dispatch(ctxFor(root), "observatory.live.status", {});
    assert.equal(response.ok, true);
    assert.equal(response.result.available, false);
    assert.equal(response.result.reason, "agent-access-off");
    const start = await dispatch(ctxFor(root), "observatory.live.start", {});
    assert.equal(start.error.code, "agent-access-off");
  });

  test("start, mark, stop and wait drive the app's recorder and return the saved recording", async () => {
    const root = await tempDir();
    const app = await startFakeLiveApp(root);
    try {
      const ctx = ctxFor(root);
      const started = await dispatch(ctx, "observatory.live.start", { durationSeconds: 15, label: "open big project" });
      assert.ok(started.ok, JSON.stringify(started.error));
      assert.equal(started.result.recording, true);
      assert.equal((await dispatch(ctx, "observatory.live.start", {})).error.code, "busy", "one recording at a time");
      assert.ok((await dispatch(ctx, "observatory.live.mark", { label: "felt slow" })).ok);
      const waiting = dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 10 });
      await new Promise((resolve) => setTimeout(resolve, 100));
      assert.ok((await dispatch(ctx, "observatory.live.stop", {})).ok);
      const waited = await waiting;
      assert.ok(waited.ok, JSON.stringify(waited.error));
      assert.equal(waited.result.recording.runId, started.result.runId);
      assert.equal(waited.result.recording.stopReason, "agent");
      assert.equal((await dispatch(ctx, "observatory.live.mark", {})).error.code, "not-recording");
      for (const call of app.calls) assert.equal(call.headers["x-observatory-agent-token"], app.token);
      assert.ok(app.calls.every((call) => !call.headers.origin));
    } finally {
      await app.close();
    }
  });

  describe("wait, in every recorder state", () => {
    const withApp = async (options, body) => {
      const root = await tempDir();
      const app = await startFakeLiveApp(root, options);
      try {
        await body(ctxFor(root), app);
      } finally {
        await app.close();
      }
    };

    test("idle with no recording since the app started: says so, waits for nothing", () => withApp({}, async (ctx) => {
      const response = await dispatch(ctx, "observatory.live.wait", {});
      assert.ok(response.ok, JSON.stringify(response.error));
      assert.equal(response.result.waited, false);
      assert.equal(response.result.recording, null);
      assert.match(response.result.message, /No recording/);
    }));

    test("idle after a recording: returns that recording by the run id in lastRun (an object, not a string)", () => withApp({}, async (ctx) => {
      const started = await dispatch(ctx, "observatory.live.start", {});
      await dispatch(ctx, "observatory.live.stop", {});
      await new Promise((resolve) => setTimeout(resolve, 150));
      const status = await dispatch(ctx, "observatory.live.status", {});
      assert.equal(typeof status.result.lastRun, "object");
      const response = await dispatch(ctx, "observatory.live.wait", {});
      assert.ok(response.ok, JSON.stringify(response.error));
      assert.equal(response.result.waited, false);
      assert.equal(response.result.recording.runId, started.result.runId);
    }));

    test("while recording until the time limit stops it", () => withApp({ limitMs: 200 }, async (ctx) => {
      const started = await dispatch(ctx, "observatory.live.start", { durationSeconds: 15 });
      const response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 10 });
      assert.ok(response.ok, JSON.stringify(response.error));
      assert.equal(response.result.waited, true);
      assert.equal(response.result.recording.runId, started.result.runId);
      assert.equal(response.result.recording.stopReason, "time-limit");
    }));

    test("during finalizing: waits for the files, then reads them", () => withApp({ finalizeMs: 400 }, async (ctx, app) => {
      const started = await dispatch(ctx, "observatory.live.start", {});
      await dispatch(ctx, "observatory.live.stop", {});
      assert.equal(app.state, "finalizing");
      const response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 10 });
      assert.ok(response.ok, JSON.stringify(response.error));
      assert.equal(response.result.waited, true);
      assert.equal(response.result.recording.runId, started.result.runId);
    }));

    test("a recording that failed or was interrupted is still returned, marked as such", async () => {
      await withApp({ outcome: "failed" }, async (ctx) => {
        const started = await dispatch(ctx, "observatory.live.start", {});
        await dispatch(ctx, "observatory.live.stop", {});
        const response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 10 });
        assert.ok(response.ok, JSON.stringify(response.error));
        assert.equal(response.result.failed.runId, started.result.runId);
        assert.equal(response.result.recording.status, "failed");
      });
      await withApp({ outcome: "interrupted" }, async (ctx) => {
        await dispatch(ctx, "observatory.live.start", {});
        await dispatch(ctx, "observatory.live.stop", {});
        const response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 10 });
        assert.equal(response.result.recording.status, "interrupted");
      });
    });

    test("runs out with timed-out (exit 124), never a success", () => withApp({}, async (ctx) => {
      await dispatch(ctx, "observatory.live.start", {});
      const response = await dispatch(ctx, "observatory.live.wait", { timeoutSeconds: 1 });
      assert.equal(response.ok, false);
      assert.equal(response.error.code, "timed-out");
      assert.equal(exitCodeFor(response), 124);
      await dispatch(ctx, "observatory.live.stop", {});
    }));
  });

  test("bad input is refused before anything reaches the app", async () => {
    const root = await tempDir();
    const app = await startFakeLiveApp(root);
    try {
      const ctx = ctxFor(root);
      assert.equal((await dispatch(ctx, "observatory.live.start", { durationSeconds: 45 })).error.code, "invalid-input");
      assert.equal((await dispatch(ctx, "observatory.live.start", { durationSeconds: 30, shell: "rm -rf /" })).error.code, "invalid-input");
      assert.equal((await dispatch(ctx, "observatory.live.mark", { label: "line\nbreak" })).error.code, "invalid-input");
      assert.equal((await dispatch(ctx, "observatory.live.mark", { label: "x".repeat(65) })).error.code, "invalid-input");
      assert.equal(app.calls.length, 0);
    } finally {
      await app.close();
    }
  });

  test("the key is only ever sent to 127.0.0.1, and a stale or dead app is reported as such", async () => {
    const root = await tempDir();
    await mkdir(path.join(root, "live"), { recursive: true });
    const write = (value) => writeFile(accessFile(root), JSON.stringify({ schema: "archivist.observatory.live-access", token: "t".repeat(32), pid: process.pid, ...value }));
    let fetched = 0;
    const fetchImpl = async () => { fetched += 1; throw new Error("no"); };
    for (const endpoint of ["http://example.com/observatory/api/live/", "http://127.0.0.1.evil.test:80/observatory/api/live/", "https://127.0.0.1:443/observatory/api/live/", "http://localhost:3000/observatory/api/live/"]) {
      await write({ endpoint });
      await assert.rejects(callLive(root, "status", {}, { fetchImpl }), { code: "unreadable-record" });
    }
    assert.equal(fetched, 0);
    await write({ endpoint: "http://127.0.0.1:9/observatory/api/live/" });
    await assert.rejects(readAccess(root, { alive: () => false }), { code: "app-not-running" });
    await assert.rejects(callLive(root, "status", {}, { fetchImpl }), { code: "app-unreachable" });
    await assert.rejects(callLive(root, "agent-access", { enabled: true }, { fetchImpl }), { code: "unknown-operation" }, "agents can't turn their own access on");
    if (process.platform !== "win32") {
      const app = await startFakeLiveApp(root);
      assert.equal((await stat(app.file)).mode & 0o777, 0o600);
      await app.close();
    }
  });

  test("list, get, spikes, window and compare read saved recordings", async () => {
    const root = await tempDir();
    await writeLiveFixtures(root);
    const ctx = ctxFor(root);
    const list = await dispatch(ctx, "observatory.live.list", {});
    assert.equal(list.result.total, 4);
    const get = await dispatch(ctx, "observatory.live.get", { runId: LIVE_FIXTURE_ID });
    assert.equal(get.result.notes[0].label, "felt slow");
    assert.ok(get.result.worst.length > 0);
    assert.equal((await dispatch(ctx, "observatory.live.spikes", { runId: LIVE_FIXTURE_ID, limit: 2 })).result.spikes.length, 2);
    const window = await dispatch(ctx, "observatory.live.window", { runId: LIVE_FIXTURE_ID, fromMs: 3000, toMs: 4000 });
    assert.equal(window.result.eventLoop.stalls.length, 1);
    assert.equal((await dispatch(ctx, "observatory.live.window", { runId: LIVE_FIXTURE_ID, fromMs: 0, toMs: 90000 })).error.code, "invalid-input");
    assert.equal((await dispatch(ctx, "observatory.live.window", { runId: LIVE_FIXTURE_ID, fromMs: 500, toMs: 100 })).error.code, "invalid-input");
    const compared = await dispatch(ctx, "observatory.live.compare", { baseRunId: LIVE_CALM_ID, runId: LIVE_FIXTURE_ID });
    assert.equal(compared.result.compatible, true);
    assert.ok(compared.result.metrics.find((metric) => metric.metric === "maxEventLoopDelayMs").delta > 300);
    assert.equal((await dispatch(ctx, "observatory.live.compare", { baseRunId: LIVE_FIXTURE_ID, runId: LIVE_FUTURE_ID })).result.compatible, false);
    assert.equal((await dispatch(ctx, "observatory.live.get", { runId: "20261030T000000-000Z-live-000000" })).error.code, "not-found");
    const runs = await dispatch(ctx, "observatory.runs.list", { kind: "live-session" });
    assert.equal(runs.result.total, 4);
    assert.equal(typeof runs.result.runs[0].frameGaps, "number");
  });

  test("the CLI prints live recordings for people and JSON for agents", async () => {
    const root = await tempDir();
    await writeLiveFixtures(root);
    const text = await cli(root, ["live", "get", LIVE_FIXTURE_ID]);
    assert.equal(text.code, 0, text.stderr);
    assert.match(text.stdout, /Occurred during chat switch conversation/);
    const json = await cli(root, ["live", "spikes", LIVE_FIXTURE_ID, "--json"]);
    assert.equal(JSON.parse(json.stdout).operation, "observatory.live.spikes");
    const off = await cli(root, ["live", "start", "--json"]);
    assert.equal(off.code, 1);
    assert.equal(JSON.parse(off.stdout).error.code, "agent-access-off");
  });
});

test("the app serves the same page files as the dashboard server", async () => {
  const source = await readFile(path.join(HERE, "..", "..", "..", "frontend", "src", "Core", "Domains", "Observatory", "observatory_records.cpp"), "utf8");
  const list = source.slice(source.indexOf("pageFiles()"), source.indexOf("return files;"));
  const names = [...list.matchAll(/QStringLiteral\("([^"]+)"\)/g)].map((match) => match[1]);
  assert.deepEqual(names.sort(), Object.keys(PAGE_FILES).sort());
});
