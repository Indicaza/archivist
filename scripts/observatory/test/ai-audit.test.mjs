// AI audits, acceptance tests from the telemetry brief. Each injects one
// slowdown or failure into a synthetic trace (fixtures/ai-trace.mjs, the
// shape the native runtime test records) and checks the audit names it,
// and only it. The end-to-end tests run the real CLI and job runner with
// the stand-in app; no test reaches a real provider.
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, test } from "node:test";
import { compareAudits, evaluateFindings, PRESETS } from "../dashboard/public/audit.mjs";
import { normalizeRun } from "../dashboard/public/normalize.mjs";
import { CLI_PATH } from "../paths.mjs";
import { analyzeOperations } from "../ui-operations.mjs";
import { summarizeActions } from "../ui-stats.mjs";
import { syntheticAiRun } from "./fixtures/ai-trace.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE_APP = path.join(HERE, "fixtures", "fake-app.mjs");
const budgets = JSON.parse(await readFile(path.join(HERE, "..", "budgets.json"), "utf8"));

function summaryFor(knobs = {}, { scenario = "ai-turn", provider = "mock", turns = 3, scale = 1, dropped = 0 } = {}) {
  const events = syntheticAiRun({ knobs, turns, scale, dropped });
  return {
    kind: "ui-scenario", status: "passed", scenario: { id: scenario, name: scenario, version: 1 },
    app: { driverVersion: 2 }, ai: { provider, behaviour: "script" },
    actions: { failed: 0 }, stats: summarizeActions(events.actions), operations: analyzeOperations(events).summary,
  };
}
const findingsFor = (knobs, options) => evaluateFindings([1, 2, 3].map((n) => ({ runId: `run-${n}`, summary: summaryFor(knobs, options) })));
const rules = (findings) => findings.map((finding) => finding.rule).sort();
const one = (findings, rule) => {
  const found = findings.filter((finding) => finding.rule === rule);
  assert.equal(found.length, 1, `${rule} in ${rules(findings)}`);
  return found[0];
};

describe("AI findings: one injected cause, one finding", () => {
  test("a healthy turn has no findings", () => {
    assert.deepEqual(rules(findingsFor({})), []);
  });

  test("a retrieval delay is found as retrieval, inside preparation", () => {
    const findings = findingsFor({ retrievalMs: 400 });
    assert.deepEqual(rules(findings), ["ai.slow-preparation", "ai.slow-retrieval"]);
    assert.equal(one(findings, "ai.slow-retrieval").severity, "medium");
    assert.match(one(findings, "ai.slow-preparation").summary, /largest stage is retrieval/);
    assert.equal(one(findings, "ai.slow-retrieval").confidence.level, "consistent");
  });

  test("context compilation is reported with its size, and a small context isn't", () => {
    const big = one(findingsFor({ compileMs: 450, sourceMessages: 2000, included: 180, estimatedTokens: 95000 }), "ai.slow-context-compile");
    assert.equal(big.severity, "high");
    assert.match(big.summary, /2000 messages, 180 included, about 95000 tokens/);
    assert.deepEqual(rules(findingsFor({ compileMs: 20, sourceMessages: 12, included: 12 })), []);
  });

  test("a delayed mocked provider is first-token latency, split from Archivist's own time and labelled as injected", () => {
    const finding = one(findingsFor({ firstByteMs: 2400 }), "ai.first-token-latency");
    assert.equal(finding.severity, "medium");
    assert.match(finding.summary, /Archivist preparing the request/);
    assert.match(finding.summary, /injected by the mock provider/);
    const real = one(findingsFor({ firstByteMs: 5000 }, { provider: "real" }), "ai.first-token-latency");
    assert.equal(real.severity, "high");
    assert.match(real.summary, /real provider/);
  });

  test("a slow stream is reported as pauses, not as Archivist's time", () => {
    const findings = findingsFor({ streamMs: 2600, maxGapMs: 1500, longGaps: 2 });
    assert.deepEqual(rules(findings), ["ai.stream-pauses"]);
    assert.match(one(findings, "ai.stream-pauses").summary, /longest pause was 1\.50 s/);
  });

  test("an interrupted stream and a refused request are failed turns; a cancel is not", () => {
    assert.match(one(findingsFor({ outcome: "interrupted" }), "ai.failed-turns").summary, /stream cut off \(remote-closed\)/);
    assert.match(one(findingsFor({ outcome: "failed", failStatus: 400 }), "ai.failed-turns").summary, /HTTP 400/);
    const cancelled = findingsFor({ outcome: "cancelled" });
    assert.ok(!rules(cancelled).includes("ai.failed-turns"), rules(cancelled).join());
  });

  test("a retry is reported with what was refused and how long it waited", () => {
    const short = one(findingsFor({ retry: { status: 503, waitMs: 50 } }), "ai.retries");
    assert.equal(short.severity, "low");
    assert.match(short.evidence[0].text, /HTTP 503/);
    assert.equal(one(findingsFor({ retry: { status: 429, waitMs: 1200 } }), "ai.retries").severity, "medium");
  });

  test("a slow tool is named; many tool rounds are their own finding", () => {
    const slow = one(findingsFor({ tools: [{ tool: "search_library", ms: 700 }] }), "ai.slow-tool");
    assert.match(slow.summary, /search_library tool takes 700 ms/);
    const many = findingsFor({ tools: Array.from({ length: 5 }, (_, i) => ({ tool: `read_file`, ms: 5 + i })) });
    assert.deepEqual(rules(many), ["ai.excess-tool-rounds"]);
    assert.match(one(many, "ai.excess-tool-rounds").summary, /up to 5/);
  });

  test("database queue wait and slow queries are told apart", () => {
    const queued = findingsFor({ db: [{ op: "chat.append-event", kind: "write", queueMs: 90, executeMs: 2 }] });
    assert.deepEqual(rules(queued), ["db.queue-contention"]);
    assert.match(one(queued, "db.queue-contention").summary, /waits 90 ms in the queue for 2 ms of work/);
    const slowQuery = findingsFor({ db: [{ op: "library.scan", kind: "write", queueMs: 1, executeMs: 90 }] });
    assert.ok(!rules(slowQuery).includes("db.queue-contention"));
    assert.deepEqual(rules(slowQuery), ["ui.slow-operation"], "a slow query is a slow db.execute, where the time is");
    const sync = one(findingsFor({ db: [{ op: "ChatStore.messages", kind: "read", executeMs: 30 }] }), "db.ui-thread-sync");
    assert.equal(sync.severity, "medium");
  });

  test("slow presentation is the UI's time after the text arrived", () => {
    const finding = one(findingsFor({ shownAfterDeltaMs: 300, flushMaxMs: 40 }), "ai.slow-presentation");
    assert.equal(finding.severity, "high");
    assert.match(finding.summary, /slowest text flush took 40 ms/);
  });

  test("time no stage explains, missing stages and dropped events are reported, never filled in", () => {
    assert.deepEqual(rules(findingsFor({ unexplainedMs: 120 })), ["ai.unattributed"]);
    const missing = one(findingsFor({ omit: ["ai.retrieval"], presentation: false }), "ai.missing-instrumentation");
    assert.match(missing.summary, /ai\.retrieval/);
    assert.match(missing.summary, /chat presentation/);
    one(findingsFor({}, { dropped: 40 }), "trace.dropped-events");
  });
});

describe("AI comparisons", () => {
  const auditOf = (runId, preset, runIds) => normalizeRun(runId, { schemaVersion: 1, kind: "audit", runId, status: "completed", preset, rulesVersion: 3, children: runIds.map((id) => ({ runId: id })), findings: [] });
  const machine = { platform: "darwin", osVersion: "25.0.0", architecture: "arm64", cpuModel: "Fixture M", logicalCpus: 10, totalMemoryBytes: 1 };
  const aiRun = (id, n, { provider = "mock", scale = 1 } = {}) => normalizeRun(id, { ...summaryFor({}, { scenario: "ai-pipeline", provider, scale }), schemaVersion: 1, runId: id, startedAt: new Date(Date.UTC(2026, 9, 1, 0, n)).toISOString(), environment: machine });

  test("a mocked and a real provider run are never compared", () => {
    const before = [1, 2, 3].map((n) => `2026100${n}T000000-000Z-ui-ai-pipeline-aaaaa${n}`);
    const after = [1, 2, 3].map((n) => `2026100${n}T000001-000Z-ui-ai-pipeline-bbbbb${n}`);
    const runs = [...before.map((id, n) => aiRun(id, n)), ...after.map((id, n) => aiRun(id, n + 10, { provider: "real", scale: 6 }))];
    const result = compareAudits({ audit: auditOf("20261002T000000-000Z-audit-bbbbbb", "ai", after), baseline: auditOf("20261001T000000-000Z-audit-aaaaaa", "ai", before), runs, budgets });
    const pipeline = result.categories.find((category) => category.category === "ui:ai-pipeline");
    assert.equal(pipeline.verdict, "incompatible", JSON.stringify(pipeline));
    assert.match(pipeline.reasons.join(" "), /aiProvider: mock/);
    const same = [...before.map((id, n) => aiRun(id, n)), ...after.map((id, n) => aiRun(id, n + 10, { scale: 6 }))];
    const compared = compareAudits({ audit: auditOf("20261002T000000-000Z-audit-bbbbbb", "ai", after), baseline: auditOf("20261001T000000-000Z-audit-aaaaaa", "ai", before), runs: same, budgets });
    assert.equal(compared.categories.find((category) => category.category === "ui:ai-pipeline").verdict, "regression");
  });

  const normalized = (knobs, options, n) => normalizeRun(`r${n}`, { ...summaryFor(knobs, options), schemaVersion: 1, runId: `r${n}`, startedAt: new Date(Date.UTC(2026, 9, n)).toISOString() });

  test("AI metrics are medians over completed turns; a cancel never reads as fast", () => {
    const run = normalized({}, {}, 1);
    assert.equal(run.ui.ai.provider, "mock");
    assert.equal(run.ui.ai.completed, 3);
    assert.ok(run.ui.ai.retrievalMs > 0 && run.ui.ai.firstVisibleTextMs > run.ui.ai.firstModelDeltaMs);
    const cancelled = normalized({ outcome: "cancelled" }, {}, 2);
    assert.equal(cancelled.ui.ai.cancelled, 3);
    assert.equal(cancelled.ui.ai.turnMs, null);
  });
});

function cli(root, args, env = {}) {
  return new Promise((resolve) => {
    execFile(process.execPath, [CLI_PATH, ...args], {
      env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root, ARCHIVIST_OBSERVATORY_APP: FAKE_APP, FAKE_APP_MODE: "ai", ...env },
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
async function audit(root, preset, env = {}, extra = []) {
  const response = json(await cli(root, ["audit", "run", "--preset", preset, "--wait", "--json", ...extra], env));
  assert.ok(response.ok, JSON.stringify(response.error ?? response));
  return response.result;
}

describe("AI audits end to end", () => {
  test("the full AI turn audit runs offline against the mock, keeps the user's key away, and names an injected delay", async () => {
    const root = await tempDir();
    const result = await audit(root, "ai-turn", { OPENAI_API_KEY: "sk-user-key-must-not-reach-the-app", FAKE_AI_KNOBS: JSON.stringify({ retrievalMs: 400 }) });
    assert.equal(result.job.status, "completed", JSON.stringify(result.job));
    const auditId = result.job.auditRunId;
    const findings = json(await cli(root, ["audit", "findings", auditId, "--json"])).result.findings;
    const retrieval = findings.find((finding) => finding.rule === "ai.slow-retrieval");
    assert.ok(retrieval, findings.map((finding) => finding.rule).join());

    // The selected finding, on its own, for an agent.
    const single = await cli(root, ["audit", "findings", auditId, "--finding", retrieval.id]);
    assert.equal(single.code, 0, single.stderr);
    assert.match(single.stdout, /## \[medium\] Slow Library retrieval/);
    assert.match(single.stdout, /audit trace .* --json/);

    // The concise report has the AI turns table and the AI blind spots.
    const report = await cli(root, ["audit", "report", auditId]);
    assert.match(report.stdout, /## AI turns/);
    assert.match(report.stdout, /\| Full AI turn \(mocked\) \| mock \| (\d+)\/\1 \|/);
    assert.match(report.stdout, /local mock provider/);

    // The trace, by cause, from the same operation the page uses.
    const runId = retrieval.evidence[0].runId;
    assert.match(runId, /-ui-ai-turn-/, "evidence names the Observatory run, not the AI turn's own id");
    assert.equal(retrieval.evidence.length, 3, "one line per run");
    assert.match(retrieval.evidence[0].text, /median 400 ms over 3 turns/);
    const trace = json(await cli(root, ["audit", "trace", runId, "--json"])).result;
    assert.equal(trace.available, true);
    assert.equal(trace.turns.length, 3);
    assert.match(trace.turns[0].summary, /AI turn .*: completed/);
    assert.equal(trace.turns[0].tree.name, "ai.run");
    assert.ok(trace.turns[0].call.children.some((child) => child.name === "ai.queue-wait"));
    assert.ok(trace.turns[0].tree.children.find((child) => child.name === "ai.prepare").children.every((child) => child.thread === "ArchivistAI"));
    const text = await cli(root, ["audit", "trace", runId, "--turn", "1"]);
    assert.match(text.stdout, /first network byte .* first model delta .* first visible text/);
    assert.equal(json(await cli(root, ["audit", "trace", runId, "--turn", "9", "--json"])).error.code, "not-found");

    // The full bundle carries each run's trace.
    const exported = json(await cli(root, ["audit", "export", auditId, "--json"])).result;
    assert.ok(exported.manifest.files.some((file) => /runs\/.*\/ui-operations\.json$/.test(file.path)), JSON.stringify(exported.manifest.files.map((file) => file.path)));

    const runs = await readdir(path.join(root, "runs"));
    const child = [];
    for (const id of runs) {
      const summary = JSON.parse(await readFile(path.join(root, "runs", id, "summary.json"), "utf8"));
      if (summary.kind === "ui-scenario") child.push({ id, summary });
    }
    assert.ok(child.length >= 1);
    for (const { id, summary } of child) {
      assert.equal(summary.ai.provider, "mock");
      assert.equal(summary.ai.calls, "local mock only");
      assert.equal(summary.operations.trace.aiTurns.length, 3);
      const log = await readFile(path.join(root, "runs", id, "app.log"), "utf8").catch(() => "");
      if (log) assert.match(log, /ai-env base=local key=offline/);
      for (const name of await readdir(path.join(root, "runs", id))) {
        const text = await readFile(path.join(root, "runs", id, name), "utf8").catch(() => "");
        assert.ok(!text.includes("sk-user-key"), `${name} must not hold the user's key`);
      }
    }
  });

  test("the provider latency audit never starts without explicit permission, and is in no other preset", async () => {
    for (const preset of Object.values(PRESETS)) {
      if (preset.id !== "provider") assert.ok(!preset.scenarios.includes("ai-provider"), preset.id);
    }
    assert.equal(PRESETS.provider.providerCalls, true);
    const root = await tempDir();
    const refused = json(await cli(root, ["audit", "run", "--preset", "provider", "--json"]));
    assert.equal(refused.ok, false);
    assert.equal(refused.error.code, "invalid-input");
    assert.match(refused.error.message, /allow-provider-calls/);
    const sneaky = json(await cli(root, ["tests", "run", "--scenario", "ai-provider", "--json"]));
    assert.equal(sneaky.ok, false);
    assert.deepEqual(await readdir(path.join(root, "runs")).catch(() => []), [], "nothing ran");
  });

  test("a slowed-down AI pipeline is a regression against the previous audit", async () => {
    const root = await tempDir();
    const base = await audit(root, "ai", {}, ["--repeat", "3"]);
    assert.equal(base.job.status, "completed", JSON.stringify(base.job));
    const slow = await audit(root, "ai", { FAKE_APP_SCALE: "6" }, ["--repeat", "3"]);
    const comparison = json(await cli(root, ["audit", "compare", slow.job.auditRunId, base.job.auditRunId, "--json"])).result;
    assert.equal(comparison.status, "compared", JSON.stringify(comparison.blockers ?? comparison));
    const pipeline = comparison.categories.find((category) => category.category === "ui:ai-pipeline");
    assert.equal(pipeline.verdict, "regression", JSON.stringify(pipeline));
    const retrieval = pipeline.metrics.find((metric) => metric.metric === "ai.retrieval" || metric.key === "ai.retrieval");
    assert.equal(retrieval.verdict, "regression");
  });
});
