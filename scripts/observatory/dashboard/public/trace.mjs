// Traces: LiveEvents spans with ids, parents and threads, read back as
// trees by cause, and the AI turns and database jobs in them. Pure (no Node
// or browser APIs): the CLI, audits and the page share it.
//
// Rules this module keeps:
//   - A parent's time is never filled in with invented children. Self time
//     is the parent minus the union of its children inside it; the rest of
//     a turn that no span covers is reported as unattributed.
//   - A child may outlive its parent (work it started that ends later); that
//     time is reported as outlived, not clipped silently.
//   - A number the trace doesn't have is null, with the reason in `missing`.
//
// Input events come from the UI Lab (startMs/endMs) or the live recorder
// (atMs/endMs); both carry id, parent and thread from schema 2 of the event
// API. Events without ids (older builds) can't be built into trees.
export const TRACE_SCHEMA = "archivist.observatory.trace";
export const TRACE_SCHEMA_VERSION = 1;

const round = (value) => (value == null || !Number.isFinite(value) ? null : Math.round(value * 100) / 100);

function unionMs(intervals) {
  const sorted = intervals.filter(([s, e]) => e > s).sort((a, b) => a[0] - b[0]);
  let total = 0;
  let current = null;
  for (const [start, end] of sorted) {
    if (!current || start > current[1]) {
      if (current) total += current[1] - current[0];
      current = [start, end];
    } else current[1] = Math.max(current[1], end);
  }
  if (current) total += current[1] - current[0];
  return total;
}

function median(values) {
  const list = values.filter((value) => typeof value === "number" && Number.isFinite(value)).sort((a, b) => a - b);
  if (!list.length) return null;
  const mid = Math.floor(list.length / 2);
  return list.length % 2 ? list[mid] : (list[mid - 1] + list[mid]) / 2;
}

// One shape for both sources.
export function normalizeEvents(events) {
  return (events ?? []).map((event) => {
    const start = event.startMs ?? event.atMs ?? null;
    const end = typeof event.endMs === "number" ? event.endMs : null;
    return {
      id: event.id ?? null,
      parent: event.parent ?? null,
      thread: event.thread ?? null,
      kind: event.kind,
      name: `${event.category}.${event.name}`,
      startMs: start,
      endMs: event.kind === "span" ? end : null,
      ok: event.ok ?? null,
      fields: event.fields ?? {},
      action: event.action ?? null,
    };
  }).filter((event) => typeof event.startMs === "number");
}

export function hasIds(events) {
  return events.some((event) => event.id != null);
}

// Trees by parent id. A node whose parent isn't in the trace is a root.
export function buildTrees(rawEvents) {
  const events = normalizeEvents(rawEvents);
  const nodes = new Map();
  for (const event of events) if (event.kind === "span" && event.id != null) nodes.set(event.id, { ...event, children: [], markers: [] });
  const roots = [];
  for (const event of events) {
    if (event.kind !== "span") {
      const owner = nodes.get(event.parent);
      if (owner) owner.markers.push(event);
      continue;
    }
    if (event.id == null) continue;
    const node = nodes.get(event.id);
    const parent = nodes.get(event.parent);
    (parent ? parent.children : roots).push(node);
  }
  const finish = (node) => {
    node.children.sort((a, b) => a.startMs - b.startMs);
    const end = node.endMs ?? node.startMs;
    node.durationMs = node.endMs != null ? round(node.endMs - node.startMs) : null;
    const inside = node.children.map((child) => [Math.max(child.startMs, node.startMs), Math.min(child.endMs ?? child.startMs, end)]);
    node.childMs = round(unionMs(inside));
    node.selfMs = node.durationMs != null ? round(node.durationMs - node.childMs) : null;
    node.outlivedMs = round(unionMs(node.children.filter((child) => (child.endMs ?? 0) > end).map((child) => [end, child.endMs])));
    node.children.forEach(finish);
    return node;
  };
  roots.sort((a, b) => a.startMs - b.startMs).forEach(finish);
  return { roots, nodes, events };
}

const find = (node, name) => node.children.find((child) => child.name === name) ?? null;
const findAll = (node, name) => node.children.filter((child) => child.name === name);
function descendants(node, name, out = []) {
  for (const child of node.children) {
    if (child.name === name) out.push(child);
    descendants(child, name, out);
  }
  return out;
}
const since = (node, at) => (node && at != null ? round(at - node.startMs) : null);
const firstMarker = (node, name) => node?.markers.find((marker) => marker.name === name) ?? null;

function stage(node, from) {
  if (!node) return null;
  return {
    name: node.name,
    thread: node.thread,
    startMs: since(from, node.startMs),
    durationMs: node.durationMs,
    selfMs: node.selfMs,
    ok: node.ok,
    fields: node.fields,
    markers: node.markers.map((marker) => ({ name: marker.name, atMs: since(from, marker.startMs), fields: marker.fields })),
    children: node.children.map((child) => stage(child, from)),
  };
}

// The stages a fully instrumented turn reports. One missing from a turn
// that should have it is named, so a gap is never mistaken for zero time.
const EXPECTED = ["ai.prepare", "ai.provider-round", "ai.persist"];
const EXPECTED_PREPARE = ["ai.conversation-load", "ai.attachments", "ai.retrieval", "ai.context-compile"];

// Every AI turn (ai.run span) in a trace, with the numbers an audit or a
// person needs. Times are ms from the run's start unless named otherwise.
export function aiTurns(rawEvents) {
  const { nodes, events } = buildTrees(rawEvents);
  const presentation = events.filter((event) => event.name.startsWith("chat.ai-"));
  const turns = [];
  for (const run of [...nodes.values()].filter((node) => node.name === "ai.run").sort((a, b) => a.startMs - b.startMs)) {
    const runId = run.fields.runId ?? null;
    const call = nodes.get(run.parent) ?? null;
    const queue = call ? find(call, "ai.queue-wait") : null;
    const prepare = find(run, "ai.prepare");
    const rounds = findAll(run, "ai.provider-round").map((roundNode, index) => {
      const attempts = findAll(roundNode, "ai.http-attempt").map((attempt) => ({
        attempt: attempt.fields.attempt ?? null,
        startMs: since(run, attempt.startMs),
        durationMs: attempt.durationMs,
        ok: attempt.ok,
        status: attempt.fields.status ?? null,
        error: attempt.fields.error ?? null,
        retry: attempt.fields.retry ?? false,
        cancelled: attempt.fields.cancelled ?? false,
        interrupted: attempt.fields.interrupted ?? false,
        bytesSent: attempt.fields.bytesSent ?? null,
        bytesReceived: attempt.fields.bytesReceived ?? null,
        firstByteMs: since(attempt, firstMarker(attempt, "ai.first-byte")?.startMs),
      }));
      const streamed = [...attempts].reverse().find((attempt) => attempt.ok) ?? null;
      const streamedNode = streamed ? findAll(roundNode, "ai.http-attempt").find((node) => node.fields.attempt === streamed.attempt) : null;
      const firstByte = firstMarker(streamedNode, "ai.first-byte");
      const firstDelta = firstMarker(roundNode, "ai.first-delta");
      const retryWaits = findAll(roundNode, "ai.retry-wait");
      return {
        round: roundNode.fields.round ?? index,
        startMs: since(run, roundNode.startMs),
        durationMs: roundNode.durationMs,
        ok: roundNode.ok,
        endpoint: roundNode.fields.endpoint ?? null,
        serializeMs: round(findAll(roundNode, "ai.request-serialize").reduce((sum, node) => sum + (node.durationMs ?? 0), 0)),
        attempts,
        retries: Math.max(0, attempts.length - 1),
        retryWaitMs: round(retryWaits.reduce((sum, node) => sum + (node.durationMs ?? 0), 0)),
        // From the round's start, so retries and their waits are included.
        firstByteMs: since(roundNode, firstByte?.startMs),
        firstDeltaMs: since(roundNode, firstDelta?.startMs),
        events: roundNode.fields.events ?? null,
        deltas: roundNode.fields.deltas ?? null,
        bytesSent: roundNode.fields.bytesSent ?? null,
        bytesReceived: roundNode.fields.bytesReceived ?? null,
        maxGapMs: roundNode.fields.maxGapMs ?? null,
        longGaps: roundNode.fields.longGaps ?? null,
        toolCalls: roundNode.fields.toolCalls ?? null,
        inputTokens: roundNode.fields.inputTokens ?? null,
        outputTokens: roundNode.fields.outputTokens ?? null,
      };
    });
    const toolRounds = findAll(run, "ai.tool-round");
    const tools = toolRounds.flatMap((toolRound) => findAll(toolRound, "ai.tool").map((tool) => ({
      tool: tool.fields.tool ?? null,
      callId: tool.fields.callId ?? null,
      round: tool.fields.round ?? toolRound.fields.round ?? null,
      startMs: since(run, tool.startMs),
      durationMs: tool.durationMs,
      ok: tool.ok,
      errorCode: tool.fields.errorCode ?? null,
      argumentBytes: tool.fields.argumentBytes ?? null,
      resultBytes: tool.fields.resultBytes ?? null,
      serializeMs: tool.fields.serializeMs ?? null,
    })));
    const persist = findAll(run, "ai.persist");
    const mine = presentation.filter((event) => event.fields.runId === runId);
    const shown = mine.find((event) => event.name === "chat.ai-first-text-shown");
    const received = mine.find((event) => event.name === "chat.ai-first-delta-received");
    const presented = mine.find((event) => event.name === "chat.ai-presentation");
    const firstRoundWithDelta = rounds.find((entry) => entry.firstDeltaMs != null);
    const firstByteNode = rounds.length ? firstMarker(findAll(findAll(run, "ai.provider-round")[0], "ai.http-attempt").find((node) => node.ok) ?? null, "ai.first-byte") : null;
    const deltaNode = findAll(run, "ai.provider-round").map((node) => firstMarker(node, "ai.first-delta")).find(Boolean) ?? null;

    const missing = [];
    for (const name of EXPECTED) if (!find(run, name) && !(name === "ai.provider-round" && toolRounds.length === 0 && run.fields.rounds === 0)) missing.push(name);
    if (prepare) for (const name of EXPECTED_PREPARE) if (!find(prepare, name)) missing.push(name);
    if (!call) missing.push("ai.start-chat-run (queue wait unknown)");
    if (!mine.length) missing.push("chat presentation (no ChatStore in this trace, so first visible text is unknown)");

    turns.push({
      runId,
      action: run.action ?? null,
      thread: run.thread,
      provider: run.fields.provider ?? null,
      model: run.fields.model ?? null,
      outcome: run.fields.outcome ?? (run.endMs == null ? "unfinished" : null),
      ok: run.ok,
      startMs: run.startMs,
      durationMs: run.durationMs,
      queueWaitMs: queue?.durationMs ?? null,
      callbackDelayMs: call ? find(call, "ai.callback-delay")?.durationMs ?? null : null,
      prepare: prepare ? {
        durationMs: prepare.durationMs,
        selfMs: prepare.selfMs,
        estimatedTokens: prepare.fields.estimatedTokens ?? null,
        stages: prepare.children.map((child) => stage(child, run)),
      } : null,
      modelCatalogMs: find(run, "ai.model-catalog")?.durationMs ?? null,
      rounds,
      toolRounds: toolRounds.length,
      tools,
      persistMs: persist.length ? round(persist.reduce((sum, node) => sum + (node.durationMs ?? 0), 0)) : null,
      eventWrites: run.fields.eventWrites ?? null,
      eventWriteMs: run.fields.eventWriteMs ?? null,
      eventWriteMaxMs: run.fields.eventWriteMaxMs ?? null,
      // Three different firsts, from the run's start.
      firstNetworkByteMs: since(run, firstByteNode?.startMs),
      firstModelDeltaMs: since(run, deltaNode?.startMs),
      firstDeltaReceivedMs: since(run, received?.startMs),
      firstVisibleTextMs: since(run, shown?.startMs),
      firstRoundWithText: firstRoundWithDelta?.round ?? null,
      presentation: presented ? {
        durationMs: round(presented.endMs - presented.startMs),
        deltas: presented.fields.deltas ?? null,
        flushes: presented.fields.flushes ?? null,
        flushMs: presented.fields.flushMs ?? null,
        flushMaxMs: presented.fields.flushMaxMs ?? null,
      } : null,
      tokens: {
        input: run.fields.inputTokens ?? null,
        output: run.fields.outputTokens ?? null,
        basis: run.fields.inputTokens != null ? "as the provider reported them" : "the provider reported no usage",
      },
      // The run's time that none of its direct children covers.
      unattributedMs: run.selfMs,
      outlivedMs: run.outlivedMs,
      missing,
      tree: stage(run, run),
      // The call that queued the run (its queue wait, and the reply's
      // delivery back on the caller's thread), from the run's start.
      call: call ? stage(call, run) : null,
      // What the chat view did, on the UI thread.
      presentationMarks: mine.filter((event) => event.kind !== "span").map((event) => ({ name: event.name, thread: event.thread, atMs: since(run, event.startMs) })),
      presentationSpan: presented ? { name: presented.name, thread: presented.thread, startMs: since(run, presented.startMs), durationMs: round(presented.endMs - presented.startMs) } : null,
    });
  }
  return turns;
}

// Database jobs (db.read / db.write / db.run), per operation.
export function dbJobs(rawEvents) {
  const { nodes } = buildTrees(rawEvents);
  const groups = new Map();
  for (const job of nodes.values()) {
    if (!["db.read", "db.write", "db.run"].includes(job.name)) continue;
    const op = job.fields.op ?? "unnamed";
    const key = `${job.name}\t${op}`;
    if (!groups.has(key)) groups.set(key, { kind: job.name.slice(3), op, n: 0, failed: 0, rows: [], queue: [], execute: [], callback: [], uiThread: 0, uiThreadMs: 0, uiThreadMaxMs: 0 });
    const entry = groups.get(key);
    entry.n += 1;
    if (job.ok === false) entry.failed += 1;
    if (job.fields.rows != null) entry.rows.push(job.fields.rows);
    const queue = find(job, "db.queue-wait");
    const execute = find(job, "db.execute");
    const callback = find(job, "db.callback-delay");
    if (queue) entry.queue.push(queue.durationMs);
    if (execute) entry.execute.push(execute.durationMs);
    if (callback) entry.callback.push(callback.durationMs);
    if (execute?.thread === "main") {
      entry.uiThread += 1;
      entry.uiThreadMs += execute.durationMs ?? 0;
      entry.uiThreadMaxMs = Math.max(entry.uiThreadMaxMs, execute.durationMs ?? 0);
    }
  }
  const stats = (values) => (values.length ? { medianMs: round(median(values)), maxMs: round(Math.max(...values)), totalMs: round(values.reduce((a, b) => a + b, 0)) } : null);
  return [...groups.values()].map((entry) => ({
    kind: entry.kind,
    op: entry.op,
    n: entry.n,
    failed: entry.failed,
    rows: entry.rows.length ? { median: median(entry.rows), max: Math.max(...entry.rows) } : null,
    queueWait: stats(entry.queue),
    execute: stats(entry.execute),
    callbackDelay: stats(entry.callback),
    // CoreRuntime::read runs the query on the calling thread.
    uiThreadSync: entry.uiThread ? { n: entry.uiThread, totalMs: round(entry.uiThreadMs), maxMs: round(entry.uiThreadMaxMs) } : null,
  })).sort((a, b) => (b.execute?.totalMs ?? 0) + (b.queueWait?.totalMs ?? 0) - (a.execute?.totalMs ?? 0) - (a.queueWait?.totalMs ?? 0));
}

// Everything the trace says, for an artifact: AI turns, database jobs, and
// how much of the trace could be built into trees.
export function analyzeTrace(rawEvents, { dropped = 0 } = {}) {
  const events = normalizeEvents(rawEvents);
  if (!hasIds(events)) {
    return { schema: TRACE_SCHEMA, schemaVersion: TRACE_SCHEMA_VERSION, available: false, reason: "this build's events carry no span ids (before trace schema 1)" };
  }
  const turns = aiTurns(rawEvents);
  return {
    schema: TRACE_SCHEMA,
    schemaVersion: TRACE_SCHEMA_VERSION,
    available: true,
    events: events.length,
    spans: events.filter((event) => event.kind === "span").length,
    threads: [...new Set(events.map((event) => event.thread).filter(Boolean))].sort(),
    dropped,
    ai: { turns },
    db: { jobs: dbJobs(rawEvents) },
  };
}

// A short summary of one turn, for reports and the CLI.
export function turnSummary(turn) {
  const ms = (value) => (value == null ? "unknown" : `${Math.round(value)} ms`);
  const lines = [
    `AI turn ${turn.runId ?? "(no run id)"}: ${turn.outcome ?? "?"} in ${ms(turn.durationMs)} (${turn.provider ?? "?"} / ${turn.model ?? "?"})`,
    `  queue ${ms(turn.queueWaitMs)} · prepare ${ms(turn.prepare?.durationMs)} · ${turn.rounds.length} provider round(s) · ${turn.tools.length} tool call(s) · persist ${ms(turn.persistMs)}`,
    `  first network byte ${ms(turn.firstNetworkByteMs)} · first model delta ${ms(turn.firstModelDeltaMs)} · first visible text ${ms(turn.firstVisibleTextMs)}`,
  ];
  for (const entry of turn.rounds) {
    lines.push(`  round ${entry.round}: ${ms(entry.durationMs)}, ${entry.attempts.length} attempt(s)${entry.retries ? ` (${entry.retries} retried, waited ${ms(entry.retryWaitMs)})` : ""}, first byte ${ms(entry.firstByteMs)}, ${entry.events ?? "?"} events, longest gap ${ms(entry.maxGapMs)}`);
  }
  if (turn.missing.length) lines.push(`  not measured: ${turn.missing.join("; ")}`);
  return lines.join("\n");
}
