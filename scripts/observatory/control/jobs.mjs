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
import { mkdir, readdir, readFile, rm, writeFile } from "node:fs/promises";
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
    execFile("ps", ["-o", "command=", "-p", String(pid)], { timeout: 5000 }, (error, stdout) => resolve(error ? null : stdout.trim()));
  });
}

// True only for a live process that is this job's runner. A pid alone could
// have been reused by an unrelated program after the runner exited.
export async function ownsProcess(pid, jobId) {
  if (!isAlive(pid)) return false;
  const command = await processCommand(pid);
  return Boolean(command && command.includes("job-run") && command.includes(jobId));
}

// ---- the one-UI-test-at-a-time lock -------------------------------------------

async function readLock(root) {
  try {
    return JSON.parse(await readFile(lockFile(root), "utf8"));
  } catch {
    return null;
  }
}

async function lockIsStale(holder) {
  if (!holder) return true;
  if (!holder.pid) return Date.now() - Date.parse(holder.since ?? 0) > START_GRACE_MS;
  if (holder.owner === "job") return !(await ownsProcess(holder.pid, holder.jobId));
  return !isAlive(holder.pid);
}

// holder: { owner: "job" | "cli", jobId?, pid, since }. Returns { ok } or
// { ok: false, holder } when another UI test is running.
export async function acquireLock(root, holder) {
  await mkdir(jobsDir(root), { recursive: true });
  const value = { ...holder, since: holder.since ?? new Date().toISOString() };
  for (let attempt = 0; attempt < 2; attempt += 1) {
    try {
      await writeFile(lockFile(root), `${JSON.stringify(value, null, 2)}\n`, { flag: "wx" });
      return { ok: true };
    } catch (error) {
      if (error.code !== "EEXIST") throw error;
    }
    const current = await readLock(root);
    if (!(await lockIsStale(current))) return { ok: false, holder: current };
    await rm(lockFile(root), { force: true });
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

// request: { scenarios: [ids], repeat, build }, already validated.
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
    kind: "ui-test",
    createdAt: new Date().toISOString(),
    request,
    target,
    plan: { scenarios: request.scenarios, repeat: request.repeat, build: request.build, totalRuns: request.scenarios.length * request.repeat },
    pid: null,
    current: null,
    runs: [],
    buildRunId: null,
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
  if (job.pid && (await ownsProcess(job.pid, job.jobId))) return loaded;
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
