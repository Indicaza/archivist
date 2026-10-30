import assert from "node:assert/strict";
import { appendFile, writeFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { ninjaLogSize, parseNinjaLogLines, progressAnalyzer, readNinjaLogSince } from "../ninja.mjs";
import { classifyBuild } from "../build-phases.mjs";
import { tempDir } from "./helpers.mjs";

test("progress lines are counted and marked as inferred", () => {
  const analyzer = progressAnalyzer();
  ["[1/3] Building CXX object a.o", "noise", "[3/3] Linking CXX executable app"].forEach(analyzer.line);
  assert.deepEqual(analyzer.details().ninjaSteps, { basis: "inferred-from-output", highestReported: 3, planned: 3, noWorkToDo: false });
  const idle = progressAnalyzer();
  idle.line("ninja: no work to do.");
  assert.equal(idle.details().ninjaSteps.noWorkToDo, true);
});

test("only the entries appended during the build are read from .ninja_log", async () => {
  const file = path.join(await tempDir(), ".ninja_log");
  await writeFile(file, "# ninja log v6\n0\t900\t0\told.o\tabc\n");
  const before = await ninjaLogSize(file);
  await appendFile(file, "10\t1510\t0\ta.o\th1\n20\t220\t0\tb.o\th2\nbroken line\n");
  const log = await readNinjaLogSince(file, before);
  assert.equal(log.basis, "measured-by-ninja");
  assert.equal(log.edgesRecorded, 2);
  assert.equal(log.sumOfEdgeDurationsMs, 1700);
  assert.deepEqual(log.slowestEdges[0], { output: "a.o", durationMs: 1500 });
});

test("a recompacted or missing log claims nothing", async () => {
  const file = path.join(await tempDir(), ".ninja_log");
  assert.equal((await readNinjaLogSince(file, null)).basis, "unavailable");
  await writeFile(file, "# ninja log v6\n");
  assert.equal((await readNinjaLogSince(file, 9999)).basis, "unavailable");
  assert.deepEqual(parseNinjaLogLines("# header\n"), []);
});

test("builds are classified from the directory state and the dry run", () => {
  const plan = (steps) => [{ name: "build-plan", status: "success", details: { plannedSteps: steps } }, { name: "native-build", status: "success" }];
  assert.equal(classifyBuild({ ninjaLogExisted: false }, plan(10)).value, "clean");
  assert.equal(classifyBuild({ ninjaLogExisted: true }, plan(0)).value, "no-change");
  assert.equal(classifyBuild({ ninjaLogExisted: true }, plan(4)).value, "incremental");
  assert.equal(classifyBuild({ ninjaLogExisted: true }, [{ name: "native-build", status: "skipped" }]).value, null);
});
