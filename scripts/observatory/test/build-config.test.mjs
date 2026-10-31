import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdir, readFile, stat, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { archivistBuildPhases } from "../build-phases.mjs";
import { tempDir } from "./helpers.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CHECK = path.resolve(HERE, "../../qt-build-cache-valid");
const has = (command) => spawnSync(command, ["--version"], { stdio: "ignore" }).status === 0;
const check = (source, build) => spawnSync("bash", [CHECK, source, build], { encoding: "utf8" }).status;
const cache = (source, generator = "Ninja", type = "Debug") => [
  `CMAKE_HOME_DIRECTORY:INTERNAL=${source}`,
  `CMAKE_GENERATOR:INTERNAL=${generator}`,
  `CMAKE_BUILD_TYPE:STRING=${type}`,
].join("\n") + "\n";

test("a valid debug Ninja cache can be reused, but a missing or foreign graph cannot", async () => {
  const root = await tempDir();
  const source = path.join(root, "source");
  const build = path.join(root, "build");
  await mkdir(source);
  await mkdir(build);
  assert.equal(check(source, build), 1);
  await writeFile(path.join(build, "CMakeCache.txt"), cache(source));
  assert.equal(check(source, build), 1);
  await writeFile(path.join(build, "build.ninja"), "ninja_required_version = 1.3\n");
  assert.equal(check(source, build), 0);
  await writeFile(path.join(build, "CMakeCache.txt"), cache(path.join(root, "other")));
  assert.equal(check(source, build), 1);
  await writeFile(path.join(build, "CMakeCache.txt"), cache(source, "Unix Makefiles"));
  assert.equal(check(source, build), 1);
  await writeFile(path.join(build, "CMakeCache.txt"), cache(source, "Ninja", "Release"));
  assert.equal(check(source, build), 1);
  await writeFile(path.join(build, "CMakeCache.txt"), cache(source));
  assert.equal(check(source, build), 0);
});

test("Ninja regenerates CMake graph when CMakeLists changes, without explicit configure", {
  skip: !(has("cmake") && has("ninja")) && "cmake and ninja required",
}, async () => {
  const root = await tempDir();
  const source = path.join(root, "source");
  const build = path.join(root, "build");
  await mkdir(source);
  const lists = path.join(source, "CMakeLists.txt");
  const definition = (value) => [
    "cmake_minimum_required(VERSION 3.24)",
    "project(FastBuild NONE)",
    'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/version.txt" CONTENT "' + value + '")',
    "",
  ].join("\\n");
  await writeFile(lists, definition("first"));
  execFileSync("cmake", ["-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug"], { stdio: "ignore" });
  assert.equal(check(source, build), 0);
  assert.equal(await readFile(path.join(build, "version.txt"), "utf8"), "first");
  await writeFile(lists, definition("second"));
  execFileSync("cmake", ["--build", build], { stdio: "ignore" });
  assert.equal(await readFile(path.join(build, "version.txt"), "utf8"), "second");
  assert.ok((await stat(path.join(build, "build.ninja"))).isFile());
  assert.equal(check(source, build), 0);
});

test("Observatory uses the same conditional configure as everyday development", () => {
  const phases = archivistBuildPhases({ root: "/repo", buildDir: "/repo/build/frontend-debug" });
  const configure = phases.find((phase) => phase.name === "configure");
  assert.deepEqual(configure.args, ["--if-needed"]);
  const analyzer = configure.analyze();
  analyzer.line("CMake cache compatible; Ninja will check regeneration inputs.");
  assert.equal(analyzer.details().explicitConfigureSkipped, true);
  const newAnalyzer = configure.analyze();
  assert.equal(newAnalyzer.details().explicitConfigureSkipped, false);
});
