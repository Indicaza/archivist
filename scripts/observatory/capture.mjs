// Runs build phases as subprocesses and measures them with a monotonic
// clock. Knows nothing about Archivist: phases are plain command lists.
import { spawn } from "node:child_process";

const ERROR_LINE = /(^FAILED:|\berror\b|CMake Error|npm ERR!)/i;
const SIGNAL_NUMBERS = { SIGHUP: 1, SIGINT: 2, SIGTERM: 15 };

export function nowMs() {
  return Number(process.hrtime.bigint()) / 1e6;
}

function round(ms) {
  return Math.round(ms * 10) / 10;
}

// Splits a stream into lines, calling onLine for each complete one.
function lineSplitter(onLine) {
  let pending = "";
  return {
    push(chunk) {
      pending += chunk;
      const lines = pending.split(/\r?\n/);
      pending = lines.pop();
      lines.forEach(onLine);
    },
    flush() {
      if (pending) {
        onLine(pending);
        pending = "";
      }
    },
  };
}

// Runs one command, teeing stdout/stderr into the log. Resolves (never
// rejects) with the outcome, including the child's own exit code or signal.
export function runPhase(phase, { log, echo = false, origin = nowMs(), onSpawn = () => {} }) {
  return new Promise((resolve) => {
    const analyzer = phase.analyze ? phase.analyze() : null;
    const tail = [];
    const errorLines = [];
    let context = 0;
    const started = nowMs();
    const display = phase.display ?? [phase.command, ...(phase.args ?? [])].join(" ");
    log.write(`\n===== ${phase.name} =====\n$ ${display}\n\n`);

    const onLine = (line) => {
      tail.push(line);
      if (tail.length > 20) {
        tail.shift();
      }
      // Keep each error line plus a few after it (CMake explains below).
      if (errorLines.length < 20 && ERROR_LINE.test(line)) {
        errorLines.push(line);
        context = 3;
      } else if (context > 0 && errorLines.length < 20 && line.trim()) {
        errorLines.push(line);
        context -= 1;
      }
      analyzer?.line(line);
    };
    const out = lineSplitter(onLine);

    const finish = (fields) => {
      out.flush();
      const durationMs = round(nowMs() - started);
      log.write(`\n----- ${phase.name}: ${fields.status} in ${durationMs} ms`
        + `${fields.exitCode != null ? ` (exit ${fields.exitCode})` : ""}`
        + `${fields.signal ? ` (signal ${fields.signal})` : ""} -----\n`);
      const result = {
        name: phase.name,
        command: display,
        status: fields.status,
        exitCode: fields.exitCode ?? null,
        signal: fields.signal ?? null,
        startOffsetMs: round(started - origin),
        durationMs,
        details: analyzer ? analyzer.details() : {},
      };
      if (fields.status !== "success") {
        result.error = {
          message: fields.message,
          excerpt: (errorLines.length ? errorLines.slice(0, 8) : tail.slice(-10)),
        };
      }
      resolve(result);
    };

    let child;
    try {
      child = spawn(phase.command, phase.args ?? [], {
        cwd: phase.cwd,
        env: phase.env ?? process.env,
        stdio: ["ignore", "pipe", "pipe"],
      });
    } catch (error) {
      finish({ status: "failure", message: `Could not start: ${error.message}` });
      return;
    }
    onSpawn(child);

    for (const stream of [child.stdout, child.stderr]) {
      stream.setEncoding("utf8");
      stream.on("data", (chunk) => {
        log.write(chunk);
        if (echo) {
          process.stdout.write(chunk);
        }
        out.push(chunk);
      });
    }

    let spawnError = null;
    child.on("error", (error) => {
      spawnError = error;
    });
    child.on("close", (code, signal) => {
      if (spawnError) {
        finish({ status: "failure", message: `Could not start: ${spawnError.message}` });
      } else if (code === 0) {
        finish({ status: "success", exitCode: 0 });
      } else if (signal) {
        finish({ status: "interrupted", signal, message: `Stopped by ${signal}.` });
      } else {
        finish({ status: "failure", exitCode: code, message: `Exited with code ${code}.` });
      }
    });
  });
}

// Runs phases in order, stopping at the first that does not succeed.
// `onProgress(state)` is called after every phase so partial results can be
// saved; `onPhaseDone(result)` lets the caller print progress.
// A SIGINT/SIGTERM/SIGHUP marks the run interrupted, forwards the signal to
// the running child and lets it finish before returning.
export async function runCapture(phases, {
  log,
  echo = false,
  onProgress = async () => {},
  onPhaseDone = () => {},
  signals = ["SIGINT", "SIGTERM", "SIGHUP"],
} = {}) {
  const origin = nowMs();
  const state = { status: "running", phases: [], interruptedBy: null };
  let current = null;

  const handlers = signals.map((name) => {
    const handler = () => {
      state.interruptedBy ??= name;
      if (current && current.exitCode === null && current.signalCode === null) {
        current.kill(name);
      }
    };
    process.on(name, handler);
    return [name, handler];
  });

  try {
    for (const phase of phases) {
      if (state.interruptedBy || state.status !== "running") {
        state.phases.push({ name: phase.name, status: "skipped" });
        continue;
      }
      const context = { log, echo, origin, onSpawn: (child) => { current = child; } };
      const result = await (phase.run ? phase.run(context, state) : runPhase(phase, context));
      current = null;
      if (state.interruptedBy && result.status !== "success") {
        result.status = "interrupted";
      }
      state.phases.push(result);
      onPhaseDone(result);
      if (result.status !== "success") {
        state.status = result.status;
      }
      state.totalDurationMs = round(nowMs() - origin);
      await onProgress(state);
    }
  } finally {
    handlers.forEach(([name, handler]) => process.off(name, handler));
  }

  if (state.interruptedBy) {
    state.status = "interrupted";
  } else if (state.status === "running") {
    state.status = "success";
  }
  state.totalDurationMs = round(nowMs() - origin);
  return state;
}

// The exit code the CLI should return so callers see the build's own result.
export function exitCodeFor(state) {
  if (state.status === "success") {
    return 0;
  }
  if (state.status === "interrupted") {
    return 128 + (SIGNAL_NUMBERS[state.interruptedBy] ?? SIGNAL_NUMBERS.SIGINT);
  }
  const failed = state.phases.find((phase) => phase.status === "failure");
  return failed?.exitCode || 1;
}
