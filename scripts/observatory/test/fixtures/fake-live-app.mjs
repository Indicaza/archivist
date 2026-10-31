// TEST FIXTURE ONLY: stands in for a running Archivist's live recorder.
// Serves POST /observatory/api/live/<status|start|stop|mark> on 127.0.0.1
// with the app's envelope and error codes, checks the agent key, and writes
// <root>/live/agent-access.json like the app does when agent access is on.
// On stop it saves a copy of the fixture recording as the new run.
import { randomBytes } from "node:crypto";
import { mkdir, rm, writeFile } from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import { liveFixtures, LIVE_FIXTURE_ID } from "./live-runs.mjs";
import { writeFixtureRuns } from "./dashboard-runs.mjs";

const DURATIONS = [15, 30, 60, 120];

// finalizeMs: how long "finalizing" lasts; outcome: the saved status
// (completed, failed, interrupted); limitMs: stop by "time-limit" after this
// long (the real app uses the recording's duration).
export async function startFakeLiveApp(root, { pid = process.pid, writeAccess = true, finalizeMs = 50, outcome = "completed", limitMs = null } = {}) {
  const token = randomBytes(24).toString("base64url");
  const calls = [];
  let state = "idle";
  let current = null;
  let lastRun = null;
  let counter = 0;

  const status = () => {
    const result = { state, recording: state === "recording", allowedDurations: DURATIONS, defaultDuration: 30, lastRun };
    if (current) {
      const elapsedMs = Date.now() - current.startedAt;
      Object.assign(result, { runId: current.runId, label: current.label, startedBy: "agent", durationSeconds: current.durationSeconds });
      if (state === "recording") Object.assign(result, { elapsedMs, remainingMs: Math.max(0, current.durationSeconds * 1000 - elapsedMs), frames: Math.round(elapsedMs / 16), events: current.marks, windows: 1 });
    }
    return result;
  };
  const failure = (code, message) => ({ error: { code, message } });

  // Like the app: "finalizing" until the files are on disk, then idle with lastRun.
  const finish = async (reason) => {
    const run = current;
    const { summary, files } = liveFixtures()[LIVE_FIXTURE_ID];
    const saved = structuredClone(summary);
    Object.assign(saved, { runId: run.runId, label: run.label ?? null, createdAt: new Date(run.startedAt).toISOString(), status: outcome });
    saved.live = { ...saved.live, stopReason: reason, startedBy: "agent", requestedDurationS: run.durationSeconds };
    await writeFixtureRuns(root, { [run.runId]: { summary: saved, files } });
    lastRun = { runId: run.runId, status: outcome, stopReason: reason, finishedAt: new Date().toISOString(), error: outcome === "failed" ? "fixture: write failed" : null };
    state = "idle";
    current = null;
  };
  const stopNow = (reason) => {
    clearTimeout(current.limit);
    state = "finalizing";
    setTimeout(() => finish(reason), finalizeMs);
  };

  const handle = async (operation, input) => {
    switch (operation) {
      case "status":
        return status();
      case "start": {
        if (state !== "idle") return failure("busy", "A live recording is already running.");
        const durationSeconds = input.durationSeconds ?? 30;
        if (!DURATIONS.includes(durationSeconds)) return failure("invalid-input", "durationSeconds must be 15, 30, 60 or 120.");
        counter += 1;
        current = { runId: `20261030T18000${counter % 10}-000Z-live-${String(counter).padStart(6, "a")}`, label: input.label ?? null, durationSeconds, startedAt: Date.now(), marks: 0 };
        state = "recording";
        if (limitMs != null) current.limit = setTimeout(() => { if (state === "recording") stopNow("time-limit"); }, limitMs);
        return status();
      }
      case "stop": {
        if (state !== "recording") return failure("not-recording", "No live recording is running.");
        stopNow("agent");
        return status();
      }
      case "mark": {
        if (state !== "recording") return failure("not-recording", "No live recording is running.");
        current.marks += 1;
        return { ...status(), markedAtMs: Date.now() - current.startedAt };
      }
      default:
        return failure("unknown-operation", "Live operations are status, start, stop and mark.");
    }
  };

  const server = http.createServer((request, response) => {
    let body = "";
    request.on("data", (chunk) => { body += chunk; });
    request.on("end", async () => {
      const operation = request.url.replace("/observatory/api/live/", "");
      calls.push({ operation, headers: request.headers, body });
      const send = (status, value) => {
        response.writeHead(status, { "Content-Type": "application/json" });
        response.end(JSON.stringify(value));
      };
      if (request.method !== "POST" || !request.url.startsWith("/observatory/api/live/")) return send(404, { error: "Not found." });
      const envelope = (result) => ({ schema: "archivist.observatory.response", schemaVersion: 1, operation: `observatory.live.${operation}`, ...result });
      if (request.headers["x-observatory-agent-token"] !== token) return send(401, envelope({ ok: false, error: { code: "unauthorized", message: "Wrong or missing agent key." } }));
      let input;
      try {
        input = body ? JSON.parse(body) : {};
      } catch {
        return send(400, envelope({ ok: false, error: { code: "invalid-input", message: "Not JSON." } }));
      }
      const outcome = await handle(operation, input);
      if (outcome.error) return send(outcome.error.code === "invalid-input" ? 400 : 409, envelope({ ok: false, error: outcome.error }));
      return send(200, envelope({ ok: true, result: outcome }));
    });
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const endpoint = `http://127.0.0.1:${server.address().port}/observatory/api/live/`;
  const file = path.join(root, "live", "agent-access.json");
  if (writeAccess) {
    await mkdir(path.dirname(file), { recursive: true });
    await rm(file, { force: true });
    await writeFile(file, JSON.stringify({ schema: "archivist.observatory.live-access", endpoint, token, pid, enabledAt: new Date().toISOString() }), { mode: 0o600 });
  }
  return {
    endpoint,
    token,
    calls,
    file,
    get state() { return state; },
    async close() {
      server.closeAllConnections();
      await new Promise((resolve) => server.close(resolve));
      await rm(file, { force: true });
    },
  };
}
