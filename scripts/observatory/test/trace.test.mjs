// Trace analysis, on a trace the native AI runtime test recorded from a
// complete mocked turn (core_ai_runtime_test, aTracedTurnNestsByCause...,
// exported with ARCHIVIST_TRACE_EXPORT): one refused and retried request,
// one tool round, a streamed answer with provider-reported usage.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { aiTurns, analyzeTrace, buildTrees, dbJobs, turnSummary } from "../dashboard/public/trace.mjs";
import { analyzeOperations } from "../ui-operations.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const fixture = async () => JSON.parse(await readFile(path.join(HERE, "fixtures", "ai-turn-trace.json"), "utf8")).events;

const span = (id, parent, name, startMs, endMs, fields = {}, thread = "ArchivistAI") => {
  const [category, ...rest] = name.split(".");
  return { kind: "span", id, parent, thread, category, name: rest.join("."), startMs, endMs, ok: true, fields };
};

test("a recorded mocked turn reads back with every stage, in order, by cause", async () => {
  const events = await fixture();
  const [turn] = aiTurns(events);
  assert.equal(turn.outcome, "completed");
  assert.equal(turn.provider, "openai");
  assert.match(turn.runId, /^[0-9a-f-]{36}$/);
  assert.ok(turn.queueWaitMs != null && turn.queueWaitMs >= 0);
  assert.deepEqual(turn.prepare.stages.map((stage) => stage.name), ["ai.conversation-load", "ai.attachments", "ai.retrieval", "ai.context-compile"]);
  const retrieval = turn.prepare.stages.find((stage) => stage.name === "ai.retrieval");
  assert.deepEqual(retrieval.children.map((child) => child.name), ["retrieval.library-search", "retrieval.select"]);
  assert.deepEqual(retrieval.children[0].children.map((child) => child.name), ["retrieval.fts-query", "retrieval.rank"]);

  assert.equal(turn.rounds.length, 2);
  const [toolRound, answer] = turn.rounds;
  assert.deepEqual(toolRound.attempts.map((attempt) => [attempt.status, attempt.ok, attempt.retry]), [[503, false, true], [200, true, false]]);
  assert.equal(toolRound.retries, 1);
  assert.ok(toolRound.retryWaitMs >= 4, "the 5 ms retry-after-ms wait");
  assert.equal(answer.deltas, 2);
  assert.equal(answer.inputTokens, 321);
  assert.ok(answer.maxGapMs >= 20, "events were written 30 ms apart");
  assert.ok(answer.firstByteMs <= answer.firstDeltaMs);
  assert.deepEqual(turn.tools.map((tool) => [tool.tool, tool.callId, tool.ok]), [["list_directory", "call_resp_1", true]]);
  assert.ok(turn.tools[0].resultBytes > 0);
  assert.deepEqual(turn.tokens, { input: 321, output: 7, basis: "as the provider reported them" });

  // Three different firsts, never one standing in for another.
  assert.ok(turn.firstNetworkByteMs > 0 && turn.firstModelDeltaMs > turn.firstNetworkByteMs);
  assert.equal(turn.firstVisibleTextMs, null);
  assert.ok(turn.missing.some((item) => item.startsWith("chat presentation")));

  // Self time + time in children = the span, at every level.
  const check = (node) => {
    if (node.durationMs != null) assert.ok(Math.abs(node.selfMs + node.childMs - node.durationMs) < 0.02, node.name);
    node.children.forEach(check);
  };
  buildTrees(events).roots.forEach(check);
  assert.ok(turn.unattributedMs >= 0 && turn.unattributedMs <= turn.durationMs);
  assert.match(turnSummary(turn), /first visible text unknown/);
});

test("the recorded trace carries ids and sizes, never the conversation", async () => {
  const text = JSON.stringify(await fixture());
  for (const word of ["harbour", "green", "notes.txt", "test-key", "Bearer", "lights"]) assert.ok(!text.includes(word), word);
});

test("no child durations are invented: a stage with no spans is missing, not zero", () => {
  const events = [
    span(1, null, "ai.run", 0, 100, { runId: "r", outcome: "completed" }),
    span(2, 1, "ai.prepare", 0, 40),
    span(3, 1, "ai.provider-round", 40, 90, { round: 0 }),
  ];
  const [turn] = aiTurns(events);
  assert.deepEqual(turn.prepare.stages, []);
  assert.equal(turn.prepare.selfMs, 40, "all of prepare is its own, unexplained time");
  assert.deepEqual(turn.missing.slice(0, 5), ["ai.persist", "ai.conversation-load", "ai.attachments", "ai.retrieval", "ai.context-compile"]);
  assert.equal(turn.rounds[0].firstByteMs, null);
  assert.equal(turn.queueWaitMs, null);
  assert.equal(turn.unattributedMs, 10, "90..100 is covered by no child");
});

test("a child that outlives its parent is reported, not clipped away", () => {
  const { roots } = buildTrees([span(1, null, "ai.start-chat-run", 0, 10), span(2, 1, "ai.run", 5, 60)]);
  assert.equal(roots[0].childMs, 5);
  assert.equal(roots[0].outlivedMs, 50);
});

test("database jobs separate queue wait from execution and flag UI-thread reads", () => {
  const events = [
    span(1, null, "db.write", 0, 160, { op: "fast-write", rows: 3 }, "main"),
    span(2, 1, "db.queue-wait", 0, 150, {}, "ArchivistCoreWriter"),
    span(3, 1, "db.execute", 150, 152, {}, "ArchivistCoreWriter"),
    span(4, 1, "db.callback-delay", 152, 160, {}, "main"),
    span(5, null, "db.read", 200, 230, { op: "ChatStore" }, "main"),
    span(6, 5, "db.execute", 200, 228, {}, "main"),
  ];
  const jobs = dbJobs(events);
  const write = jobs.find((job) => job.op === "fast-write");
  assert.equal(write.queueWait.medianMs, 150);
  assert.equal(write.execute.medianMs, 2);
  assert.equal(write.uiThreadSync, null);
  const read = jobs.find((job) => job.op === "ChatStore");
  assert.deepEqual(read.uiThreadSync, { n: 1, totalMs: 28, maxMs: 28 });
});

test("a trace from a build without span ids says so", () => {
  const result = analyzeTrace([{ kind: "span", category: "chat", name: "x", startMs: 0, endMs: 1 }]);
  assert.equal(result.available, false);
});

test("UI Lab runs carry the trace in their operations artifact", async () => {
  const operations = (await fixture()).map((event) => ({ ...event, startMs: event.atMs, action: 0 }));
  const { summary, artifact } = analyzeOperations({
    clock: { runStartMs: 0 },
    actions: [{ index: 0, phase: "measured", iteration: 0, step: 0, op: "sendMessage", label: "send", ok: true, startMs: 0, endMs: 400, dispatchMs: 1, stateMs: 300, settledMs: 320 }],
    operations: { operations, stalls: [], open: [], dropped: 0 },
  });
  assert.equal(artifact.trace.available, true);
  assert.equal(summary.trace.aiTurns.length, 1);
  assert.equal(summary.trace.aiTurns[0].rounds, 2);
  assert.ok(summary.trace.threads.includes("ArchivistAI"));
});
