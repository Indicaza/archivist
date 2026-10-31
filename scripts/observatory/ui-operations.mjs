// Opens up UI Lab actions into the operations measured inside them. The
// in-app driver tags every LiveEvents span and marker, and every event-loop
// stall, with the action that was running when it began (UiLab.setAction),
// all on one clock (LiveEvents::nowNs). Here they become, per action:
//   - a tree of spans, nested by time containment (a span that lies inside
//     another is shown as its child; spans from other threads can nest this
//     way without one calling the other, so the tree says "inside", not
//     "called by");
//   - self time (a span minus the union of its children), and
//   - unattributed time (the action minus the union of its top-level spans),
// so overlapping spans are never added up as if they were sequential.
export const UI_OPERATIONS_SCHEMA = "archivist.observatory.ui-operations";
export const UI_OPERATIONS_VERSION = 1;
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
  const startupSpans = spans.filter((op) => op.category === "startup");
  const firstFrame = markers.find((op) => op.category === "startup" && op.name === "first-frame");
  const eventLoop = markers.find((op) => op.category === "startup" && op.name === "enter-event-loop");
  const startup = startupSpans.length ? {
    basis: "LiveEvents spans in main(); 0 = main() entered (time before main() is not measured)",
    stages: startupSpans.sort((a, b) => a.startMs - b.startMs).map((op) => ({ name: op.name, startMs: round(op.startMs), durationMs: round(op.endMs - op.startMs), ok: op.ok ?? null })),
    eventLoopMs: eventLoop ? round(eventLoop.startMs) : null,
    firstFrameMs: firstFrame ? round(firstFrame.startMs) : null,
    readyMs: events.readyMs != null ? round(runStart + events.readyMs) : null,
    stallsBeforeReady: stalls.filter((stall) => events.readyMs != null && stall.atMs < runStart + events.readyMs).length,
  } : null;

  const actions = (events.actions ?? []).map((action) => {
    const t0 = runStart + action.startMs;
    const end = action.endMs != null ? runStart + action.endMs : t0 + (action.settledMs ?? action.stateMs ?? action.dispatchMs ?? 0);
    const own = spans.filter((op) => op.action === action.index);
    const tree = spanTree(own, t0);
    const clipped = own.map((op) => [Math.max(op.startMs, t0), Math.min(op.endMs, end)]);
    const covered = unionMs(clipped);
    const own_stalls = stalls.filter((stall) => stall.action === action.index);
    return {
      index: action.index,
      phase: action.phase,
      iteration: action.iteration,
      step: action.step,
      op: action.op,
      label: action.label,
      durationMs: round(end - t0),
      settledMs: action.settledMs ?? null,
      operations: tree,
      markers: markers.filter((op) => op.action === action.index).map((op) => ({ name: `${op.category}.${op.name}`, atMs: round(op.startMs - t0) })),
      coveredMs: round(covered),
      unattributedMs: round(Math.max(0, end - t0 - covered)),
      stalls: own_stalls.map((stall) => ({ atMs: round(stall.atMs - t0), lateMs: round(stall.lateMs) })),
      // Spans that began in this action but ended after it (async work).
      outlived: own.filter((op) => op.endMs > end).map((op) => ({ name: `${op.category}.${op.name}`, endsAfterMs: round(op.endMs - end) })),
    };
  });

  // Per step, across measured iterations: each operation's typical time.
  const perStep = new Map();
  for (const action of actions.filter((a) => a.phase === "measured")) {
    const key = `${action.step}\t${action.label ?? action.op}`;
    if (!perStep.has(key)) perStep.set(key, { step: action.step, label: action.label ?? action.op, runs: 0, ops: new Map(), unattributed: [], stalls: 0 });
    const entry = perStep.get(key);
    entry.runs += 1;
    entry.unattributed.push(action.unattributedMs);
    entry.stalls += action.stalls.length;
    const walk = (nodes) => nodes.forEach((node) => {
      if (!entry.ops.has(node.name)) entry.ops.set(node.name, []);
      entry.ops.get(node.name).push(node.durationMs);
      if (node.children) walk(node.children);
    });
    walk(action.operations);
  }
  const steps = [...perStep.values()].map((entry) => ({
    step: entry.step,
    label: entry.label,
    runs: entry.runs,
    stalls: entry.stalls,
    unattributedMedianMs: round(median(entry.unattributed)),
    operations: [...entry.ops.entries()]
      .map(([name, values]) => ({ name, n: values.length, medianMs: round(median(values)), maxMs: round(Math.max(...values)) }))
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
  };
  return { summary, artifact };
}
