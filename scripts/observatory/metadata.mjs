// Source, machine and toolchain facts for a run. Anything that cannot be
// read is recorded as null; nothing here is guessed or required.
import { execFile } from "node:child_process";
import { readFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

// Runs a small command and returns its trimmed stdout, or null.
export function probe(command, args, options = {}) {
  return new Promise((resolve) => {
    execFile(command, args, { timeout: 10_000, ...options }, (error, stdout) => {
      resolve(error ? null : String(stdout).trim());
    });
  });
}

export async function collectSource(cwd, run = probe) {
  const [commit, branch, status] = await Promise.all([
    run("git", ["rev-parse", "HEAD"], { cwd }),
    run("git", ["branch", "--show-current"], { cwd }),
    run("git", ["status", "--porcelain=v1"], { cwd }),
  ]);
  const changed = status === null ? null : status.split("\n").filter(Boolean).length;
  return {
    commit,
    branch: branch || null,
    dirty: changed === null ? null : changed > 0,
    changedFiles: changed,
  };
}

export async function collectMachine(run = probe, system = os) {
  const platform = system.platform();
  const cpus = system.cpus();
  let osVersion = null;
  if (platform === "darwin") {
    osVersion = await run("sw_vers", ["-productVersion"]);
  }
  return {
    platform,
    osVersion: osVersion || system.release() || null,
    architecture: system.arch(),
    cpuModel: cpus[0]?.model?.trim() || null,
    logicalCpus: typeof system.availableParallelism === "function"
      ? system.availableParallelism()
      : cpus.length || null,
    totalMemoryBytes: system.totalmem() || null,
    nodeVersion: process.version,
  };
}

// Paths under the home folder would carry the username.
export function redactHome(value, home = os.homedir()) {
  if (!value) {
    return null;
  }
  return home && value.startsWith(home) ? `~${value.slice(home.length)}` : value;
}

function firstVersion(text) {
  return text?.match(/\d+\.\d+(?:\.\d+)?/)?.[0] ?? null;
}

export function parseCMakeCache(text) {
  const values = {};
  for (const line of text.split("\n")) {
    const match = line.match(/^([A-Za-z0-9_.-]+):[A-Z]+=(.*)$/);
    if (match) {
      values[match[1]] = match[2];
    }
  }
  return values;
}

async function readText(file) {
  try {
    return await readFile(file, "utf8");
  } catch {
    return null;
  }
}

// Compiler id and version as CMake detected them for this build directory.
async function compilerFromBuildDir(buildDir, cache) {
  const compiler = { id: null, version: null, path: redactHome(cache.CMAKE_CXX_COMPILER) };
  const version = cache.CMAKE_CACHE_MAJOR_VERSION
    ? `${cache.CMAKE_CACHE_MAJOR_VERSION}.${cache.CMAKE_CACHE_MINOR_VERSION}.${cache.CMAKE_CACHE_PATCH_VERSION}`
    : null;
  if (!version) {
    return compiler;
  }
  const text = await readText(path.join(buildDir, "CMakeFiles", version, "CMakeCXXCompiler.cmake"));
  compiler.id = text?.match(/set\(CMAKE_CXX_COMPILER_ID "([^"]*)"\)/)?.[1] || null;
  compiler.version = text?.match(/set\(CMAKE_CXX_COMPILER_VERSION "([^"]*)"\)/)?.[1] || null;
  return compiler;
}

// Reads what the configure step recorded. Call after configure.
export async function collectToolchain(buildDir, run = probe) {
  const cacheText = await readText(path.join(buildDir, "CMakeCache.txt"));
  const cache = cacheText ? parseCMakeCache(cacheText) : {};
  const ninjaProgram = cache.CMAKE_MAKE_PROGRAM || "ninja";
  const [cmake, ninja, ccache, sccache] = await Promise.all([
    run("cmake", ["--version"]),
    run(ninjaProgram, ["--version"]),
    run("ccache", ["--version"]),
    run("sccache", ["--version"]),
  ]);
  let qtVersion = null;
  if (cache.Qt6_DIR) {
    const text = await readText(path.join(cache.Qt6_DIR, "Qt6ConfigVersion.cmake"));
    qtVersion = text?.match(/set\(PACKAGE_VERSION "([^"]*)"\)/)?.[1] || null;
  }
  return {
    buildType: cache.CMAKE_BUILD_TYPE || null,
    generator: cache.CMAKE_GENERATOR || null,
    cmakeVersion: firstVersion(cmake),
    ninjaVersion: firstVersion(ninja),
    compiler: await compilerFromBuildDir(buildDir, cache),
    qtVersion,
    compilerCache: {
      // Whether the build is set up to use a cache, not whether it hit.
      launcher: cache.CMAKE_CXX_COMPILER_LAUNCHER || null,
      ccacheInstalled: ccache !== null,
      sccacheInstalled: sccache !== null,
    },
  };
}
