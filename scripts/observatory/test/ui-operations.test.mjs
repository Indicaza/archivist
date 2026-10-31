// The boundaries of unattributed time: it is measured against the response
// window, never exceeds it, and span time outside the window is reported,
// not clipped away silently.
import { test } from "node:test";
import assert from "node:assert/strict";
import { analyzeOperations, attribute, responseWindow, actionTimings, startupPhases } from "../ui-operations.mjs";

const span = (name, startMs, endMs, action, category = "chat") => ({ kind: "span", category, name, startMs, endMs, ok: true, action });
const action = (index, fields) => ({ index, phase: "measured", iteration: 0, step: index, op: "click", label: `s${index}`, ok: true, startMs: index * 1000, dispatchMs: 1, stateMs: 5, ...fields });
const run = (actions, operations, extra = {}) => analyzeOperations({ clock: { runStartMs: 0 }, actions, operations: { operations, stalls: [], open: [], dropped: 0 }, ...extra });

test("unattributed time never exceeds its window, for random actions and spans", () => {
  let seed = 7;
  const random = () => ((seed = (seed * 1103515245 + 12345) % 2 ** 31) / 2 ** 31);
  for (let trial = 0; trial < 300; trial += 1) {
    const settled = random() < 0.2 ? null : 10 + random() * 400;
    const elapsed = (settled ?? 50) + random() * 300;
    const a = action(0, { settledMs: settled, endMs: elapsed, frames: { lastMs: settled }, settleTimedOut: random() < 0.1, ok: random() < 0.95 });
    const spans = Array.from({ length: Math.floor(random() * 6) }, (_, i) => {
      const start = -20 + random() * (elapsed + 100);
      return span(`op${i}`, start, start + random() * 500, 0);
    });
    const [result] = run([a], spans).artifact.actions;
    assert.ok(result.unattributedMs >= 0, `trial ${trial}: negative`);
    assert.ok(result.unattributedMs <= result.window.ms + 0.011, `trial ${trial}: ${result.unattributedMs} > ${result.window.ms}`);
    assert.ok(Math.abs(result.coveredMs + result.unattributedMs - result.window.ms) < 0.02, `trial ${trial}: covered + unattributed = window`);
    assert.ok(result.window.ms <= result.durationMs + 0.011, `trial ${trial}: the window lies inside the elapsed time`);
  }
});

test("the window is settled, then state, then elapsed for failed or never-quiet actions", () => {
  assert.deepEqual(responseWindow({ ok: true, settledMs: 80, stateMs: 5, startMs: 0, endMs: 200 }), { basis: "settled", ms: 80 });
  assert.deepEqual(responseWindow({ ok: true, settledMs: null, stateMs: 5, startMs: 0, endMs: 200 }), { basis: "state", ms: 5 });
  assert.deepEqual(responseWindow({ ok: false, settledMs: null, stateMs: null, startMs: 0, endMs: 5000 }), { basis: "elapsed", ms: 5000 });
  assert.deepEqual(responseWindow({ ok: true, settleTimedOut: true, settledMs: null, stateMs: 3, startMs: 0, endMs: 5000 }), { basis: "elapsed", ms: 5000 });
  const timings = actionTimings({ startMs: 100, endMs: 500, dispatchMs: 1, stateMs: 20, settledMs: 180, frames: { lastMs: 180 } });
  assert.deepEqual(timings, { elapsedMs: 400, dispatchMs: 1, stateMs: 20, lastFrameMs: 180, settledMs: 180, renderSettleMs: 160, quietWaitMs: 220 });
});

test("span time after the window is reported, overlaps count once", () => {
  const parts = attribute([span("a", 0, 60), span("b", 40, 150), span("c", 120, 300)], 0, 100);
  assert.equal(parts.covered, 100);
  assert.equal(parts.after, 200, "100..300, the union of b and c past the window");
  assert.equal(parts.before, 0);
  const [result] = run([action(0, { startMs: 0, settledMs: 100, endMs: 400 })], [span("a", 0, 60, 0), span("b", 40, 150, 0)]).artifact.actions;
  assert.equal(result.coveredMs, 100);
  assert.equal(result.unattributedMs, 0);
  assert.equal(result.afterWindowMs, 50);
  assert.deepEqual(result.outlived, [{ name: "chat.b", endsAfterMs: 50 }]);
});

test("expected transition metadata survives attribution and step aggregation", () => {
  const result = run([action(0, { expectedTransition: true, startMs: 0, settledMs: 620, endMs: 750, frames: { lastMs: 620 } })], []);
  assert.equal(result.artifact.actions[0].expectedTransition, true);
  assert.equal(result.artifact.actions[0].unattributedMs, 620, "the raw duration is retained");
  assert.equal(result.summary.steps[0].expectedTransition, true);
});

test("the startup idle step is an observation and startup splits into phases", () => {
  const idle = { index: 0, phase: "measured", iteration: 0, step: 0, op: "noop", label: "idle after ready", ok: true, startMs: 0, endMs: 2310, dispatchMs: 0, stateMs: 0, settledMs: 2010, frames: { lastMs: 2010 } };
  const operations = [
    span("qt-init", 0.5, 120, -1, "startup"),
    span("qml-load", 600, 1300, -1, "startup"),
    { kind: "marker", category: "startup", name: "qml-root-created", startMs: 1250, endMs: null, action: -1 },
    { kind: "marker", category: "startup", name: "enter-event-loop", startMs: 1301, endMs: null, action: -1 },
    { kind: "marker", category: "startup", name: "first-frame", startMs: 1700, endMs: null, action: -1 },
    span("load-chats", 1820, 1900, 0, "chat"),
  ];
  const { summary, artifact } = analyzeOperations({ clock: { runStartMs: 1800 }, readyMs: 0, actions: [idle], operations: { operations, stalls: [], open: [], dropped: 0 } });
  assert.equal(artifact.actions[0].kind, "observation");
  assert.equal(summary.steps[0].kind, "observation");
  const phases = Object.fromEntries(summary.startup.phases.map((phase) => [phase.id, phase.ms]));
  assert.deepEqual(phases, { "process": null, "native-init": 600, "qml-init": 700, "first-frame": 399, "first-usable": 100, "post-ready-work": 80, "render-stabilization": 2010, "idle-observation": 300 });
  assert.deepEqual(summary.startup.qmlLoad, { durationMs: 700, untilRootCreatedMs: 650, afterRootCreatedMs: 50, basis: "qml-root-created marker (QQmlApplicationEngine::objectCreated)" });
  assert.equal(startupPhases({ spans: [], startupSpans: [], actions: [] }).find((phase) => phase.id === "first-frame").ms, null, "no evidence: null, not 0");
});
