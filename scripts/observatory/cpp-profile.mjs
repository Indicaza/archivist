#!/usr/bin/env node
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { DEFAULT_CPP_SOURCES, isolatedCompile, readableHeader, selectCompileJobs } from "./cpp-profile-core.mjs";
import { summarizeTrace, traceFileFor } from "./time-trace.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const sourceRoot = path.join(root, "frontend", "src");
const buildDir = path.join(root, "build", "frontend-debug");
const reportFile = path.join(buildDir, "cpp-profile-report.json");
const arguments_ = process.argv.slice(2);
const json = arguments_.includes("--json");
const selected = arguments_.filter((arg) => arg !== "--json" && arg !== "--help");
if (arguments_.includes("--help")) {
  console.log("Usage: npm run observatory:cpp-profile -- [--json] [main.cpp ai_tools.cpp chat_store.cpp ai_runtime.cpp library_store.cpp]");
  console.log("Profiles only selected files using the existing Ninja Debug build and PCH. Does not rebuild or replace native objects.");
  process.exit(0);
}
const names = selected.length ? selected : DEFAULT_CPP_SOURCES;
if (!existsSync(path.join(buildDir, "build.ninja"))) {
  console.error("Missing build/frontend-debug/build.ninja. Run npm run build:fast first.");
  process.exit(2);
}

function top(map, limit, format = (value) => value) {
  return [...map.entries()]
    .sort((a, b) => b[1] - a[1])
    .slice(0, limit)
    .map(([name, durationUs]) => ({ name: format(name), ms: Math.round(durationUs / 100) / 10 }));
}

function resultEntry(file, trace, elapsedMs) {
  const result = summarizeTrace(trace);
  const ms = (us) => us == null ? null : Math.round(us / 100) / 10;
  return {
    file,
    wallMs: Math.round(elapsedMs),
    compilerMs: ms(result.compileUs),
    frontendMs: ms(result.frontendUs),
    backendMs: ms(result.backendUs),
    instantiateMs: ms(result.instantiateUs),
    headers: top(result.headers, 15, (header) => readableHeader(header, root)),
    templates: top(result.templates, 10),
  };
}

const temp = await mkdtemp(path.join(os.tmpdir(), "archivist-cpp-profile-"));
try {
  const output = execFileSync("ninja", ["-C", buildDir, "-t", "compdb"], {
    encoding: "utf8",
    maxBuffer: 64 * 1024 * 1024,
  });
  const jobs = selectCompileJobs(JSON.parse(output), sourceRoot, names);
  const results = [];
  for (let i = 0; i < jobs.length; i += 1) {
    const job = jobs[i];
    const file = path.resolve(job.directory, job.file);
    const name = path.basename(file);
    const isolatedDir = path.join(temp, String(i));
    await mkdir(isolatedDir);
    const object = path.join(isolatedDir, name + ".o");
    const dep = path.join(isolatedDir, name + ".d");
    const command = isolatedCompile(job, object, dep);
    const version = spawnSync(command.executable, ["--version"], { encoding: "utf8" });
    if (version.status !== 0 || !/clang/i.test(version.stdout ?? "")) {
      throw new Error(`Expected Clang for ${name}, found: ${(version.stdout ?? version.stderr ?? command.executable).split("\n")[0]}`);
    }
    if (!json) console.log(`[C++ profile] ${i + 1}/${jobs.length}: ${name}`);
    const started = performance.now();
    const compiled = spawnSync(command.executable, command.args, {
      cwd: command.cwd,
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
    });
    if (compiled.error) throw compiled.error;
    if (compiled.status !== 0) {
      const details = (compiled.stderr ?? compiled.stdout ?? "").split("\n").slice(-35).join("\n");
      throw new Error(`Clang failed profiling ${name} (exit ${compiled.status}):\n${details}`);
    }
    const tracePath = traceFileFor(isolatedDir, path.basename(object));
    const trace = JSON.parse(await readFile(tracePath, "utf8"));
    results.push(resultEntry(path.relative(root, file), trace, performance.now() - started));
  }
  const report = {
    schema: "archivist.observatory.cpp-profile",
    schemaVersion: 1,
    recordedAt: new Date().toISOString(),
    buildDir: path.relative(root, buildDir),
    method: "one isolated Clang -ftime-trace compile per source, using Ninja Debug compilation commands and existing PCH",
    notes: [
      "The object and dependency output are redirected to a temporary directory, never to the active Ninja build.",
      "Compiler and PCH flags match the current Debug graph; profiling overhead makes times unsuitable as normal build benchmarks.",
      "Header timings are inclusive, so individual header durations must not be added together.",
    ],
    units: results,
  };
  await writeFile(reportFile, JSON.stringify(report, null, 2) + "\n");
  if (json) {
    console.log(JSON.stringify(report, null, 2));
  } else {
    for (const entry of results) {
      const sec = (ms) => ms == null ? "unknown" : (ms / 1000).toFixed(2) + "s";
      console.log(`\n${entry.file}: clang ${sec(entry.compilerMs)}, frontend ${sec(entry.frontendMs)}, backend ${sec(entry.backendMs)}`);
      for (const header of entry.headers.slice(0, 5)) console.log(`  header ${header.ms}ms  ${header.name}`);
      for (const template of entry.templates.slice(0, 3)) console.log(`  template ${template.ms}ms  ${template.name.slice(0, 120)}`);
    }
    console.log(`\n[C++ profile] Full report: ${path.relative(root, reportFile)}`);
    console.log("[C++ profile] No native build outputs were replaced.");
  }
} catch (error) {
  console.error(`[C++ profile] ${error.message}`);
  process.exitCode = 1;
} finally {
  await rm(temp, { recursive: true, force: true });
}
