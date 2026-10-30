// The Archivist development build, split into measurable phases. Each phase
// calls the same script or command the normal build uses:
//   npm run build:ide:cached  -> scripts/build-ide.mjs
//   scripts/qt-build          -> scripts/qt-configure, then cmake --build
import { stat } from "node:fs/promises";
import path from "node:path";
import { progressAnalyzer, ninjaLogSize, readNinjaLogSince } from "./ninja.mjs";
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
// them, so a no-change build can be told from an incremental one.
function planAnalyzer() {
  const progress = progressAnalyzer();
  return {
    line: progress.line,
    details() {
      const steps = progress.details().ninjaSteps;
      return { plannedSteps: steps.noWorkToDo ? 0 : steps.planned ?? 0, plannedStepsBasis: "ninja-dry-run" };
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

// clean: no previous build output; no-change: Ninja planned nothing;
// incremental: Ninja planned some steps. Null when the native build never
// started or the plan is missing.
export function classifyBuild(before, phases) {
  const native = phases.find((phase) => phase.name === "native-build" && phase.status !== "skipped");
  if (!native) {
    return { value: null, basis: "native build did not run" };
  }
  const plan = phases.find((phase) => phase.name === "build-plan" && phase.status === "success");
  let value = null;
  if (!before.ninjaLogExisted) {
    value = "clean";
  } else if (plan) {
    value = plan.details.plannedSteps === 0 ? "no-change" : "incremental";
  }
  return { value, basis: "inferred from build directory state and ninja dry run" };
}
