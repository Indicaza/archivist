// Where Observatory records live and how they are written.
// Each run is its own directory; nothing here ever replaces an earlier run.
import { randomBytes } from "node:crypto";
import { createWriteStream } from "node:fs";
import { mkdir, readdir, readFile, rename, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

export const SCHEMA_VERSION = 1;
export const DIR_ENV = "ARCHIVIST_OBSERVATORY_DIR";

// Outside the repo and build tree, so cleaning a build never erases history.
export function observatoryRoot(env = process.env, platform = process.platform, home = os.homedir()) {
  if (env[DIR_ENV]) {
    return path.resolve(env[DIR_ENV]);
  }
  if (platform === "darwin") {
    return path.join(home, "Library", "Application Support", "Archivist", "Observatory");
  }
  const dataHome = env.XDG_DATA_HOME || path.join(home, ".local", "share");
  return path.join(dataHome, "archivist", "observatory");
}

export function runsDir(root) {
  return path.join(root, "runs");
}

// Sortable by time, unique even for runs started in the same millisecond.
export function makeRunId(kind, date = new Date()) {
  const stamp = date.toISOString().replace(/[-:]/g, "").replace(".", "-");
  return `${stamp}-${kind}-${randomBytes(3).toString("hex")}`;
}

// Creates a fresh run directory. mkdir without `recursive` fails if the
// directory exists, so an existing run can never be reused.
export async function createRunDir(root, kind) {
  const parent = runsDir(root);
  await mkdir(parent, { recursive: true });
  for (let attempt = 0; attempt < 5; attempt += 1) {
    const runId = makeRunId(kind);
    const dir = path.join(parent, runId);
    try {
      await mkdir(dir);
      return { runId, dir };
    } catch (error) {
      if (error.code !== "EEXIST") {
        throw error;
      }
    }
  }
  throw new Error("Could not create a unique Observatory run directory.");
}

// Write to a temporary file, then rename over the target: readers see the
// old summary or the new one, never half of one.
export async function writeJsonAtomic(file, value) {
  const temporary = `${file}.${process.pid}.${randomBytes(3).toString("hex")}.tmp`;
  await writeFile(temporary, `${JSON.stringify(value, null, 2)}\n`, "utf8");
  await rename(temporary, file);
}

export function openLog(file) {
  return createWriteStream(file, { flags: "a" });
}

export function closeLog(stream) {
  return new Promise((resolve) => stream.end(resolve));
}

export async function listRunIds(root) {
  try {
    const entries = await readdir(runsDir(root), { withFileTypes: true });
    return entries.filter((entry) => entry.isDirectory()).map((entry) => entry.name).sort();
  } catch (error) {
    if (error.code === "ENOENT") {
      return [];
    }
    throw error;
  }
}

export async function readSummary(root, runId) {
  const file = path.join(runsDir(root), runId, "summary.json");
  return { file, summary: JSON.parse(await readFile(file, "utf8")) };
}
