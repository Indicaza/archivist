// UI test jobs: one background process per job, running the existing UI Lab
// runner (`cli.mjs job-run <jobId>`). The job's state lives in
// <root>/jobs/<jobId>/job.json; the runner writes it, everyone else reads
// it. Only one UI test runs at a time (<root>/jobs/ui-lock.json).
//
//   queued -> starting -> running -> completed | failed | cancelled | timed-out
//   (any active state) -> interrupted   when the job's process is gone
//
// A job is bound to its own process, not to whoever started it: closing the
// dashboard doesn't stop it and can't make it look successful. Nothing
// survives a restart of the machine; such jobs show as interrupted.
import { execFile, spawn } from "node:child_process";
import { closeSync, openSync } from "node:fs";
import { mkdir, readdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import path from "node:path";
import { makeRunId, writeJsonAtomic } from "../storage.mjs";

export const JOB_SCHEMA = "archivist.observatory.job";
export const JOB_SCHEMA_VERSION = 1;
export const JOB_ID = /^[0-9]{8}T[0-9]{6}-[0-9]{3}Z-job-[0-9a-f]{6}$/;
export const JOB_STATUSES = ["queued", "starting", "running", "completed", "failed", "cancelled", "timed-out", "interrupted"];
export const ACTIVE = new Set(["queued", "starting", "running"]);
export const MAX_REPEAT = 10;
// A job that hasn't reported its process this long after being queued is gone.
const START_GRACE_MS = 60000;

export function jobsDir(root) {
  return path.join(root, "jobs");
}

function jobFile(root, jobId) {
  return path.join(jobsDir(root), jobId, "job.json");
}

export function lockFile(root) {
  return path.join(jobsDir(root), "ui-lock.json");
}

// { job } or { error } for a damaged file, null when there is no such job.
export async function readJob(root, jobId) {
  if (!JOB_ID.test(String(jobId))) return null;
  let text;
  try {
    text = await readFile(jobFile(root, jobId), "utf8");
  } catch (error) {
    if (error.code === "ENOENT") return null;
    return { error: `job.json can't be read: ${error.message}` };
  }
  try {
    return { job: JSON.parse(text) };
  } catch (error) {
    return { error: `job.json is not valid JSON: ${error.message}` };
  }
}

export async function writeJob(root, job) {
  job.updatedAt = new Date().toISOString();
  await writeJsonAtomic(jobFile(root, job.jobId), job);
}

export function setStatus(job, status, extra = {}) {
  const at = new Date().toISOString();
  Object.assign(job, extra, { status });
  job.history = [...(job.history ?? []), { status, at }];
  if (!ACTIVE.has(status)) job.finishedAt ??= at;
  return job;
}

export async function listJobIds(root) {
  try {
    return (await readdir(jobsDir(root), { withFileTypes: true }))
      .filter((entry) => entry.isDirectory() && JOB_ID.test(entry.name))
      .map((entry) => entry.name)
      .sort();
  } catch (error) {
    if (error.code === "ENOENT") return [];
    throw error;
  }
}

// ---- processes ----------------------------------------------------------------

export function isAlive(pid) {
  if (!Number.isInteger(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error.code === "EPERM";
  }
}

export function processCommand(pid) {
  return new Promise((resolve) => {
    // -ww: never cut the command line short (macOS ps otherwise trims it to
    // the terminal width, which would hide the job id).
    execFile("ps", ["-ww", "-o", "command=", "-p", String(pid)], { timeout: 5000 }, (error, stdout) => resolve(error ? null : stdout.trim()));
  });
}

// True only for a live process that is this job's runner. A pid alone could
// have been reused by an unrelated program after the runner exited.
export async function ownsProcess(pid, jobId) {
  return (await processOwnership(pid, jobId)) === "owned";
}

// "owned" (this job's runner), "gone" (no such process), "other" (the pid now
// belongs to another program) or "unknown" (alive, but ps couldn't say, e.g.
// on a busy machine). Only "gone" and "other" prove the runner has stopped.
export async function processOwnership(pid, jobId) {
  if (!isAlive(pid)) return "gone";
  for (let attempt = 0; attempt < 2; attempt += 1) {
    const command = await processCommand(pid);
    if (command) return command.includes("job-run") && command.includes(jobId) ? "owned" : "other";
    if (!isAlive(pid)) return "gone";
  }
  return "unknown";
}

async function runnerStopped(pid, jobId) {
  const state = await processOwnership(pid, jobId);
  return state === "gone" || state === "other";
}

// ---- the one-UI-test-at-a-time lock -------------------------------------------
//
// The lock is <root>/jobs/ui-lock.json, created with O_EXCL ("wx"): only one
// process can create it. Taking over a stale lock (its holder is proven
// gone) is the racy part, because several processes can judge the same lock
// stale at once. So the lock file is identified by its inode and birth time,
// and only the process that also creates ui-lock.takeover-<that identity>
// (O_EXCL again) may delete it, and only if the lock is still that same
// file. Everyone then races on "wx" again and exactly one wins.

const TAKEOVER_MARKER_STALE_MS = 30000;

async function readLock(root) {
  try {
    return JSON.parse(await readFile(lockFile(root), "utf8"));
  } catch {
    return null;
  }
}

// { holder (parsed, or null if unreadable), id (which file this is), ageMs } or null if there's no lock.
async function inspectLock(root) {
  let info;
  try {
    info = await stat(lockFile(root));
  } catch {
    return null;
  }
  return { holder: await readLock(root), id: `${info.ino}-${Math.round(info.birthtimeMs || info.ctimeMs)}`, ageMs: Date.now() - info.mtimeMs };
}

async function lockIsStale(lock) {
  const holder = lock.holder;
  // Unreadable: a holder that crashed while writing it, or one writing it right now.
  if (!holder) return lock.ageMs > 5000;
  if (!holder.pid) {
    if (holder.takenBy && !isAlive(holder.takenBy)) return true;
    return Date.now() - Date.parse(holder.since ?? 0) > START_GRACE_MS;
  }
  if (holder.owner === "job") return runnerStopped(holder.pid, holder.jobId);
  return !isAlive(holder.pid);
}

// Deletes the lock only if it is still the file `id`, and only one process at a time.
async function takeOver(root, id) {
  const marker = path.join(jobsDir(root), `ui-lock.takeover-${id}`);
  try {
    await writeFile(marker, `${process.pid}\n`, { flag: "wx" });
  } catch (error) {
    if (error.code !== "EEXIST") throw error;
    // Someone else is taking it over. A marker left by a process that died mid-takeover is cleared later.
    const info = await stat(marker).catch(() => null);
    if (info && Date.now() - info.mtimeMs > TAKEOVER_MARKER_STALE_MS) await rm(marker, { force: true });
    return;
  }
  try {
    const current = await inspectLock(root);
    if (current?.id === id) await rm(lockFile(root), { force: true });
  } finally {
    await rm(marker, { force: true });
  }
}

// holder: { owner: "job" | "cli", jobId?, pid, since }. Returns { ok } or
// { ok: false, holder } when another UI test is running.
export async function acquireLock(root, holder) {
  await mkdir(jobsDir(root), { recursive: true });
  // A job's lock gets its runner's pid once that has started; until then
  // takenBy (the process that took the lock) shows whether it is still alive.
  const value = { ...holder, takenBy: process.pid, since: holder.since ?? new Date().toISOString() };
  for (let attempt = 0; attempt < 50; attempt += 1) {
    try {
      await writeFile(lockFile(root), `${JSON.stringify(value, null, 2)}\n`, { flag: "wx" });
      return { ok: true };
    } catch (error) {
      if (error.code !== "EEXIST") throw error;
    }
    const current = await inspectLock(root);
    if (current) {
      if (!(await lockIsStale(current))) return { ok: false, holder: current.holder ?? { unreadable: true } };
      await takeOver(root, current.id);
    }
    await new Promise((resolve) => setTimeout(resolve, 5 + Math.random() * 20));
  }
  return { ok: false, holder: await readLock(root) };
}

export async function updateLock(root, match, changes) {
  const current = await readLock(root);
  if (current && Object.entries(match).every(([key, value]) => current[key] === value)) {
    await writeJsonAtomic(lockFile(root), { ...current, ...changes });
  }
}

// Removes the lock only if `match` still holds it.
export async function releaseLock(root, match) {
  const current = await readLock(root);
  if (current && Object.entries(match).every(([key, value]) => current[key] === value)) {
    await rm(lockFile(root), { force: true });
  }
}

// ---- lifecycle ------------------------------------------------------------------

// request: { scenarios: [ids], repeat, build, audit?: { preset } }, already
// validated. With audit, the runner writes an audit run when it finishes.
// Returns { job } or { busy: holder }.
export async function startJob(root, request, { cliPath, target = null, nodePath = process.execPath, env = process.env }) {
  const jobId = makeRunId("job");
  const lock = await acquireLock(root, { owner: "job", jobId, pid: null });
  if (!lock.ok) return { busy: lock.holder };
  const dir = path.join(jobsDir(root), jobId);
  await mkdir(dir, { recursive: true });
  const job = setStatus({
    schema: JOB_SCHEMA,
    schemaVersion: JOB_SCHEMA_VERSION,
    jobId,
    kind: request.audit ? "audit" : "ui-test",
    createdAt: new Date().toISOString(),
    request,
    target,
    plan: { scenarios: request.scenarios, repeat: request.repeat, build: request.build, audit: request.audit ?? null, totalRuns: request.scenarios.length * request.repeat },
    pid: null,
    current: null,
    runs: [],
    buildRunId: null,
    auditRunId: null,
    cancelRequestedAt: null,
    error: null,
    exitCode: null,
    finishedAt: null,
  }, "queued");
  await writeJob(root, job);

  let child;
  const log = openSync(path.join(dir, "job.log"), "a");
  try {
    child = spawn(nodePath, [cliPath, "job-run", jobId], {
      detached: true,
      stdio: ["ignore", log, log],
      env: { ...env, ARCHIVIST_OBSERVATORY_DIR: root },
    });
    child.on("error", () => {});
  } finally {
    closeSync(log);
  }
  if (!child?.pid) {
    setStatus(job, "failed", { error: "The test runner process could not be started." });
    await writeJob(root, job);
    await releaseLock(root, { jobId });
    return { job };
  }
  child.unref();
  // The runner may already have moved the job on; only fill in the pid.
  const latest = (await readJob(root, jobId))?.job ?? job;
  latest.pid ??= child.pid;
  if (latest.status === "queued") setStatus(latest, "starting");
  await writeJob(root, latest);
  await updateLock(root, { jobId }, { pid: child.pid });
  return { job: latest };
}

// The job as it really is: an active job whose process is gone is marked
// interrupted (and its lock freed) instead of being reported as running.
export async function refreshJob(root, jobId) {
  const loaded = await readJob(root, jobId);
  if (!loaded?.job) return loaded;
  const { job } = loaded;
  if (!ACTIVE.has(job.status)) return loaded;
  const young = Date.now() - Date.parse(job.createdAt ?? 0) < START_GRACE_MS;
  if (!job.pid && young) return loaded;
  if (job.pid && !(await runnerStopped(job.pid, job.jobId))) return loaded;
  // The runner saves its final status just before it exits, so read the
  // record again: it may have finished while we were checking.
  const latest = await readJob(root, jobId);
  if (!latest?.job || !ACTIVE.has(latest.job.status)) return latest;
  if (latest.job.updatedAt !== job.updatedAt) return refreshJob(root, jobId);
  setStatus(job, "interrupted", {
    error: "The test runner stopped without finishing (it was killed, or the computer slept or restarted). Runs it recorded are kept.",
    current: null,
  });
  await writeJob(root, job);
  await releaseLock(root, { jobId: job.jobId });
  return { job };
}

// Asks the job's runner to stop. The runner stops the app it started (and
// only that), records the run as interrupted and marks the job cancelled.
export async function cancelJob(root, jobId) {
  const loaded = await refreshJob(root, jobId);
  if (!loaded) return { error: "not-found" };
  if (!loaded.job) return { error: "unreadable-record", message: loaded.error };
  const { job } = loaded;
  if (!ACTIVE.has(job.status)) return { error: "not-running", job };
  // Re-read so a status the runner saved a moment ago isn't overwritten.
  const fresh = (await readJob(root, jobId))?.job;
  if (fresh && !ACTIVE.has(fresh.status)) return { error: "not-running", job: fresh };
  Object.assign(job, fresh ?? {});
  job.cancelRequestedAt ??= new Date().toISOString();
  await writeJob(root, job);
  if (job.pid && (await ownsProcess(job.pid, job.jobId))) {
    process.kill(job.pid, "SIGINT");
  }
  return { job };
}

// ---- progress -------------------------------------------------------------------

// What the running scenario has done so far, from the app's own ui-events.json
// (the in-app driver rewrites it after every action). Nothing is estimated.
export async function runProgress(runDir) {
  let events;
  try {
    events = JSON.parse(await readFile(path.join(runDir, "ui-events.json"), "utf8"));
  } catch {
    return { actionsDone: null, actionsPlanned: null, lastAction: null, driverStatus: null };
  }
  const actions = Array.isArray(events.actions) ? events.actions : [];
  const last = actions.at(-1);
  return {
    actionsDone: actions.length,
    actionsPlanned: typeof events.plannedActions === "number" ? events.plannedActions : null,
    driverStatus: events.status ?? null,
    lastAction: last ? { index: last.index, phase: last.phase, op: last.op, label: last.label ?? null, ok: last.ok !== false, settledMs: last.settledMs ?? null, error: last.error ?? null } : null,
  };
}
