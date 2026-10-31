// Build evidence: Ninja's own records turned into jobs, rebuild causes,
// header fan-out, wall vs summed time and a dependency chain. The end-to-end
// test builds a small CMake project with a shared header for real.
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdir, utimes, writeFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import {
  analyzeExplain, categorize, collectBuildEvidence, countIncluders, criticalPath, diffCache, explainCollector,
  groupJobs, parallelism, parseCcacheStats, parseExplainLine, parseNinjaQuery, parseSccacheStats,
} from "../build-evidence.mjs";
import { ninjaLogSize } from "../ninja.mjs";
import { summarizeTrace, summarizeTraces, traceFileFor } from "../time-trace.mjs";
import { tempDir } from "./helpers.mjs";

const has = (command) => spawnSync(command, ["--version"], { stdio: "ignore" }).status === 0;

test("explain lines are parsed into reasons", () => {
  assert.deepEqual(parseExplainLine("ninja explain: output a.o older than most recent input /s/x.h (1 vs 2)"), { kind: "input-newer", output: "a.o", input: "/s/x.h" });
  assert.deepEqual(parseExplainLine("ninja explain: a.o is dirty"), { kind: "dirty", output: "a.o" });
  assert.deepEqual(parseExplainLine("ninja explain: command line changed for b.o"), { kind: "command-changed", output: "b.o" });
  assert.deepEqual(parseExplainLine("ninja explain: output c.o doesn't exist"), { kind: "missing-output", output: "c.o" });
  assert.equal(parseExplainLine("[1/2] Building"), null);
});

test("changed inputs are causes; rebuilt inputs are links in a chain", () => {
  const lines = [
    "output gen/moc_a.cpp older than most recent input /src/a.h (1 vs 2)",
    "gen/moc_a.cpp is dirty",
    "output a.o older than most recent input /src/a.h (1 vs 2)",
    "a.o is dirty",
    "output moc_a.o older than most recent input gen/moc_a.cpp (1 vs 2)",
    "moc_a.o is dirty",
    "command line changed for z.o",
    "z.o is dirty",
    "lib.a is dirty",
  ].map((line) => `ninja explain: ${line}`);
  const result = analyzeExplain(lines, { root: "/src", buildDir: "/build", isCompile: (o) => o.endsWith(".o") });
  assert.equal(result.causes.length, 1, "moc_a.cpp is rebuilt itself, so only a.h is a cause");
  assert.equal(result.causes[0].input, "a.h");
  assert.equal(result.causes[0].inputKind, "header");
  assert.equal(result.causes[0].outputs, 2);
  assert.equal(result.causes[0].compiles, 1);
  assert.equal(result.commandLineChanged.count, 1);
  assert.equal(result.propagated, 1, "lib.a is dirty only because its objects are");
});

test("ninja -t query output becomes rules and edges", () => {
  const graph = parseNinjaQuery([
    "app:",
    "  input: CXX_EXECUTABLE_LINKER__app_",
    "    main.o",
    "    | libcore.a",
    "    || order_dep",
    "  outputs:",
    "    all",
  ].join("\n"));
  assert.deepEqual(graph.get("app"), { rule: "CXX_EXECUTABLE_LINKER__app_", inputs: ["main.o"], implicit: ["libcore.a"], orderOnly: ["order_dep"], outputs: ["all"] });
});

test("jobs are categorized, timed and chained", () => {
  const jobs = groupJobs([
    { output: "a.o", startMs: 0, endMs: 100, hash: "1" },
    { output: "b.o", startMs: 0, endMs: 300, hash: "2" },
    { output: "lib.a", startMs: 300, endMs: 350, hash: "3" },
    { output: "app", startMs: 350, endMs: 500, hash: "4" },
  ]);
  const t = parallelism(jobs);
  assert.equal(t.wallMs, 500);
  assert.equal(t.summedMs, 600, "parallel work adds up beyond wall time");
  assert.equal(t.peak, 2);
  const graph = new Map([
    ["lib.a", { rule: "X", inputs: ["a.o", "b.o"], implicit: [], orderOnly: [], outputs: [] }],
    ["app", { rule: "Y", inputs: ["main.o"], implicit: ["lib.a"], orderOnly: [], outputs: [] }],
  ]);
  const chain = criticalPath(jobs, graph);
  assert.equal(chain.lengthMs, 500);
  assert.deepEqual(chain.jobs.map((id) => jobs.find((j) => j.id === id).outputs[0]), ["b.o", "lib.a", "app"]);
  assert.equal(criticalPath(jobs, new Map()).available, false, "no graph, no claim");
  assert.equal(categorize({ outputs: ["x.o"] }, "CXX_COMPILER__core_unscanned_"), "compile");
  assert.equal(categorize({ outputs: ["lib.a"] }, "CXX_STATIC_LIBRARY_LINKER__core_"), "archive");
  assert.equal(categorize({ outputs: ["app"] }, "CXX_EXECUTABLE_LINKER__app_"), "link");
  assert.equal(categorize({ outputs: ["core_autogen/mocs_compilation.cpp"] }, "CUSTOM_COMMAND"), "autogen");
  assert.equal(categorize({ outputs: ["CMakeFiles/cmake.verify_globs"] }, "VERIFY_GLOBS"), "cmake");
});

test("ninja -t deps output counts includers", () => {
  const text = "a.o: #deps 2, deps mtime 1 (VALID)\n    /s/a.cpp\n    /s/x.h\n\nb.o: #deps 2, deps mtime 1 (VALID)\n    /s/b.cpp\n    /s/x.h\n";
  const { objects, counts } = countIncluders(text, (file) => file.endsWith(".h"));
  assert.equal(objects, 2);
  assert.equal(counts.get("/s/x.h"), 2);
});

test("compiler-cache counters: observed hits, not configuration", () => {
  const before = parseCcacheStats("direct_cache_hit\t10\npreprocessed_cache_hit\t2\ncache_miss\t5\nunsupported_compiler_option\t1\ncache_size_kibibyte\t9999\n");
  const after = parseCcacheStats("direct_cache_hit\t30\npreprocessed_cache_hit\t2\ncache_miss\t25\nunsupported_compiler_option\t1\ncache_size_kibibyte\t9999\n");
  assert.deepEqual(before, { hits: 12, misses: 5, uncacheable: 1 });
  assert.deepEqual(diffCache(before, after), { hits: 20, misses: 20, uncacheable: 0, hitRate: 0.5 });
  assert.equal(diffCache(after, before), null, "a reset during the build is not reported as numbers");
  assert.equal(parseCcacheStats("ccache version 3"), null);
  const sc = parseSccacheStats(JSON.stringify({ stats: { cache_hits: { counts: { "C/C++": 4 } }, cache_misses: { counts: { "C/C++": 1 } }, not_cached: {} } }));
  assert.deepEqual(sc, { hits: 4, misses: 1, uncacheable: 0 });
});

test("time-trace files are summed into header and template rankings", async () => {
  const trace = (us) => ({ traceEvents: [
    { ph: "X", name: "Total ExecuteCompiler", dur: us },
    { ph: "X", name: "Total Frontend", dur: us - 100 },
    { ph: "X", name: "Source", dur: 4000, args: { detail: "/r/heavy.h" } },
    { ph: "X", name: "InstantiateClass", dur: 2000, args: { detail: "std::map<int, int>" } },
  ] });
  assert.equal(summarizeTrace(trace(5000)).headers.get("/r/heavy.h"), 4000);
  const files = { [traceFileFor("/b", "a.cpp.o")]: JSON.stringify(trace(5000)), [traceFileFor("/b", "b.cpp.o")]: JSON.stringify(trace(9000)) };
  const read = async (file) => {
    if (!(file in files)) throw new Error("missing");
    return files[file];
  };
  const summary = await summarizeTraces("/b", ["a.cpp.o", "b.cpp.o", "c.cpp.o"], { read, relative: (f) => f.replace("/r/", "") });
  assert.equal(summary.units, 2);
  assert.equal(summary.missing, 1);
  assert.deepEqual(summary.headers[0], { name: "heavy.h", totalMs: 8, units: 2 });
  assert.equal(summary.slowestUnits[0].output, "b.cpp.o");
});

test("a real clang -ftime-trace file is understood", { skip: !has("clang++") && "clang++ not installed" }, async () => {
  const dir = await tempDir();
  await writeFile(path.join(dir, "x.cpp"), "#include <map>\n#include <string>\nint f(){ std::map<std::string,int> m; m[\"a\"] = 1; return (int)m.size(); }\n");
  execFileSync("clang++", ["-std=c++20", "-ftime-trace", "-c", "x.cpp", "-o", "x.cpp.o"], { cwd: dir });
  const summary = await summarizeTraces(dir, ["x.cpp.o"]);
  assert.equal(summary.units, 1);
  assert.ok(summary.totals.frontendMs > 0);
  assert.ok(summary.headers.some((header) => /map/.test(header.name)), "the <map> header is ranked");
});

// The acceptance test: change a header three sources include, rebuild, and
// check that Observatory finds the header, the jobs and the fan-out.
test("a changed shared header is traced to the compiles it caused", { skip: !(has("cmake") && has("ninja")) && "cmake or ninja not installed" }, async () => {
  const dir = await tempDir();
  const src = path.join(dir, "src");
  const build = path.join(dir, "build");
  await mkdir(src);
  await writeFile(path.join(src, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.20)\nproject(Fx CXX)\nadd_library(core STATIC a.cpp b.cpp c.cpp)\nadd_executable(app main.cpp lone.cpp)\ntarget_link_libraries(app core)\n");
  await writeFile(path.join(src, "shared.h"), "#pragma once\ninline int shared() { return 1; }\n");
  for (const name of ["a", "b", "c"]) await writeFile(path.join(src, `${name}.cpp`), `#include "shared.h"\nint f_${name}() { return shared(); }\n`);
  await writeFile(path.join(src, "main.cpp"), "int main() { return 0; }\n");
  await writeFile(path.join(src, "lone.cpp"), "int lone() { return 2; }\n");
  execFileSync("cmake", ["-S", src, "-B", build, "-G", "Ninja"], { stdio: "ignore" });
  execFileSync("ninja", ["-C", build], { stdio: "ignore" });

  const later = new Date(Date.now() + 5000);
  await utimes(path.join(src, "shared.h"), later, later);
  const explain = explainCollector();
  const dry = spawnSync("ninja", ["-C", build, "-n", "-d", "explain"], { encoding: "utf8" });
  for (const line of `${dry.stdout}\n${dry.stderr}`.split("\n")) explain.line(line);
  const logBefore = await ninjaLogSize(path.join(build, ".ninja_log"));
  execFileSync("ninja", ["-C", build], { stdio: "ignore" });

  const { summary, artifact } = await collectBuildEvidence({ root: src, buildDir: build, logBefore, explain: explain.result() });
  const cause = summary.rebuild.causes[0];
  assert.equal(cause.input, "shared.h");
  assert.equal(cause.inputKind, "header");
  assert.equal(cause.compiles, 3, "a, b and c include it; main and lone don't");
  assert.equal(cause.includedBy.objects, 3, "confirmed by the compiler's own deps");
  assert.equal(summary.byCategory.compile.jobs, 3);
  assert.ok(!artifact.jobs.some((job) => /lone|main\.cpp/.test(job.source ?? "")), "untouched sources were not rebuilt");
  assert.deepEqual(artifact.jobs.filter((job) => job.category === "compile").map((job) => job.source).sort(), ["a.cpp", "b.cpp", "c.cpp"]);
  assert.ok(summary.timing.summedJobMs >= summary.timing.longestJob.durationMs);
  assert.ok(summary.timing.wallMs <= summary.timing.summedJobMs || summary.timing.peakParallelJobs === 1);
  assert.ok(summary.criticalPath.available);
  assert.deepEqual(summary.criticalPath.jobs.slice(-2).map((job) => job.category), ["archive", "link"], "objects -> libcore.a -> app");
  assert.equal(summary.headers.mostIncluded[0].header, "shared.h");
  assert.deepEqual(summary.compilerCache, { configured: false });
});
