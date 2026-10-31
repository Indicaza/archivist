// Synthetic AI turn traces, in the event shape the in-app driver writes
// (the same shape as ai-turn-trace.json, which the native runtime test
// recorded). Each knob stands for one thing that can go slow or wrong, so an
// acceptance test can inject exactly one and check that the audit names it.
// Defaults are a healthy, fast turn. Times are ms.

export const HEALTHY = {
  queueMs: 0.2,
  conversationLoadMs: 2,
  attachmentsMs: 0.5,
  retrievalMs: 8,
  compileMs: 3,
  sourceMessages: 6,
  included: 6,
  estimatedTokens: 420,
  firstByteMs: 40, // request sent to first network byte
  byteToDeltaMs: 5,
  streamMs: 30,
  maxGapMs: 12,
  longGaps: 0,
  retry: null, // { status, waitMs }
  outcome: "completed", // completed | failed | interrupted | cancelled
  failStatus: 400,
  tools: [], // [{ tool, ms }] one tool round each
  persistMs: 3,
  shownAfterDeltaMs: 8, // first model delta to first text shown in the chat
  flushMaxMs: 2,
  presentation: true,
  unexplainedMs: 0, // a gap inside ai.run that no child span covers
  omit: [], // stage names left out, as from an older or partial build
  db: [], // [{ op, kind, queueMs, executeMs, thread }]
};

export function syntheticTurn(knobs = {}, { startMs = 0, idBase = 1, runId = "00000000-0000-4000-8000-000000000001", action = 0 } = {}) {
  const k = { ...HEALTHY, ...knobs };
  const events = [];
  let id = idBase;
  const span = (category, name, start, duration, parent, fields = {}, { thread = "ArchivistAI", ok = true } = {}) => {
    const spanId = id++;
    if (!k.omit.includes(`${category}.${name}`)) {
      events.push({ kind: "span", id: spanId, parent, thread, category, name, startMs: start, endMs: start + duration, ok, fields, action });
    }
    return spanId;
  };
  const marker = (category, name, at, parent, fields = {}, thread = "ArchivistAI") => {
    events.push({ kind: "marker", parent, thread, category, name, startMs: at, endMs: null, ok: null, fields, action });
  };

  const call = id++;
  const queue = span("ai", "queue-wait", startMs, k.queueMs, call);
  void queue;
  let t = startMs + k.queueMs;
  const runStart = t;
  const runId_ = id++;

  // Preparation.
  const prepareStart = t;
  const prepare = id++;
  span("ai", "conversation-load", t, k.conversationLoadMs, prepare, { messages: k.sourceMessages });
  t += k.conversationLoadMs;
  span("ai", "attachments", t, k.attachmentsMs, prepare, { sources: 0 });
  t += k.attachmentsMs;
  const retrieval = span("ai", "retrieval", t, k.retrievalMs, prepare, { sources: 3, files: 3 });
  const search = span("retrieval", "library-search", t + 0.1, k.retrievalMs * 0.8, retrieval, { rows: 40, candidates: 40 }, { ok: null });
  span("retrieval", "fts-query", t + 0.2, k.retrievalMs * 0.5, search, { rows: 40, limit: 50 });
  span("retrieval", "rank", t + 0.2 + k.retrievalMs * 0.5, k.retrievalMs * 0.25, search, { rows: 40 });
  span("retrieval", "select", t + k.retrievalMs * 0.9, k.retrievalMs * 0.05, retrieval, { included: 3, omitted: 37 });
  t += k.retrievalMs;
  const compile = span("ai", "context-compile", t, k.compileMs, prepare, {
    compiler: "contiguous-history", version: 1, sourceMessages: k.sourceMessages, included: k.included, omitted: k.sourceMessages - k.included, estimatedTokens: k.estimatedTokens,
  });
  const strategy = span("context", "strategy", t + 0.05, k.compileMs * 0.9, compile, { compiler: "contiguous-history", messages: k.sourceMessages }, { ok: null });
  span("context", "assemble", t + k.compileMs * 0.5, k.compileMs * 0.3, strategy, { selected: k.included }, { ok: null });
  t += k.compileMs;
  const prepareMs = t - prepareStart;
  if (!k.omit.includes("ai.prepare")) {
    events.push({ kind: "span", id: prepare, parent: runId_, thread: "ArchivistAI", category: "ai", name: "prepare", startMs: prepareStart, endMs: t, ok: true, fields: { estimatedTokens: k.estimatedTokens }, action });
  }
  t += k.unexplainedMs;

  // Tool rounds: a provider round that asks for a tool, then the tool.
  const roundSpan = (index, { stream, retry, final }) => {
    const roundStart = t;
    const round = id++;
    let at = t;
    let attempts = 0;
    if (retry) {
      attempts += 1;
      const failed = span("ai", "http-attempt", at, 6, round, { attempt: attempts, status: retry.status, error: "http-status", retry: true }, { ok: false });
      marker("ai", "first-byte", at + 5, failed, { status: retry.status });
      at += 6;
      span("ai", "retry-wait", at, retry.waitMs, round, { afterAttempt: attempts, waitMs: retry.waitMs });
      at += retry.waitMs;
    }
    attempts += 1;
    const failing = final && (k.outcome === "failed" || k.outcome === "interrupted" || k.outcome === "cancelled");
    const length = k.firstByteMs + (stream ? k.byteToDeltaMs + k.streamMs : 2);
    const fields = { attempt: attempts, status: k.outcome === "failed" && final ? k.failStatus : 200 };
    if (failing && k.outcome === "failed") fields.error = "http-status";
    if (failing && k.outcome === "interrupted") Object.assign(fields, { error: "remote-closed", interrupted: true });
    if (failing && k.outcome === "cancelled") Object.assign(fields, { cancelled: true });
    const attempt = span("ai", "http-attempt", at, length, round, fields, { ok: !failing });
    marker("ai", "first-byte", at + k.firstByteMs, attempt, { status: fields.status });
    if (stream && k.outcome !== "failed") marker("ai", "first-delta", at + k.firstByteMs + k.byteToDeltaMs, round);
    at += length;
    events.push({
      kind: "span", id: round, parent: runId_, thread: "ArchivistAI", category: "ai", name: "provider-round", startMs: roundStart, endMs: at, ok: !failing, action,
      fields: { round: index, attempts, endpoint: "responses", events: stream ? 12 : 3, deltas: stream ? 10 : 0, maxGapMs: stream ? k.maxGapMs : 3, longGaps: stream ? k.longGaps : 0, toolCalls: stream ? 0 : 1 },
    });
    t = at;
    return { firstDeltaAt: stream ? roundStart + (at - roundStart) - k.streamMs : null };
  };

  let index = 0;
  for (const tool of k.tools) {
    roundSpan(index, { stream: false, retry: index === 0 ? k.retry : null, final: false });
    const toolRound = span("ai", "tool-round", t, tool.ms + 0.2, runId_, { round: index, calls: 1 });
    span("ai", "tool", t + 0.1, tool.ms, toolRound, { tool: tool.tool, callId: `call_${index}`, round: index, argumentBytes: 24, resultBytes: 600 });
    t += tool.ms + 0.2;
    index += 1;
  }
  const answer = roundSpan(index, { stream: true, retry: k.tools.length ? null : k.retry, final: true });
  if (k.outcome === "completed") {
    span("ai", "persist", t, k.persistMs, runId_);
    t += k.persistMs;
  }
  const runEnd = t;
  events.push({
    kind: "span", id: runId_, parent: call, thread: "ArchivistAI", category: "ai", name: "run", startMs: runStart, endMs: runEnd, ok: k.outcome === "completed", action,
    fields: { runId, provider: "openai", model: "gpt-5-mini", outcome: k.outcome, rounds: index + 1, toolRounds: k.tools.length, toolCalls: k.tools.length, inputTokens: 900, outputTokens: 40 },
  });
  events.push({ kind: "span", id: call, parent: null, thread: "ArchivistAI", category: "ai", name: "start-chat-run", startMs, endMs: runStart + 1, ok: true, fields: {}, action });

  // What the chat view did with the reply, on the UI thread.
  if (k.presentation && answer.firstDeltaAt != null && k.outcome !== "failed") {
    const received = answer.firstDeltaAt + 1;
    marker("chat", "ai-first-delta-received", received, null, { runId }, "main");
    marker("chat", "ai-first-text-shown", answer.firstDeltaAt + k.shownAfterDeltaMs, null, { runId }, "main");
    events.push({ kind: "span", id: id++, parent: null, thread: "main", category: "chat", name: "ai-presentation", startMs: received, endMs: runEnd, ok: true, action, fields: { runId, outcome: k.outcome, deltas: 10, flushes: 4, flushMs: k.flushMaxMs * 3, flushMaxMs: k.flushMaxMs } });
  }

  // Database jobs around the turn: queue wait on the writer thread, then
  // execution there, or a synchronous read on the UI thread.
  let dbAt = startMs;
  for (const job of k.db) {
    const thread = job.thread ?? (job.kind === "read" ? "main" : "ArchivistCoreWriter");
    const total = (job.queueMs ?? 0) + job.executeMs + 0.3;
    const jobId = span("db", job.kind ?? "write", dbAt, total, null, { op: job.op, rows: 4 }, { thread: "main", ok: true });
    if (job.queueMs) span("db", "queue-wait", dbAt, job.queueMs, jobId, {}, { thread, ok: null });
    span("db", "execute", dbAt + (job.queueMs ?? 0), job.executeMs, jobId, {}, { thread });
    span("db", "callback-delay", dbAt + (job.queueMs ?? 0) + job.executeMs, 0.3, jobId, {}, { thread: "main", ok: null });
    dbAt += total + 1;
  }
  return { events, endMs: Math.max(runEnd, dbAt), nextId: id };
}

// A whole UI Lab events file: `turns` measured sendChatMessage actions, each
// with one AI turn, plus a setup action. Scale stretches the stages a
// regression test slows down.
export function syntheticAiRun({ knobs = {}, turns = 3, scale = 1, dropped = 0 } = {}) {
  const RUN_START = 1000;
  const scaled = { ...knobs };
  for (const key of ["retrievalMs", "compileMs"]) scaled[key] = (knobs[key] ?? HEALTHY[key]) * scale;
  const operations = [
    { kind: "span", id: 9000, category: "startup", name: "core-runtime", startMs: 100, endMs: 300, ok: true, action: -1, thread: "main" },
    { kind: "marker", category: "startup", name: "first-frame", startMs: 900, endMs: null, ok: null, action: -1, thread: "main" },
  ];
  const actions = [{ index: 0, phase: "setup", iteration: 0, step: 0, op: "seedChats", label: "seed", method: "invoke", ok: true, startMs: 0, endMs: 50, dispatchMs: 0.2, stateMs: 40, settledMs: 45, frames: { count: 3, maxIntervalMs: 16.7 } }];
  let at = 100;
  let idBase = 1;
  for (let turn = 0; turn < turns; turn += 1) {
    const index = turn + 1;
    const { events, endMs, nextId } = syntheticTurn(scaled, { startMs: RUN_START + at, idBase, runId: `00000000-0000-4000-8000-${String(index).padStart(12, "0")}`, action: index });
    operations.push(...events);
    const duration = endMs - (RUN_START + at);
    actions.push({ index, phase: "measured", iteration: turn, step: 1, op: "sendChatMessage", label: "send", method: "invoke", ok: true, startMs: at, endMs: at + duration + 5, dispatchMs: 0.3, stateMs: duration, settledMs: duration + 4, frames: { count: 20, maxIntervalMs: 16.7 } });
    at += duration + 50;
    idBase = nextId;
  }
  return {
    schema: "archivist.ui-lab.events", schemaVersion: 1, driverVersion: 2,
    status: "passed", plannedActions: actions.length, readyMs: 0, error: null,
    clock: { source: "LiveEvents::nowNs", runStartMs: RUN_START },
    actions,
    operations: { clock: "LiveEvents::nowNs", collecting: true, probeMs: 50, stallMs: 50, operations, open: [], stalls: [], dropped },
  };
}
