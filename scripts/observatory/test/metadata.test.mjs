import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { collectMachine, collectSource, collectToolchain, parseCMakeCache, redactHome } from "../metadata.mjs";
import { tempDir } from "./helpers.mjs";

const nothing = async () => null;

test("source metadata is null, not guessed, when git is unavailable", async () => {
  assert.deepEqual(await collectSource("/", nothing), { commit: null, branch: null, dirty: null, changedFiles: null });
});

test("source metadata counts changed files and handles a detached head", async () => {
  const answers = { "rev-parse": "abc123", branch: "", status: " M a.cpp\n?? b.mjs" };
  const source = await collectSource("/", async (_command, args) => answers[args[0]]);
  assert.deepEqual(source, { commit: "abc123", branch: null, dirty: true, changedFiles: 2 });
});

test("machine metadata tolerates missing CPU details", async () => {
  const system = {
    platform: () => "linux",
    release: () => "6.1",
    arch: () => "arm64",
    cpus: () => [],
    totalmem: () => 0,
  };
  const machine = await collectMachine(nothing, system);
  assert.equal(machine.cpuModel, null);
  assert.equal(machine.logicalCpus, null);
  assert.equal(machine.totalMemoryBytes, null);
  assert.equal(machine.architecture, "arm64");
});

test("toolchain metadata is null for an unconfigured build directory", async () => {
  const toolchain = await collectToolchain(path.join(await tempDir(), "none"), nothing);
  assert.equal(toolchain.buildType, null);
  assert.equal(toolchain.cmakeVersion, null);
  assert.deepEqual(toolchain.compiler, { id: null, version: null, path: null });
  assert.equal(toolchain.compilerCache.ccacheInstalled, false);
});

test("toolchain metadata reads what CMake recorded", async () => {
  const dir = await tempDir();
  const qtDir = path.join(dir, "qt");
  await mkdir(path.join(dir, "CMakeFiles", "3.31.2"), { recursive: true });
  await mkdir(qtDir);
  await writeFile(path.join(dir, "CMakeCache.txt"), [
    "CMAKE_BUILD_TYPE:STRING=Debug",
    "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++",
    "CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3",
    "CMAKE_CACHE_MINOR_VERSION:INTERNAL=31",
    "CMAKE_CACHE_PATCH_VERSION:INTERNAL=2",
    `Qt6_DIR:PATH=${qtDir}`,
  ].join("\n"));
  await writeFile(path.join(dir, "CMakeFiles", "3.31.2", "CMakeCXXCompiler.cmake"),
    'set(CMAKE_CXX_COMPILER_ID "AppleClang")\nset(CMAKE_CXX_COMPILER_VERSION "17.0.0.17000013")\n');
  await writeFile(path.join(qtDir, "Qt6ConfigVersion.cmake"), 'set(PACKAGE_VERSION "6.9.1")\n');
  const run = async (command) => ({ cmake: "cmake version 3.31.2", ninja: "1.12.1" }[command] ?? null);
  const toolchain = await collectToolchain(dir, run);
  assert.equal(toolchain.buildType, "Debug");
  assert.equal(toolchain.cmakeVersion, "3.31.2");
  assert.equal(toolchain.ninjaVersion, "1.12.1");
  assert.deepEqual(toolchain.compiler, { id: "AppleClang", version: "17.0.0.17000013", path: "/usr/bin/c++" });
  assert.equal(toolchain.qtVersion, "6.9.1");
});

test("cache parsing and home redaction", () => {
  assert.deepEqual(parseCMakeCache("// c\nA:BOOL=ON\nB_C:PATH=/x=y\n"), { A: "ON", B_C: "/x=y" });
  assert.equal(redactHome("/Users/x/qt/bin/c++", "/Users/x"), "~/qt/bin/c++");
  assert.equal(redactHome("/usr/bin/c++", "/Users/x"), "/usr/bin/c++");
});
