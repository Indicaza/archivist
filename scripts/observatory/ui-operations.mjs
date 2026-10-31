// Opens up UI Lab actions into the operations measured inside them. The
// in-app driver tags every LiveEvents span and marker, and every event-loop
// stall, with the action that was running when it began (UiLab.setAction),
// all on one clock (LiveEvents::nowNs). Here they become, per action:
//   - a tree of spans, nested by time containment (a span that lies inside
//     another is shown as its child; spans from other threads can nest this
//     way without one calling the other, so the tree says "inside", not
//     "called by");
//   - self time (a span minus the union of its children), and
//   - unattributed time: the response window minus the union of the spans
//     inside it, so overlapping spans are never added up as if sequential.
//
// Version 2 changed what "unattributed" is measured against. Each action has
// these moments, all from its start (t0):
//   dispatchMs   the call or input returned
//   stateMs      the expected UI state was first seen
//   lastFrameMs  the last frame swapped before rendering went quiet
//   settledMs    max(stateMs, lastFrameMs): the response is complete
//   elapsedMs    the action's full window, which also holds the quiet wait
//                (settleQuietMs with no frame) the driver needs to know
//                rendering stopped
// The response window is [t0, settled] (state, then elapsed, when there is no
// settled time). coveredMs + unattributedMs = windowMs exactly. Span time
// after the window (background work, the quiet wait) is reported as
// afterWindowMs, never clipped away silently. Version 1 measured against the
// elapsed window and compared with settled times, which overstated it.
//
// Steps whose op is "noop" only watch the app (the idle after startup); they
// are kind "observation", never a user interaction.
import { analyzeTrace } from "./dashboard/public/trace.mjs";

export const UI_OPERATIONS_SCHEMA = "archivist.observatory.ui-operations";
export const UI_OPERATIONS_VERSION = 2;
export const OBSERVATION_OPS = new Set(["noop"]);
const TOP = 15;

const round = (value) => (value == null ? null : Math.round(value * 100) / 100);

// Total length of the union of [start, end] intervals.
export function unionMs(intervals) {
  const sorted = intervals.filter(([s, e]) => e > s).sort((a, b) => a[0] - b[0]);
  let total = 0;
  let current = null;
  for (const [start, end] of sorted) {
    if (!current || start > current[1]) {
      if (current) total += current[1] - current[0];
      current = [start, end];
    } else {
      current[1] = Math.max(current[1], end);
    }
  }
  if (current) total += current[1] - current[0];
  return total;
}

function median(values) {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

// Spans (with an end) -> forest nested by time containment.
export function spanTree(spans, origin = 0) {
  const sorted = [...spans].sort((a, b) => a.startMs - b.startMs || b.endMs - a.endMs);
  const roots = [];
  const stack = [];
  for (const span of sorted) {
    const node = {
      name: `${span.category}.${span.name}`,
      startMs: round(span.startMs - origin),
      durationMs: round(span.endMs - span.startMs),
      ok: span.ok ?? null,
      fields: span.fields ?? null,
      ...(span.thread && span.thread !== "main" ? { thread: span.thread } : {}),
      children: [],
      _start: span.startMs,
      _end: span.endMs,
    };
    while (stack.length && !(span.startMs >= stack.at(-1)._start && span.endMs <= stack.at(-1)._end)) stack.pop();
    (stack.length ? stack.at(-1).children : roots).push(node);
    stack.push(node);
  }
  const finish = (node) => {
    node.selfMs = round(node.durationMs - unionMs(node.children.map((child) => [child._start, child._end])));
    node.children.forEach(finish);
    delete node._start;
    delete node._end;
    if (!node.children.length) delete node.children;
    if (!node.fields) delete node.fields;
    return node;
  };
  return roots.map(finish);
}

// The moments of one action, relative to its start. lastFrameMs comes from
// the frames the driver counted inside the action.
export function actionTimings(action) {
  const elapsed = action.endMs != null && action.startMs != null ? action.endMs - action.startMs : null;
  const lastFrame = action.frames?.lastMs ?? null;
  const settled = action.settledMs ?? null;
  const state = action.stateMs ?? null;
  return {
    elapsedMs: round(elapsed ?? settled ?? state ?? action.dispatchMs ?? 0),
    dispatchMs: round(action.dispatchMs ?? null),
    stateMs: round(state),
    lastFrameMs: round(lastFrame),
    settledMs: round(settled),
    // From the expected state to the last frame: rendering catching up.
    renderSettleMs: settled != null && state != null ? round(Math.max(0, settled - state)) : null,
    // The quiet observation after the response; not part of it.
    quietWaitMs: settled != null && elapsed != null ? round(Math.max(0, elapsed - settled)) : null,
  };
}

export function responseWindow(action, timings = actionTimings(action)) {
  if (action.ok !== false && !action.settleTimedOut) {
    if (timings.settledMs != null) return { basis: "settled", ms: timings.settledMs };
    if (timings.stateMs != null) return { basis: "state", ms: timings.stateMs };
  }
  return { basis: "elapsed", ms: timings.elapsedMs };
}

// Splits span time into the part inside [from, to] (a union, so overlaps
// count once) and the parts before and after it.
export function attribute(spans, from, to) {
  const inside = [];
  const before = [];
  const after = [];
  for (const op of spans) {
    if (op.startMs < from) before.push([op.startMs, Math.min(op.endMs, from)]);
    if (op.endMs > to) after.push([Math.max(op.startMs, to), op.endMs]);
    const start = Math.max(op.startMs, from);
    const end = Math.min(op.endMs, to);
    if (end > start) inside.push([start, end]);
  }
  return { covered: Math.min(unionMs(inside), Math.max(0, to - from)), before: unionMs(before), after: unionMs(after) };
}

// qml-load is one synchronous call (engine.loadFromModule). main() marks
// when the root object was created inside it; what runs after that and
// before the call returns (window show, bindings that finish late) is the
// rest. Work QML defers past the event loop is the startup phase between
// enter-event-loop and first-frame.
function qmlLoadParts(spans, markers) {
  const load = spans.find((op) => op.category === "startup" && op.name === "qml-load");
  if (!load) return null;
  const created = markers.find((op) => op.category === "startup" && op.name === "qml-root-created" && op.startMs >= load.startMs && op.startMs <= load.endMs);
  return {
    durationMs: round(load.endMs - load.startMs),
    untilRootCreatedMs: created ? round(created.startMs - load.startMs) : null,
    afterRootCreatedMs: created ? round(load.endMs - created.startMs) : null,
    basis: created ? "qml-root-created marker (QQmlApplicationEngine::objectCreated)" : "this build doesn't mark the root object, so qml-load is one block",
  };
}

// Startup split into phases that don't overlap, from main() to the end of
// the idle observation. A phase with no evidence has ms null and says why.
export function startupPhases({ spans, startupSpans, eventLoop, firstFrame, readyAt, actions, runStart }) {
  const qml = startupSpans.find((op) => op.name === "qml-load");
  const nativeEnd = qml?.startMs ?? eventLoop?.startMs ?? null;
  const loopAt = eventLoop?.startMs ?? qml?.endMs ?? null;
  const frameAt = firstFrame?.startMs ?? null;
  const idle = actions.find((action) => OBSERVATION_OPS.has(action.op));
  const idleStart = idle ? runStart + idle.startMs : null;
  const idleTimings = idle ? actionTimings(idle) : null;
  const span = (id, label, from, to, basis) => ({ id, label, startMs: from != null ? round(from) : null, ms: from != null && to != null ? round(Math.max(0, to - from)) : null, basis });
  const background = readyAt != null ? spans.filter((op) => op.category !== "startup" && op.startMs >= readyAt) : [];
  return [
    { id: "process", label: "Process start to main()", startMs: null, ms: null, basis: "not measured (the clock starts in main())" },
    span("native-init", "Native initialization (Qt, profile, core, AI, IDE host)", 0, nativeEnd, "main() entered to qml-load start"),
    span("qml-init", "QML load (synchronous)", qml?.startMs ?? null, qml?.endMs ?? null, "startup.qml-load span"),
    span("first-frame", "Event loop to first frame", loopAt, frameAt, "enter-event-loop to first-frame markers"),
    span("first-usable", "First frame to usable UI", frameAt, readyAt, "first-frame marker to the driver's ready check"),
    {
      id: "post-ready-work", label: "Background work after ready", startMs: readyAt != null ? round(readyAt) : null,
      ms: background.length ? round(unionMs(background.map((op) => [op.startMs, op.endMs]))) : (readyAt != null ? 0 : null),
      spans: background.length,
      basis: "union of spans that began after ready; overlaps the phases below, not added to them",
    },
    span("render-stabilization", "Rendering settling after ready", idleStart, idleTimings?.settledMs != null ? idleStart + idleTimings.settledMs : null, "the idle observation's last frame"),
    span("idle-observation", "Intentional idle observation", idleTimings?.settledMs != null ? idleStart + idleTimings.settledMs : idleStart, idle ? runStart + idle.endMs : null, "the driver's quiet wait; not app work"),
  ];
}

// The trace in a few numbers per AI turn and the busiest database jobs; the
// full trees are in the artifact.
export function traceSummary(trace) {
  if (!trace?.available) return { available: false, reason: trace?.reason ?? null };
  return {
    available: true,
    schemaVersion: trace.schemaVersion,
    threads: trace.threads,
    aiTurns: trace.ai.turns.map((turn) => ({
      runId: turn.runId, outcome: turn.outcome, durationMs: turn.durationMs, queueWaitMs: turn.queueWaitMs,
      prepareMs: turn.prepare?.durationMs ?? null, rounds: turn.rounds.length, retries: turn.rounds.reduce((sum, entry) => sum + entry.retries, 0),
      tools: turn.tools.length, persistMs: turn.persistMs, firstNetworkByteMs: turn.firstNetworkByteMs,
      firstModelDeltaMs: turn.firstModelDeltaMs, firstVisibleTextMs: turn.firstVisibleTextMs, unattributedMs: turn.unattributedMs,
      missing: turn.missing,
    })),
    db: trace.db.jobs.slice(0, 10),
  };
}

// events: the driver's ui-events.json (with `operations` and `clock`).
export function analyzeOperations(events) {
  const collected = events?.operations;
  if (!collected || !Array.isArray(collected.operations)) {
    return { summary: { collected: false, reason: "this app build doesn't report operations to the UI Lab" }, artifact: null };
  }
  const runStart = Number(events.clock?.runStartMs ?? 0);
  const operations = collected.operations;
  const spans = operations.filter((op) => op.kind === "span" && typeof op.endMs === "number");
  const markers = operations.filter((op) => op.kind !== "span");
  const stalls = collected.stalls ?? [];

  // Startup: what main() reported before the scenario began.
  const startupSpans = spans.filter((op) => op.category === "startup" && !op.name.includes("."));
  const startupMarker = (name) => markers.find((op) => op.category === "startup" && op.name === name);
  const firstFrame = startupMarker("first-frame");
  const eventLoop = startupMarker("enter-event-loop");
  const readyAt = events.readyMs != null ? runStart + events.readyMs : null;
  const startup = startupSpans.length ? {
    basis: "LiveEvents spans in main(); 0 = main() entered (time before main() is not measured)",
    stages: startupSpans.sort((a, b) => a.startMs - b.startMs).map((op) => ({ name: op.name, startMs: round(op.startMs), durationMs: round(op.endMs - op.startMs), ok: op.ok ?? null })),
    qmlLoad: qmlLoadParts(spans, markers),
    eventLoopMs: eventLoop ? round(eventLoop.startMs) : null,
    firstFrameMs: firstFrame ? round(firstFrame.startMs) : null,
    readyMs: readyAt != null ? round(readyAt) : null,
    stallsBeforeReady: stalls.filter((stall) => readyAt != null && stall.atMs < readyAt).length,
    phases: startupPhases({ spans, startupSpans, eventLoop, firstFrame, readyAt, actions: events.actions ?? [], runStart }),
  } : null;

  const actions = (events.actions ?? []).map((action) => {
    const t0 = runStart + action.startMs;
    const timings = actionTimings(action);
    const window = responseWindow(action, timings);
    const windowEnd = t0 + window.ms;
    const own = spans.filter((op) => op.action === action.index);
    const tree = spanTree(own, t0);
    const parts = attribute(own, t0, windowEnd);
    const own_stalls = stalls.filter((stall) => stall.action === action.index);
    return {
      index: action.index,
      phase: action.phase,
      iteration: action.iteration,
      step: action.step,
      op: action.op,
      label: action.label,
      kind: OBSERVATION_OPS.has(action.op) ? "observation" : "interaction",
      startMs: round(action.startMs),
      durationMs: round(timings.elapsedMs),
      settledMs: action.settledMs ?? null,
      timings,
      window: { basis: window.basis, ms: round(window.ms) },
      operations: tree,
      markers: markers.filter((op) => op.action === action.index).map((op) => ({ name: `${op.category}.${op.name}`, atMs: round(op.startMs - t0) })),
      coveredMs: round(parts.covered),
      unattributedMs: round(window.ms - parts.covered),
      beforeWindowMs: round(parts.before),
      afterWindowMs: round(parts.after),
      stalls: own_stalls.map((stall) => ({ atMs: round(stall.atMs - t0), lateMs: round(stall.lateMs) })),
      // Spans that began in this action but ended after its response window
      // (async work, or work during the quiet wait).
      outlived: own.filter((op) => op.endMs > windowEnd).map((op) => ({ name: `${op.category}.${op.name}`, endsAfterMs: round(op.endMs - windowEnd) })),
    };
  });

  // Per step, across measured iterations: each operation's typical time.
  const perStep = new Map();
  for (const action of actions.filter((a) => a.phase === "measured")) {
    const key = `${action.step}\t${action.label ?? action.op}`;
    if (!perStep.has(key)) perStep.set(key, { step: action.step, label: action.label ?? action.op, kind: action.kind, runs: 0, ops: new Map(), parents: new Map(), unattributed: [], windows: [], bases: new Set(), stalls: 0 });
    const entry = perStep.get(key);
    entry.runs += 1;
    entry.unattributed.push(action.unattributedMs);
    entry.windows.push(action.window.ms);
    entry.bases.add(action.window.basis);
    entry.stalls += action.stalls.length;
    const walk = (nodes, parent) => nodes.forEach((node) => {
      if (!entry.ops.has(node.name)) entry.ops.set(node.name, []);
      entry.ops.get(node.name).push(node.durationMs);
      if (parent && !entry.parents.has(node.name)) entry.parents.set(node.name, parent);
      if (node.children) walk(node.children, node.name);
    });
    walk(action.operations);
  }
  const steps = [...perStep.values()].map((entry) => ({
    step: entry.step,
    label: entry.label,
    kind: entry.kind,
    runs: entry.runs,
    stalls: entry.stalls,
    // Both medians come from the same actions and the same window.
    windowBasis: [...entry.bases].join("+"),
    windowMedianMs: round(median(entry.windows)),
    unattributedMedianMs: round(median(entry.unattributed)),
    operations: [...entry.ops.entries()]
      .map(([name, values]) => ({ name, parent: entry.parents.get(name) ?? null, n: values.length, medianMs: round(median(values)), maxMs: round(Math.max(...values)) }))
      .sort((a, b) => b.medianMs - a.medianMs),
  }));

  const slowest = spans
    .filter((op) => op.category !== "startup")
    .map((op) => ({ name: `${op.category}.${op.name}`, durationMs: round(op.endMs - op.startMs), action: op.action, ok: op.ok ?? null }))
    .sort((a, b) => b.durationMs - a.durationMs)
    .slice(0, TOP);

  const artifact = {
    schema: UI_OPERATIONS_SCHEMA,
    schemaVersion: UI_OPERATIONS_VERSION,
    clock: collected.clock ?? null,
    runStartMs: runStart,
    startup,
    actions,
    between: spans.filter((op) => op.action === -1 && op.category !== "startup").map((op) => ({ name: `${op.category}.${op.name}`, startMs: round(op.startMs - runStart), durationMs: round(op.endMs - op.startMs) })),
    stalls: stalls.map((stall) => ({ atMs: round(stall.atMs - runStart), lateMs: round(stall.lateMs), action: stall.action })),
    open: (collected.open ?? []).map((op) => ({ name: `${op.category}.${op.name}`, startMs: round(op.startMs - runStart), action: op.action })),
    dropped: collected.dropped ?? 0,
    // Trees by cause (span ids and parents), AI turns and database jobs.
    trace: analyzeTrace(operations, { dropped: collected.dropped ?? 0 }),
  };
  const summary = {
    collected: true,
    schema: UI_OPERATIONS_SCHEMA,
    schemaVersion: UI_OPERATIONS_VERSION,
    basis: "LiveEvents spans and markers tagged with the running action; event-loop probe every " + (collected.probeMs ?? 50) + " ms, stall when " + (collected.stallMs ?? 50) + " ms late",
    spans: spans.length,
    markers: markers.length,
    stalls: stalls.length,
    dropped: collected.dropped ?? 0,
    unfinished: (collected.open ?? []).length,
    startup,
    slowest,
    steps,
    trace: traceSummary(artifact.trace),
  };
  return { summary, artifact };
}
