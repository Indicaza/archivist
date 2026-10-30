import assert from "node:assert/strict";
import test from "node:test";
import { parseRuntimeLog, summarizeEvents } from "../runtime-log.mjs";

const LOG = [
  "qml: something unrelated",
  "archivist.wells.timing: publish: geometry 0.12, views 0.3, panels 1.5, documents 0.01, revision 2, surfaces 0.05, composer 0.02 ms",
  "archivist.wells.timing: minimize “Notes, model draft” model 0.0123 ms, publish 3.25 ms",
  "archivist.wells.timing: restore “Notes” original model 5e-05 ms, publish 2.5 ms, after 0.75 ms",
  "archivist.wells.timing: minimize “Broken” model ms, publish ms",
  "archivist.wells.timing: publish: geometry fast ms",
  "archivist.chat: publish: geometry 1 ms",
].join("\n");

test("known wells timing lines become events; titles are not stored", () => {
  const parsed = parseRuntimeLog(LOG);
  assert.equal(parsed.categoryLines, 5);
  assert.equal(parsed.events.length, 3);
  const [publish, minimize, restore] = parsed.events;
  assert.equal(publish.operation, "publish");
  assert.equal(publish.lineNumber, 2);
  assert.equal(publish.stagesMs.panels, 1.5);
  assert.equal(publish.totalMs, 4);
  assert.deepEqual(minimize, { operation: "minimize", lineNumber: 3, modelMs: 0.0123, publishMs: 3.25 });
  assert.deepEqual(restore, { operation: "restore", lineNumber: 4, how: "original", modelMs: 0.00005, publishMs: 2.5, afterMs: 0.75 });
  assert.ok(!JSON.stringify(parsed).includes("Notes"));
});

test("malformed timing lines are listed, not guessed", () => {
  assert.deepEqual(parseRuntimeLog(LOG).unrecognizedLines, [5, 6]);
});

test("a log without timing lines gives no events", () => {
  const parsed = parseRuntimeLog("");
  assert.deepEqual(parsed.events, []);
  assert.equal(parsed.categoryLines, 0);
  assert.deepEqual(summarizeEvents(parsed.events), {});
});

test("events are summarized per operation", () => {
  const summary = summarizeEvents(parseRuntimeLog(LOG).events);
  assert.deepEqual(summary.minimize, { field: "publishMs", count: 1, meanMs: 3.25, maxMs: 3.25 });
  assert.equal(summary.publish.count, 1);
});
