#!/usr/bin/env node
// TEST FIXTURES ONLY. live/ holds one recording made by the real flight
// recorder (Qt 6.4, offscreen software rendering on Linux, a scripted 5 s
// session with a 90 ms and a 320 ms UI-thread stall). It is not a measurement
// of Archivist on a Mac. The other recordings here are derived from it.
//
//   node scripts/observatory/test/fixtures/live-runs.mjs <empty-dir>
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { writeFixtureRuns } from "./dashboard-runs.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const read = (name) => readFileSync(path.join(HERE, "live", name), "utf8");

export const LIVE_FIXTURE_ID = JSON.parse(read("summary.json")).runId;
export const LIVE_CALM_ID = "20261030T150000-000Z-live-0000c1";
export const LIVE_PARTIAL_ID = "20261030T160000-000Z-live-0000c2";
export const LIVE_FUTURE_ID = "20261030T170000-000Z-live-0000c3";

// { runId, summary, metrics, events } of the recorded session.
export function liveFixture() {
  return { runId: LIVE_FIXTURE_ID, summary: JSON.parse(read("summary.json")), metrics: JSON.parse(read("metrics.json")), events: JSON.parse(read("events.json")) };
}

function filesOf(metrics, events, stream = null) {
  const files = {};
  if (metrics) files["metrics.json"] = JSON.stringify(metrics);
  if (events) files["events.json"] = JSON.stringify(events);
  if (stream) files["live-stream.ndjson"] = stream;
  return files;
}

// runId -> { summary, files }: the recording, a calmer one to compare with,
// one cut short by a crash (recovered: no metrics.json, partial), and one
// with a newer measurement version.
export function liveFixtures() {
  const base = liveFixture();
  const runs = { [base.runId]: { summary: base.summary, files: { ...filesOf(base.metrics, base.events), "live-stream.ndjson": read("live-stream.ndjson") } } };

  const calm = structuredClone(base.summary);
  Object.assign(calm, { runId: LIVE_CALM_ID, label: "fixture: calm", createdAt: "2026-10-30T15:00:00.000Z", finishedAt: "2026-10-30T15:00:05.300Z" });
  calm.live.spikes = [];
  calm.live.stats.eventLoop = { ...calm.live.stats.eventLoop, maxDelayMs: 4.1, stalls: 0 };
  calm.live.stats.cpu = { ...calm.live.stats.cpu, meanPercent: 9.5 };
  const calmMetrics = structuredClone(base.metrics);
  calmMetrics.eventLoopProbes = calmMetrics.eventLoopProbes.map(([atMs, delay]) => [atMs, Math.min(delay, 4.1)]);
  runs[LIVE_CALM_ID] = { summary: calm, files: filesOf(calmMetrics, base.events) };

  const partial = structuredClone(base.summary);
  Object.assign(partial, { runId: LIVE_PARTIAL_ID, label: "fixture: crashed", status: "interrupted", createdAt: "2026-10-30T16:00:00.000Z" });
  partial.live = { ...partial.live, partial: true, stopReason: "app-ended", durationMs: 4000 };
  partial.artifacts = { stream: "live-stream.ndjson" };
  runs[LIVE_PARTIAL_ID] = { summary: partial, files: { "live-stream.ndjson": read("live-stream.ndjson") } };

  const future = structuredClone(base.summary);
  Object.assign(future, { runId: LIVE_FUTURE_ID, label: "fixture: newer measurement", createdAt: "2026-10-30T17:00:00.000Z" });
  future.live.measurementVersion = 2;
  runs[LIVE_FUTURE_ID] = { summary: future, files: filesOf({ ...base.metrics, measurementVersion: 2 }, base.events) };
  return runs;
}

export async function writeLiveFixtures(root) {
  return writeFixtureRuns(root, liveFixtures());
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const root = process.argv[2];
  if (!root) {
    console.error("Usage: node scripts/observatory/test/fixtures/live-runs.mjs <empty-dir>");
    process.exit(2);
  }
  const ids = await writeLiveFixtures(path.resolve(root));
  console.log(`Wrote ${ids.length} live fixture recordings to ${path.resolve(root)}/runs`);
}
