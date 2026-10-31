#!/usr/bin/env node
import { spawn } from "node:child_process";
import { existsSync, watch } from "node:fs";
import { mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { isQmlRuntimeFile, qtBinaryCandidates, sourceQmlDirectory } from "./qml-dev-utils.mjs";
import { refreshDevQmlTree } from "./qml-dev-stage.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = sourceQmlDirectory(root);
const args = new Set(process.argv.slice(2));
if ([...args].some((arg) => !["--skip-build", "--help"].includes(arg))) {
  console.error("Usage: node scripts/qt-qml-dev.mjs [--skip-build]");
  process.exit(2);
}
if (args.has("--help")) {
  console.log("Run Archivist from source QML and restart only the native process on QML edits. The initial build uses the normal cached build. C++ changes still require a manual rebuild and restart.");
  process.exit(0);
}

const run = (command, commandArgs) => new Promise((resolve, reject) => {
  const child = spawn(command, commandArgs, { cwd: root, stdio: "inherit" });
  child.once("error", reject);
  child.once("exit", (code, signal) => resolve(signal ? 130 : code ?? 1));
});

const build = async () => {
  const exitCode = await run("npm", ["run", "build:fast"]);
  if (exitCode !== 0) throw new Error(`Initial build failed (exit ${exitCode}).`);
};

if (!args.has("--skip-build")) await build();
const binary = qtBinaryCandidates(root).find(existsSync);
if (!binary) {
  console.error("No Archivist binary found. Run npm run build:fast before using --skip-build.");
  process.exit(1);
}

const stopped = await run(path.join(root, "scripts", "qt-stop"), []);
if (stopped !== 0) {
  console.error("Unable to stop the previous managed Archivist instance.");
  process.exit(stopped);
}

const stageDirectory = await mkdtemp(path.join(os.tmpdir(), "archivist-qml-dev-"));
const stagedSource = path.join(stageDirectory, "qml");
await refreshDevQmlTree(source, stagedSource);
console.log("[QML dev] Prepared module-aware QML mirror outside the repository.");

let app = null;
let watcher = null;
let debounce = null;
let restarting = false;
let restartPending = false;
let shuttingDown = false;

function startApp() {
  if (shuttingDown) return;
  console.log("\n[QML dev] Launching Archivist from working-tree QML (no native rebuild on QML edits).");
  const child = spawn(binary, [], {
    cwd: root,
    env: { ...process.env, ARCHIVIST_QML_SOURCE_DIR: stagedSource },
    stdio: "inherit",
  });
  app = child;
  child.once("error", (error) => console.error(`[QML dev] Launch failed: ${error.message}`));
  child.once("exit", (code, signal) => {
    if (app === child) app = null;
    if (!shuttingDown && !restarting) console.log(`[QML dev] Archivist exited (${signal ?? code}). Editing QML will launch it again.`);
  });
}

async function stopApp() {
  const child = app;
  if (!child) return;
  if (child.exitCode !== null || child.signalCode !== null) {
    app = null;
    return;
  }
  await new Promise((resolve) => {
    let finished = false;
    const done = () => {
      if (finished) return;
      finished = true;
      clearTimeout(force);
      resolve();
    };
    child.once("exit", done);
    const force = setTimeout(() => {
      if (child.exitCode === null && child.signalCode === null) child.kill("SIGKILL");
    }, 2500);
    child.kill("SIGTERM");
  });
}

async function restartLoop() {
  if (restarting || shuttingDown) return;
  restarting = true;
  try {
    while (restartPending && !shuttingDown) {
      restartPending = false;
      console.log("[QML dev] Reloading QML — restarting the app, skipping CMake/Ninja.");
      await stopApp();
      await refreshDevQmlTree(source, stagedSource);
      startApp();
    }
  } catch (error) {
    console.error(`[QML dev] Unable to prepare QML source mirror: ${error.message}`);
  } finally {
    restarting = false;
    if (restartPending && !shuttingDown) void restartLoop();
  }
}

function scheduleRestart(file) {
  if (shuttingDown || !isQmlRuntimeFile(file)) return;
  restartPending = true;
  clearTimeout(debounce);
  debounce = setTimeout(() => void restartLoop(), 220);
}

async function shutdown(signal) {
  if (shuttingDown) return;
  shuttingDown = true;
  clearTimeout(debounce);
  watcher?.close();
  await stopApp();
  await rm(stageDirectory, { recursive: true, force: true });
  process.exit(signal === "SIGINT" ? 130 : 0);
}

process.on("SIGINT", () => void shutdown("SIGINT"));
process.on("SIGTERM", () => void shutdown("SIGTERM"));
process.on("SIGHUP", () => void shutdown("SIGHUP"));

watcher = watch(source, { recursive: true }, (_event, filename) => {
  if (filename) scheduleRestart(String(filename));
});
startApp();
console.log(`[QML dev] Watching ${source}. Ctrl+C stops the watcher and its app.`);
console.log("[QML dev] C++/CMake changes still need npm run build:fast and a watcher restart.");
