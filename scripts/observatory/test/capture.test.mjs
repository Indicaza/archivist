import assert from "node:assert/strict";
import test from "node:test";
import { exitCodeFor, runCapture, runPhase } from "../capture.mjs";
import { memoryLog, nodePhase } from "./helpers.mjs";

test("a successful command is captured with its output", async () => {
  const log = memoryLog();
  const result = await runPhase(nodePhase("hello", "console.log('out'); console.error('err')"), { log });
  assert.equal(result.status, "success");
  assert.equal(result.exitCode, 0);
  assert.match(log.text(), /out/);
  assert.match(log.text(), /err/);
  assert.equal(result.error, undefined);
});

test("a failing command keeps its exit code and an error excerpt", async () => {
  const result = await runPhase(nodePhase("broken", "console.error('x.cpp:1:2: error: nope'); process.exit(7)"), { log: memoryLog() });
  assert.equal(result.status, "failure");
  assert.equal(result.exitCode, 7);
  assert.deepEqual(result.error.excerpt, ["x.cpp:1:2: error: nope"]);
});

test("a command that cannot start is a failure, not a crash", async () => {
  const result = await runPhase({ name: "missing", command: "/definitely/not/here" }, { log: memoryLog() });
  assert.equal(result.status, "failure");
  assert.match(result.error.message, /Could not start/);
});

test("phase durations and offsets are measured", async () => {
  const state = await runCapture([
    nodePhase("first", "setTimeout(() => {}, 150)"),
    nodePhase("second", "setTimeout(() => {}, 250)"),
  ], { log: memoryLog() });
  const [first, second] = state.phases;
  assert.equal(state.status, "success");
  assert.ok(first.durationMs >= 140, `first took ${first.durationMs}`);
  assert.ok(second.durationMs >= 240, `second took ${second.durationMs}`);
  assert.ok(second.startOffsetMs >= first.startOffsetMs + first.durationMs);
  assert.ok(state.totalDurationMs >= first.durationMs + second.durationMs);
});

test("a failure stops later phases, saves partial progress and keeps the exit code", async () => {
  const saved = [];
  const state = await runCapture([
    nodePhase("ok", ""),
    nodePhase("fails", "process.exit(3)"),
    nodePhase("never", "process.exit(0)"),
  ], { log: memoryLog(), onProgress: async (progress) => saved.push(JSON.parse(JSON.stringify(progress.phases))) });
  assert.equal(state.status, "failure");
  assert.deepEqual(state.phases.map((phase) => phase.status), ["success", "failure", "skipped"]);
  assert.equal(exitCodeFor(state), 3);
  assert.equal(saved[0].length, 1);
  assert.equal(saved[1][1].status, "failure");
});

test("an interruption stops the running phase and is recorded", async () => {
  const phases = [nodePhase("slow", "setTimeout(() => {}, 10000)"), nodePhase("after", "")];
  setTimeout(() => process.kill(process.pid, "SIGTERM"), 200);
  const state = await runCapture(phases, { log: memoryLog() });
  assert.equal(state.status, "interrupted");
  assert.equal(state.interruptedBy, "SIGTERM");
  assert.equal(state.phases[0].status, "interrupted");
  assert.equal(state.phases[1].status, "skipped");
  assert.ok(state.phases[0].durationMs < 5000);
  assert.equal(exitCodeFor(state), 143);
  assert.equal(process.listenerCount("SIGTERM"), 0);
});

test("results serialize to plain JSON", async () => {
  const state = await runCapture([nodePhase("ok", "")], { log: memoryLog() });
  const copy = JSON.parse(JSON.stringify(state));
  assert.deepEqual(copy, state);
  assert.equal(typeof copy.phases[0].durationMs, "number");
});
