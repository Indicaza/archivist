import assert from "node:assert/strict";
import { readdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { createRunDir, listRunIds, makeRunId, observatoryRoot, readSummary, writeJsonAtomic } from "../storage.mjs";
import { tempDir } from "./helpers.mjs";

test("the environment variable overrides the default location", () => {
  assert.equal(observatoryRoot({ ARCHIVIST_OBSERVATORY_DIR: "/tmp/obs" }, "darwin", "/Users/x"), "/tmp/obs");
  assert.equal(observatoryRoot({}, "darwin", "/Users/x"), "/Users/x/Library/Application Support/Archivist/Observatory");
  assert.equal(observatoryRoot({}, "linux", "/home/x"), "/home/x/.local/share/archivist/observatory");
});

test("run ids are unique and sort by time even within one millisecond", () => {
  const date = new Date("2026-10-30T04:00:00.123Z");
  const ids = new Set(Array.from({ length: 200 }, () => makeRunId("build", date)));
  assert.equal(ids.size, 200);
  assert.match([...ids][0], /^20261030T040000-123Z-build-[0-9a-f]{6}$/);
});

test("rapid runs each get their own directory and never reuse one", async () => {
  const root = await tempDir();
  const runs = await Promise.all(Array.from({ length: 20 }, () => createRunDir(root, "build")));
  assert.equal(new Set(runs.map((run) => run.runId)).size, 20);
  await writeFile(path.join(runs[0].dir, "summary.json"), "{\"first\":true}");
  await createRunDir(root, "build");
  assert.equal(await readFile(path.join(runs[0].dir, "summary.json"), "utf8"), "{\"first\":true}");
  assert.equal((await listRunIds(root)).length, 21);
});

test("summaries are written atomically and leave no temporary files", async () => {
  const root = await tempDir();
  const { runId, dir } = await createRunDir(root, "build");
  const summary = { schemaVersion: 1, runId, phases: [{ name: "configure", durationMs: 12.5 }] };
  await writeJsonAtomic(path.join(dir, "summary.json"), summary);
  await writeJsonAtomic(path.join(dir, "summary.json"), { ...summary, status: "success" });
  assert.deepEqual(await readdir(dir), ["summary.json"]);
  assert.deepEqual((await readSummary(root, runId)).summary, { ...summary, status: "success" });
});

test("listing an observatory that does not exist yet is empty", async () => {
  assert.deepEqual(await listRunIds(path.join(await tempDir(), "missing")), []);
});
