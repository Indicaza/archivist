// The Archivist development build, split into measurable phases. Each phase
// calls the same script or command the normal build uses:
//   npm run build:ide:cached  -> scripts/build-ide.mjs
//   scripts/qt-build          -> scripts/qt-configure, then cmake --build
import { stat } from "node:fs/promises";
import path from "node:path";
import { isBookkeepingDescription, progressAnalyzer, ninjaLogSize, readNinjaLogSince } from "./ninja.mjs";
import { cacheTool, explainCollector } from "./build-evidence.mjs";
import { parseCMakeCache, probe } from "./metadata.mjs";
import { readFile } from "node:fs/promises";
import { runPhase } from "./capture.mjs";

export const PHASE_LABELS = {
  "ide-bundle": "IDE bundle",
  configure: "Configure",
  "build-plan": "Build plan",
  "native-build": "Native",
};

const CACHED_IDE = "Archivist IDE shell is unchanged; using cached build.";

function ideAnalyzer() {
  let cached = false;
  return {
    line(text) {
      if (text.includes(CACHED_IDE)) {
        cached = true;
      }
    },
    details() {
      return { cacheReused: cached, cacheReusedBasis: "inferred-from-output" };
    },
  };
}

function configureAnalyzer() {
  let reset = false;
  let reused = false;
  return {
    line(text) {
      if (text.includes("Resetting stale frontend CMake cache")) reset = true;
      if (text.includes("CMake cache compatible; Ninja will check regeneration inputs.")) reused = true;
    },
    details() {
      return { staleCacheReset: reset, explicitConfigureSkipped: reused };
    },
  };
}

// Ninja dry run (-n): lists the steps a build would run without running
// them. CMake's glob re-check and regeneration steps are always listed, so
// they are counted apart from real work. With -d explain Ninja also says
// why each output is out of date; those lines go to `explain` (not to
// summary.json) for build-evidence.mjs.
function planAnalyzer(explain = explainCollector()) {
  const progress = progressAnalyzer();
  let bookkeeping = 0;
  return {
    line(text) {
      explain.line(text);
      progress.line(text);
      const description = text.match(/^\[\d+\/\d+\]\s*(.*)$/)?.[1];
      if (description && isBookkeepingDescription(description)) {
        bookkeeping += 1;
      }
    },
    details() {
      const steps = progress.details().ninjaSteps;
      const planned = steps.noWorkToDo ? 0 : steps.planned ?? 0;
      return {
        plannedSteps: planned,
        plannedBookkeepingSteps: bookkeeping,
        plannedWorkSteps: Math.max(0, planned - bookkeeping),
        plannedStepsBasis: "ninja-dry-run",
      };
    },
  };
}

async function compilerCache(buildDir) {
  try {
    const cache = parseCMakeCache(await readFile(path.join(buildDir, "CMakeCache.txt"), "utf8"));
    return cacheTool(cache.CMAKE_CXX_COMPILER_LAUNCHER);
  } catch {
    return null;
  }
}

// evidence: filled in while the phases run (explain lines, the log offset,
// compiler-cache counters) for collectBuildEvidence afterwards.
export function archivistBuildPhases({ root, buildDir, evidence = {}, timeTrace = false }) {
  const relativeBuildDir = path.relative(root, buildDir);
  const ninjaLog = path.join(buildDir, ".ninja_log");
  const explain = explainCollector();
  evidence.explain = null;
  const nativeBuild = {
    name: "native-build",
    command: "cmake",
    args: ["--build", buildDir],
    display: `cmake --build ${relativeBuildDir}`,
    cwd: root,
    analyze: progressAnalyzer,
    async run(context) {
      const before = await ninjaLogSize(ninjaLog);
      const cache = await compilerCache(buildDir);
      const cacheBefore = cache ? await cache.read(probe) : null;
      evidence.logBefore = before;
      evidence.explain = explain.result();
      const result = await runPhase(nativeBuild, context);
      result.details.ninjaLog = await readNinjaLogSince(ninjaLog, before);
      if (cache) evidence.cache = { name: cache.name, before: cacheBefore, after: await cache.read(probe) };
      return result;
    },
  };
  return [
    { name: "ide-bundle", command: process.execPath, args: [path.join(root, "scripts", "build-ide.mjs")], display: "node scripts/build-ide.mjs", cwd: root, analyze: ideAnalyzer },
    timeTrace ? timeTraceConfigure(root, buildDir) : { name: "configure", command: path.join(root, "scripts", "qt-configure"), display: "scripts/qt-configure", cwd: root, analyze: configureAnalyzer },
    { name: "build-plan", command: "cmake", args: ["--build", buildDir, "--", "-n", "-d", "explain"], display: `cmake --build ${relativeBuildDir} -- -n -d explain`, cwd: root, analyze: () => planAnalyzer(explain) },
    nativeBuild,
  ];
}

// The profiling build's configure: the same as scripts/qt-configure (Ninja,
// Debug, Homebrew's prefix when there is one) in its own directory, with
// Clang's -ftime-trace added. Refused if the compiler isn't Clang.
function timeTraceConfigure(root, buildDir) {
  const prefix = probe("brew", ["--prefix"]);
  const phase = {
    name: "configure",
    command: "cmake",
    display: `cmake -S frontend -B ${path.relative(root, buildDir)} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-ftime-trace`,
    cwd: root,
    analyze: () => {
      let compiler = null;
      return {
        line(text) {
          compiler ??= text.match(/The CXX compiler identification is (\S+)/)?.[1] ?? null;
        },
        details: () => ({ timeTrace: true, compilerId: compiler }),
      };
    },
    async run(context) {
      const brew = await prefix;
      phase.args = ["-S", path.join(root, "frontend"), "-B", buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_CXX_FLAGS=-ftime-trace", ...(brew ? [`-DCMAKE_PREFIX_PATH=${brew}`] : [])];
      const result = await runPhase(phase, context);
      const id = result.details.compilerId;
      if (result.status === "success" && id && !/Clang/i.test(id)) {
        return { ...result, status: "failure", error: { message: `-ftime-trace needs Clang; this build uses ${id}.`, excerpt: [] } };
      }
      return result;
    },
  };
  return phase;
}

async function exists(file) {
  try {
    await stat(file);
    return true;
  } catch {
    return false;
  }
}

// What the build directory looked like before anything ran.
export async function buildConditionsBefore(buildDir) {
  return {
    buildDirExisted: await exists(buildDir),
    cmakeCacheExisted: await exists(path.join(buildDir, "CMakeCache.txt")),
    ninjaLogExisted: await exists(path.join(buildDir, ".ninja_log")),
  };
}

// clean: no .ninja_log before the run. Otherwise, for a successful build,
// what Ninja logged: no-change when it ran only CMake/AUTOMOC bookkeeping
// steps, incremental when it ran real work. A failed step is not logged,
// so a failed build falls back to the dry run's plan.
export function classifyBuild(before, phases) {
  const native = phases.find((phase) => phase.name === "native-build" && phase.status !== "skipped");
  if (!native) {
    return { value: null, basis: "native build did not run" };
  }
  if (!before.ninjaLogExisted) {
    return { value: "clean", basis: "no .ninja_log before the build" };
  }
  const log = native.details?.ninjaLog;
  if (native.status === "success" && log?.basis === "measured-by-ninja") {
    return {
      value: log.workSteps === 0 ? "no-change" : "incremental",
      basis: ".ninja_log steps, excluding CMake/AUTOMOC bookkeeping",
    };
  }
  const plan = phases.find((phase) => phase.name === "build-plan" && phase.status === "success");
  if (plan) {
    return {
      value: plan.details.plannedWorkSteps === 0 ? "no-change" : "incremental",
      basis: "ninja dry run, excluding CMake bookkeeping",
    };
  }
  return { value: null, basis: "no Ninja log or plan available" };
}
