// The live recorder's operations (observatory.live.*): the same abilities the
// Live page gives a person, for agents. status/start/stop/mark/wait reach the
// running app through control/live.mjs (only while the person has turned on
// agent access); list/get/spikes/window/compare read recordings already on
// disk and work any time.
//
// operations.mjs owns the envelope, validation and error type; it passes them
// in here so the two modules don't import each other.
import { listRunIds, loadRunsById } from "../dashboard/records.mjs";
import { compareLive, liveModel, LONG_GAP_MS, operationSummary, rangeDetail, spikesWithContext, summarySentence, headline, windowLabel } from "../dashboard/public/live.mjs";
import { loadRunEvidence } from "./evidence.mjs";
import { callLive, LiveAccessError } from "./live.mjs";

export const LIVE_DURATIONS = [15, 30, 60, 120];
export const MAX_WINDOW_MS = 60_000;
export const MAX_WAIT_SECONDS = 300;
// What an agent may put in a label or note: no newlines or control characters.
const LABEL = "^[^\\u0000-\\u001f\\u007f]{0,64}$";

export const LIVE_ERROR_CODES = {
  "not-recording": "No live recording is running.",
  "agent-access-off": "The person hasn't turned on \"Let agents record\" in Archivist's Observatory > Live (or Archivist isn't running).",
  "app-not-running": "The Archivist that turned on agent access has quit.",
  "app-unreachable": "Archivist didn't answer on its local address.",
  "io-error": "The app couldn't create or write the recording's folder.",
  "timed-out": "The recording was still running when the wait ran out.",
  unavailable: "The recorder isn't available in this app.",
};

export const LIVE_HTTP_STATUS = {
  "not-recording": 409, "agent-access-off": 409, "app-not-running": 409, "app-unreachable": 502, "timed-out": 504, "io-error": 500, unavailable: 503,
};

const isBusy = (status) => status.recording === true || status.state === "recording" || status.state === "finalizing";
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export function liveOperations({ ControlError, requireRun, runIdPattern }) {
  const app = async (ctx, op, input = {}) => {
    try {
      return await callLive(ctx.root, op, input, ctx.liveOptions ?? {});
    } catch (error) {
      if (error instanceof LiveAccessError) throw new ControlError(error.code, error.message);
      throw error;
    }
  };

  async function loadModel(ctx, runId) {
    const loaded = await requireRun(ctx, runId);
    if (loaded.summary.kind !== "live-session") throw new ControlError("invalid-input", `Run ${runId} is a ${loaded.summary.kind ?? "unknown"} run, not a live recording.`);
    const evidence = await loadRunEvidence(ctx.root, runId);
    return liveModel({ runId, summary: loaded.summary, metrics: evidence.live?.metrics ?? null, events: evidence.live?.events ?? null });
  }

  function overview(model, summary) {
    return {
      runId: model.runId,
      status: model.status,
      label: summary.label ?? null,
      createdAt: summary.createdAt ?? null,
      startedBy: model.startedBy,
      stopReason: model.stopReason,
      partial: model.partial,
      requestedDurationS: model.requestedDurationS,
      durationMs: model.durationMs,
      measurementVersion: model.measurementVersion,
      summary: summarySentence(model),
      headline: headline(model),
      windows: model.windows.map((window) => ({ ...window, label: windowLabel(model, window.id) })),
      spikes: model.spikes.length,
      hasMetrics: model.hasMetrics,
      hasEvents: model.hasEvents,
    };
  }

  const runIdInput = { type: "string", pattern: runIdPattern, required: true };

  return {
    "observatory.live.status": {
      description: "Whether the running Archivist's live recorder can be reached, and if so its state (idle or recording, elapsed time, the recording's run id). Never fails because agent access is off: it says so instead.",
      cli: "npm run observatory -- live status --json",
      input: {},
      sideEffects: "none",
      async run(ctx) {
        try {
          return { available: true, ...(await app(ctx, "status")) };
        } catch (error) {
          if (["agent-access-off", "app-not-running", "app-unreachable", "unreadable-record"].includes(error.code)) {
            return { available: false, reason: error.code, message: error.message };
          }
          throw error;
        }
      },
    },
    "observatory.live.start": {
      description: "Start a live recording in the running Archivist (needs agent access on). It always stops itself at the duration; one recording at a time; the app shows that it is recording.",
      cli: "npm run observatory -- live start [--duration 15|30|60|120] [--label <text>] --json",
      input: {
        durationSeconds: { type: "integer", enum: LIVE_DURATIONS, default: 30, description: "How long to record; the recorder stops by itself then." },
        label: { type: "string", pattern: LABEL, description: "A short name for the recording (≤64 characters)." },
      },
      sideEffects: "The running Archivist starts recording frame swaps, UI-thread delays, CPU/memory and instrumented operations into a new live-session run. Nothing is changed in the app. A red Recording indicator shows in its top bar.",
      async run(ctx, input) {
        return app(ctx, "start", input);
      },
    },
    "observatory.live.stop": {
      description: "Stop the running live recording now and save it.",
      cli: "npm run observatory -- live stop --json",
      input: {},
      sideEffects: "Ends the recording; its run is saved with stopReason agent.",
      async run(ctx) {
        return app(ctx, "stop");
      },
    },
    "observatory.live.mark": {
      description: "Add a note at this moment of the running recording (e.g. \"opening the big project now\").",
      cli: "npm run observatory -- live mark [<note>] --json",
      input: { label: { type: "string", pattern: LABEL, description: "What you noticed (optional; \"marker\" if left out)." } },
      sideEffects: "Adds a note to the running recording.",
      async run(ctx, input) {
        return app(ctx, "mark", input);
      },
    },
    "observatory.live.wait": {
      description: "Wait until the running recording ends (by its time limit, a stop, an error or a quit), then return the saved recording's overview. Called while idle, it returns the last recording (waited: false), or recording: null if there has been none since Archivist started.",
      cli: "npm run observatory -- live wait [--timeout <seconds>] --json",
      input: { timeoutSeconds: { type: "integer", min: 1, max: MAX_WAIT_SECONDS, default: 150 } },
      sideEffects: "none",
      async run(ctx, input) {
        const deadline = Date.now() + input.timeoutSeconds * 1000;
        let status = await app(ctx, "status");
        // The recording this call waits for: the running one, else the last one.
        const runId = status.runId ?? null;
        const waited = isBusy(status);
        while (isBusy(status) && (status.runId ?? runId) === runId) {
          if (Date.now() > deadline) {
            throw new ControlError("timed-out", `Recording ${runId} was still ${status.state} after ${input.timeoutSeconds} s. It stops by itself at its length; call wait again.`, { runId, state: status.state });
          }
          await sleep(ctx.livePollMs ?? 500);
          status = await app(ctx, "status");
        }
        // lastRun is the app's record of the last recording: { runId, status, stopReason, finishedAt, error }.
        const last = status.lastRun && typeof status.lastRun === "object" ? status.lastRun : null;
        const finished = runId ?? (typeof last?.runId === "string" ? last.runId : null);
        if (!finished) return { waited: false, status, recording: null, message: "No recording has been made since Archivist started." };
        if (last?.runId === finished && last.status === "failed") {
          // Still read what was saved: a failed recording keeps its stream.
          const loaded = await requireRun(ctx, finished).catch(() => null);
          return { waited, status, recording: loaded ? overview(await loadModel(ctx, finished), loaded.summary) : null, failed: { runId: finished, error: last.error ?? null } };
        }
        const loaded = await requireRun(ctx, finished);
        return { waited, status, recording: overview(await loadModel(ctx, finished), loaded.summary) };
      },
    },
    "observatory.live.list": {
      description: "Saved live recordings, newest first, with headline figures.",
      cli: "npm run observatory -- live list [--limit <n>] --json",
      input: { limit: { type: "integer", min: 1, max: 200, default: 20 } },
      sideEffects: "none",
      async run(ctx, input) {
        // Live recordings' folders are named <time>-live-<hex> (the app's
        // recovery relies on the same), so only those summaries are read.
        const ids = (await listRunIds(ctx.root)).filter((id) => id.includes("-live-"));
        const runs = (await loadRunsById(ctx.root, ids)).filter((run) => run.kind === "live-session");
        return {
          total: runs.length,
          recordings: runs.slice(0, input.limit).map((run) => ({ runId: run.runId, status: run.status, createdAt: run.createdAt, label: run.label, problems: run.problems ?? [], ...(run.live ?? {}) })),
        };
      },
    },
    "observatory.live.get": {
      description: "One recording: overview, evidence-only summary, statistics, instrumented operations and the worst moments with what happened around them.",
      cli: "npm run observatory -- live get <run-id> --json",
      input: { runId: runIdInput },
      sideEffects: "none",
      async run(ctx, input) {
        const loaded = await requireRun(ctx, input.runId);
        const model = await loadModel(ctx, input.runId);
        return {
          ...overview(model, loaded.summary),
          stats: model.stats,
          operations: operationSummary(model),
          notes: model.events.filter((event) => event.kind === "annotation").map((event) => ({ atMs: event.atMs, label: event.name })),
          worst: spikesWithContext(model, { limit: 5 }),
          artifacts: loaded.summary.artifacts ?? {},
        };
      },
    },
    "observatory.live.spikes": {
      description: `The notable moments of a recording (frame gaps between steady frames, and event-loop stalls of ${LONG_GAP_MS} ms or more), worst first, each with the operations it occurred during or near. Correlation only.`,
      cli: "npm run observatory -- live spikes <run-id> [--limit <n>] --json",
      input: { runId: runIdInput, limit: { type: "integer", min: 1, max: 100, default: 20 } },
      sideEffects: "none",
      async run(ctx, input) {
        const model = await loadModel(ctx, input.runId);
        return { runId: input.runId, total: model.spikes.length, spikes: spikesWithContext(model, { limit: input.limit }) };
      },
    },
    "observatory.live.window": {
      description: `Everything recorded between two times (ms since the recording started, at most ${MAX_WINDOW_MS / 1000} s apart): frames per window, UI-thread delays, CPU, memory, moments and operations.`,
      cli: "npm run observatory -- live window <run-id> --from <ms> --to <ms> --json",
      input: {
        runId: runIdInput,
        fromMs: { type: "integer", min: 0, max: 600_000, required: true },
        toMs: { type: "integer", min: 0, max: 600_000, required: true },
      },
      sideEffects: "none",
      async run(ctx, input) {
        if (input.toMs <= input.fromMs) throw new ControlError("invalid-input", "\"toMs\" must be after \"fromMs\".");
        if (input.toMs - input.fromMs > MAX_WINDOW_MS) throw new ControlError("invalid-input", `A window is at most ${MAX_WINDOW_MS} ms long.`);
        const model = await loadModel(ctx, input.runId);
        return { runId: input.runId, ...rangeDetail(model, input.fromMs, input.toMs) };
      },
    },
    "observatory.live.compare": {
      description: "Compare two recordings' headline figures (frame intervals, gaps and stalls per minute, CPU, memory growth). Refuses recordings with different measurement versions. Not a significance test.",
      cli: "npm run observatory -- live compare <base-run-id> <run-id> --json",
      input: { baseRunId: runIdInput, runId: runIdInput },
      sideEffects: "none",
      async run(ctx, input) {
        const [base, candidate] = await Promise.all([loadModel(ctx, input.baseRunId), loadModel(ctx, input.runId)]);
        return compareLive(base, candidate);
      },
    },
  };
}
