// The Archivist development build, split into measurable phases. Each phase
// calls the same script or command the normal build uses:
//   npm run build:ide:cached  -> scripts/build-ide.mjs
//   scripts/qt-build          -> scripts/qt-configure, then cmake --build
import { stat } from "node:fs/promises";
import path from "node:path";
import { isBookkeepingDescription, progressAnalyzer, ninjaLogSize, readNinjaLogSince } from "./ninja.mjs";
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
  return {
    line(text) {
      if (text.includes("Resetting stale frontend CMake cache")) {
        reset = true;
      }
    },
    details() {
      return { staleCacheReset: reset };
    },
  };
}

// Ninja dry run (-n): lists the steps a build would run without running
// them. CMake's glob re-check and regeneration steps are always listed, so
// they are counted apart from real work.
function planAnalyzer() {
  const progress = progressAnalyzer();
  let bookkeeping = 0;
  return {
    line(text) {
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

export function archivistBuildPhases({ root, buildDir }) {
  const relativeBuildDir = path.relative(root, buildDir);
  const ninjaLog = path.join(buildDir, ".ninja_log");
  const nativeBuild = {
    name: "native-build",
    command: "cmake",
    args: ["--build", buildDir],
    display: `cmake --build ${relativeBuildDir}`,
    cwd: root,
    analyze: progressAnalyzer,
    async run(context) {
      const before = await ninjaLogSize(ninjaLog);
      const result = await runPhase(nativeBuild, context);
      result.details.ninjaLog = await readNinjaLogSince(ninjaLog, before);
      return result;
    },
  };
  return [
    { name: "ide-bundle", command: process.execPath, args: [path.join(root, "scripts", "build-ide.mjs")], display: "node scripts/build-ide.mjs", cwd: root, analyze: ideAnalyzer },
    { name: "configure", command: path.join(root, "scripts", "qt-configure"), display: "scripts/qt-configure", cwd: root, analyze: configureAnalyzer },
    { name: "build-plan", command: "cmake", args: ["--build", buildDir, "--", "-n"], display: `cmake --build ${relativeBuildDir} -- -n`, cwd: root, analyze: planAnalyzer },
    nativeBuild,
  ];
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
