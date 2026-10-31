#!/usr/bin/env node
// Stands in for the Archivist binary in UI Lab runner tests. FAKE_APP_MODE
// picks the behaviour; it writes ui-events.json like the in-app driver.
import { writeFileSync } from "node:fs";
import { syntheticAiRun } from "./ai-trace.mjs";

const mode = process.env.FAKE_APP_MODE || "pass";
const output = process.env.ARCHIVIST_UI_LAB_OUTPUT;
const action = (index, phase, ok, settledMs) => ({
  index, phase, iteration: 1, step: index % 2, op: "click", label: `step ${index % 2}`, method: "input", ok,
  error: ok ? null : "timed out waiting for panelOpen", timedOut: !ok, startMs: index * 200,
  dispatchMs: 0.4, stateMs: 1.5, settledMs, settleTimedOut: false, frames: { count: 12, firstMs: 3, lastMs: settledMs, maxIntervalMs: 16.7 },
});
const report = (status, actions, error = null) => ({
  schema: "archivist.ui-lab.events", schemaVersion: 1, driverVersion: 1, scenario: { id: "fake", version: 1 },
  status, plannedActions: 4, readyMs: 900, error,
  isolation: { ok: true, profileDir: process.env.ARCHIVIST_PROFILE_DIR, checks: [] }, actions,
});

console.log(`[UiLab] Test profile: ${process.env.ARCHIVIST_PROFILE_DIR}`);
console.log(`env-check db=${process.env.ARCHIVIST_DB_PATH} legacy=${process.env.ARCHIVIST_LEGACY_WORKSPACE ?? "unset"}`);
console.log("archivist.wells.timing: minimize “Lab B” model 0.02 ms, publish 3.5 ms");

// "slow": a run whose app reports operations (LiveEvents) like a current
// build, with one deliberately slow operation in step 1 of every measured
// iteration, and an event-loop stall while it runs.
// FAKE_APP_SCALE stretches the slow operations, to stand in for a slowdown.
const SCALE = Number(process.env.FAKE_APP_SCALE || 1);
const RUN_START = 1500;
const span = (category, name, startMs, durationMs, action, fields) => ({ kind: "span", category, name, startMs: RUN_START + startMs, endMs: RUN_START + startMs + durationMs, ok: true, action, ...(fields ? { fields } : {}) });
const slowOperations = () => {
  const operations = [
    { kind: "span", category: "startup", name: "qt-init", startMs: 0.5, endMs: 120, ok: null, action: -1 },
    { kind: "span", category: "startup", name: "core-runtime", startMs: 130, endMs: 410, ok: true, action: -1, fields: { schema: 42 } },
    { kind: "span", category: "startup", name: "qml-load", startMs: 600, endMs: 1300, ok: null, action: -1 },
    { kind: "marker", category: "startup", name: "enter-event-loop", startMs: 1301, endMs: null, ok: null, action: -1 },
    { kind: "marker", category: "startup", name: "first-frame", startMs: 1380, endMs: null, ok: null, action: -1 },
  ];
  const stalls = [];
  for (let index = 0; index < 4; index += 1) {
    const t0 = index * 400;
    if (index % 2 === 1) {
      operations.push(span("chat", "open-conversation", t0 + 1, 260 * SCALE, index, { messages: 160 }));
      operations.push(span("chat", "load-message-page", t0 + 5, 30, index, { messages: 160 }));
      operations.push(span("chat", "publish-messages", t0 + 40, 210 * SCALE, index, { messages: 160 }));
      stalls.push({ atMs: RUN_START + t0 + 45, lateMs: 190, action: index });
    } else {
      operations.push(span("workspace", "switch-tab", t0 + 1, 4, index));
    }
  }
  return { clock: "LiveEvents::nowNs", collecting: true, probeMs: 50, stallMs: 50, operations, open: [], stalls, dropped: 0 };
};

// "ai": AI turns, as a current build traces them (see ai-trace.mjs).
// FAKE_AI_KNOBS (JSON) injects one slowdown or failure; FAKE_APP_SCALE
// stretches retrieval and compilation. It reports where the runner pointed
// its provider, without ever printing a key.
if (mode === "ai") {
  const base = process.env.OPENAI_BASE_URL ?? "";
  const key = process.env.OPENAI_API_KEY;
  console.log(`ai-env base=${/^http:\/\/127\.0\.0\.1:/.test(base) ? "local" : base ? "remote" : "unset"} key=${key === "observatory-offline-no-key" ? "offline" : key ? "other" : "unset"}`);
  writeFileSync(output, JSON.stringify(syntheticAiRun({ knobs: JSON.parse(process.env.FAKE_AI_KNOBS || "{}"), scale: SCALE })));
  process.exit(0);
} else if (mode === "slow") {
  const slowAction = (index, phase) => ({ ...action(index, phase, true, index % 2 ? 280 * SCALE : 20), startMs: index * 400, endMs: index * 400 + (index % 2 ? 300 * SCALE : 30) });
  writeFileSync(output, JSON.stringify({ ...report("passed", [slowAction(0, "setup"), slowAction(1, "warmup"), slowAction(2, "measured"), slowAction(3, "measured")]), readyMs: 0, clock: { source: "LiveEvents::nowNs", runStartMs: RUN_START }, operations: slowOperations() }));
  process.exit(0);
} else if (mode === "pass") {
  writeFileSync(output, JSON.stringify(report("passed", [action(0, "setup", true, 20), action(1, "warmup", true, 40), action(2, "measured", true, 30), action(3, "measured", true, 50)])));
  process.exit(0);
} else if (mode === "fail") {
  writeFileSync(output, JSON.stringify(report("failed", [action(0, "setup", true, 20), action(1, "measured", false, null)], "action 1 (click): timed out")));
  process.exit(1);
} else if (mode === "refuse") {
  console.error("[UiLab] Refusing to run: ARCHIVIST_PROFILE_DIR is not set");
  process.exit(3);
} else if (mode === "crash") {
  writeFileSync(output, JSON.stringify(report("running", [action(0, "setup", true, 20)])));
  process.kill(process.pid, "SIGKILL");
} else if (mode === "driver-timeout") {
  writeFileSync(output, JSON.stringify(report("timeout", [action(0, "setup", true, 20)], "the scenario ran past its timeoutMs")));
  process.exit(2);
} else if (mode === "hang") {
  writeFileSync(output, JSON.stringify(report("running", [])));
  setInterval(() => {}, 1000);
}
