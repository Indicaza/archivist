// Runs one UI Lab scenario: starts an isolated Archivist instance with a
// disposable profile, lets the in-app driver (frontend/qml/App/UiLab)
// run the scenario, and collects what it wrote. Knows nothing about
// summaries or terminals; cli.mjs assembles the record.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import { mkdtemp, readFile, rm, stat } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { nowMs } from "./capture.mjs";

const PROFILE_PREFIX = "archivist-ui-lab-";
export const EXIT = { passed: 0, failed: 1, usage: 2, refused: 3, error: 70, timeout: 124, interrupted: 130 };

// The app binary the dev build produces (a bundle on macOS).
export function appBinaryCandidates(buildDir) {
  return [
    path.join(buildDir, "archivist-qt.app", "Contents", "MacOS", "archivist-qt"),
    path.join(buildDir, "archivist-qt"),
  ];
}

export async function findAppBinary(buildDir) {
  for (const candidate of appBinaryCandidates(buildDir)) {
    try {
      if ((await stat(candidate)).isFile()) {
        return candidate;
      }
    } catch {
      // try the next one
    }
  }
  return null;
}

// Size, time and hash of the binary that ran, so a result can be tied to
// the exact build.
export async function binaryIdentity(file) {
  const details = await stat(file);
  const hash = createHash("sha256");
  await new Promise((resolve, reject) => {
    createReadStream(file).on("data", (chunk) => hash.update(chunk)).on("end", resolve).on("error", reject);
  });
  return { sizeBytes: details.size, modifiedAt: details.mtime.toISOString(), sha256: hash.digest("hex") };
}

export function createProfile() {
  return mkdtemp(path.join(os.tmpdir(), PROFILE_PREFIX));
}

// Removes only a directory this runner made.
export async function removeProfile(dir) {
  if (!path.basename(dir).startsWith(PROFILE_PREFIX) || path.dirname(dir) !== path.resolve(os.tmpdir())) {
    throw new Error(`Refusing to remove ${dir}: not a UI Lab profile.`);
  }
  await rm(dir, { recursive: true, force: true });
}

// The app's environment: the caller's, minus Archivist switches that would
// change behaviour, plus the test profile and the scenario.
export function labEnvironment(base, { profileDir, scenarioFile, eventsFile }) {
  const env = {};
  for (const [name, value] of Object.entries(base)) {
    if (!name.startsWith("ARCHIVIST_") && name !== "QT_LOGGING_RULES") {
      env[name] = value;
    }
  }
  return {
    ...env,
    ARCHIVIST_PROFILE_DIR: profileDir,
    ARCHIVIST_DB_PATH: path.join(profileDir, "archivist.db"),
    ARCHIVIST_UI_LAB_SCENARIO: scenarioFile,
    ARCHIVIST_UI_LAB_OUTPUT: eventsFile,
    // The existing Workspace timings, ingested afterwards.
    QT_LOGGING_RULES: "archivist.wells.timing=true",
  };
}

export async function readEvents(file) {
  try {
    return JSON.parse(await readFile(file, "utf8"));
  } catch {
    return null;
  }
}

// Runs the app until it exits, the timeout passes or the runner is
// interrupted. Only the process started here is ever signalled.
export function runApp({ command, args = [], env, logFile, timeoutMs, killGraceMs = 5000, signals = ["SIGINT", "SIGTERM", "SIGHUP"] }) {
  return new Promise((resolve) => {
    const started = nowMs();
    const log = createWriteStream(logFile, { flags: "a" });
    let timedOut = false;
    let interruptedBy = null;
    let killTimer = null;

    const child = spawn(command, args, { env, stdio: ["ignore", "pipe", "pipe"] });
    child.stdout.pipe(log, { end: false });
    child.stderr.pipe(log, { end: false });

    const stop = () => {
      if (child.exitCode !== null || child.signalCode !== null) {
        return;
      }
      child.kill("SIGTERM");
      killTimer = setTimeout(() => child.kill("SIGKILL"), killGraceMs);
    };
    const timer = setTimeout(() => {
      timedOut = true;
      stop();
    }, timeoutMs);
    const handlers = signals.map((name) => {
      const handler = () => {
        interruptedBy ??= name;
        stop();
      };
      process.on(name, handler);
      return [name, handler];
    });

    let spawnError = null;
    child.on("error", (error) => {
      spawnError = error;
    });
    child.on("close", (exitCode, signal) => {
      clearTimeout(timer);
      clearTimeout(killTimer);
      handlers.forEach(([name, handler]) => process.off(name, handler));
      log.end(() => resolve({
        exitCode,
        signal,
        timedOut,
        interruptedBy,
        spawnError: spawnError?.message ?? null,
        durationMs: Math.round((nowMs() - started) * 10) / 10,
      }));
    });
  });
}

// One word for how the run went, from the app's exit and its own report.
export function classifyRun(app, events) {
  if (app.spawnError) {
    return { status: "error", message: `Could not start the app: ${app.spawnError}` };
  }
  if (app.interruptedBy) {
    return { status: "interrupted", message: `Stopped by ${app.interruptedBy}.` };
  }
  if (app.timedOut) {
    return { status: "timeout", message: "The app did not finish before the overall timeout." };
  }
  if (app.exitCode === EXIT.refused) {
    return { status: "refused", message: "The app refused to run: the test profile was not isolated (see runtime.log)." };
  }
  if (app.signal || (app.exitCode !== 0 && !events)) {
    return { status: "crashed", message: app.signal ? `The app was killed by ${app.signal}.` : `The app exited with code ${app.exitCode} without a report.` };
  }
  if (!events) {
    return { status: "error", message: "The app exited without writing ui-events.json." };
  }
  if (events.status === "running") {
    return { status: "crashed", message: `The app exited (code ${app.exitCode}) in the middle of the scenario.` };
  }
  if (events.status === "passed" && app.exitCode === 0) {
    return { status: "passed", message: null };
  }
  if (events.status === "timeout") {
    return { status: "timeout", message: events.error };
  }
  return { status: events.status === "passed" ? "error" : events.status, message: events.error ?? `The app exited with code ${app.exitCode}.` };
}

export function exitCodeForStatus(status) {
  return EXIT[status] ?? EXIT.error;
}

// Overall time limit: the scenario's own, its startup allowance, and room
// for the app to start and quit.
export function overallTimeoutMs(scenario) {
  return Number(scenario.timeoutMs || 180000) + Number(scenario.readyTimeoutMs || 30000) + 30000;
}
