#!/usr/bin/env node
// Stands in for the Archivist binary in UI Lab runner tests. FAKE_APP_MODE
// picks the behaviour; it writes ui-events.json like the in-app driver.
import { writeFileSync } from "node:fs";

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

if (mode === "pass") {
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
} else if (mode === "hang") {
  writeFileSync(output, JSON.stringify(report("running", [])));
  setInterval(() => {}, 1000);
}
