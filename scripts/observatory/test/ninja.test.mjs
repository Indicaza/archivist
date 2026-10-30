import assert from "node:assert/strict";
import { appendFile, writeFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { groupSteps, ninjaLogSize, parseNinjaLogLines, progressAnalyzer, readNinjaLogSince } from "../ninja.mjs";
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

test("only the steps appended during the build are read from .ninja_log", async () => {
  const dir = await tempDir();
  const file = path.join(dir, ".ninja_log");
  await writeFile(file, "# ninja log v7\n0\t900\t0\told.o\tabc\n");
  const before = await ninjaLogSize(file);
  await appendFile(file, [
    "10\t1510\t0\ta.o\th1",
    "20\t220\t0\tb.cpp\th2",
    `20\t220\t0\t${dir}/b.cpp\th2`,
    "20\t220\t0\tb.cpp.aotstats\th2",
    "broken line",
    "",
  ].join("\n"));
  const log = await readNinjaLogSince(file, before);
  assert.equal(log.basis, "measured-by-ninja");
  assert.equal(log.stepsRecorded, 2);
  assert.equal(log.workSteps, 2);
  assert.equal(log.sumOfStepDurationsMs, 1700);
  assert.deepEqual(log.slowestSteps[0], { output: "a.o", outputCount: 1, durationMs: 1500 });
  assert.deepEqual(log.slowestSteps[1], { output: "b.cpp", outputCount: 2, durationMs: 200 });
});

test("CMake and AUTOMOC bookkeeping steps are told apart from real work", () => {
  // The steps a no-change Archivist build ran on macOS.
  const build = "/Users/x/Archivist/build/frontend-debug";
  const lines = [
    `17\t67\t0\t${build}/CMakeFiles/cmake.verify_globs\th1`,
    "103\t236\t0\tarchivist-core_autogen/timestamp\th2",
    "103\t236\t0\tarchivist-core_autogen/mocs_compilation.cpp\th2",
    `103\t236\t0\t${build}/archivist-core_autogen/timestamp\th2`,
    "236\t258\t0\tmeta_types/archivist-core_json_file_list.txt\th3",
    "236\t258\t0\tmeta_types/archivist-core_json_file_list.txt.timestamp\th3",
  ];
  const steps = groupSteps(parseNinjaLogLines(lines.join("\n")), build);
  assert.equal(steps.length, 3);
  assert.ok(steps.every((step) => step.bookkeeping));
  const compile = groupSteps(parseNinjaLogLines("0\t5\t0\tCMakeFiles/app.dir/a.cpp.o\th9"));
  assert.equal(compile[0].bookkeeping, false);
});

test("a recompacted or missing log claims nothing", async () => {
  const file = path.join(await tempDir(), ".ninja_log");
  assert.equal((await readNinjaLogSince(file, null)).basis, "unavailable");
  await writeFile(file, "# ninja log v6\n");
  assert.equal((await readNinjaLogSince(file, 9999)).basis, "unavailable");
  assert.deepEqual(parseNinjaLogLines("# header\n"), []);
});

test("builds are classified from the directory state, Ninja's log and the dry run", () => {
  const native = (workSteps, status = "success") => ({ name: "native-build", status, details: { ninjaLog: { basis: "measured-by-ninja", workSteps } } });
  const plan = (work) => ({ name: "build-plan", status: "success", details: { plannedWorkSteps: work } });
  const existed = { ninjaLogExisted: true };
  assert.equal(classifyBuild({ ninjaLogExisted: false }, [plan(10), native(10)]).value, "clean");
  assert.equal(classifyBuild(existed, [plan(0), native(0)]).value, "no-change");
  assert.equal(classifyBuild(existed, [plan(0), native(3)]).value, "incremental");
  // Ninja doesn't log a failed step, so a failed build uses the plan.
  assert.equal(classifyBuild(existed, [plan(2), native(0, "failure")]).value, "incremental");
  assert.equal(classifyBuild(existed, [{ name: "native-build", status: "skipped" }]).value, null);
});

test("the dry run counts CMake's re-check steps apart", async () => {
  const { archivistBuildPhases } = await import("../build-phases.mjs");
  const planPhase = archivistBuildPhases({ root: "/r", buildDir: "/r/build" }).find((phase) => phase.name === "build-plan");
  const analyzer = planPhase.analyze();
  ["[1/2] Re-checking globbed directories...", "[2/2] Re-running CMake..."].forEach(analyzer.line);
  assert.deepEqual(analyzer.details(), { plannedSteps: 2, plannedBookkeepingSteps: 2, plannedWorkSteps: 0, plannedStepsBasis: "ninja-dry-run" });
});
