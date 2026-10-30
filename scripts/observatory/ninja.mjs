// Reads Ninja's own output and log. Counts from console output are marked
// "inferred"; durations from .ninja_log are Ninja's own measurements.
import { open, stat } from "node:fs/promises";
import path from "node:path";

const PROGRESS = /^\[(\d+)\/(\d+)\]/;
const NO_WORK = /^ninja: no work to do\.?$/;

// Watches `cmake --build` output for Ninja progress lines.
export function progressAnalyzer() {
  let finished = 0;
  let planned = null;
  let noWork = false;
  return {
    line(text) {
      const match = text.match(PROGRESS);
      if (match) {
        finished = Math.max(finished, Number(match[1]));
        planned = Math.max(planned ?? 0, Number(match[2]));
      } else if (NO_WORK.test(text.trim())) {
        noWork = true;
      }
    },
    details() {
      return {
        ninjaSteps: {
          basis: "inferred-from-output",
          highestReported: noWork ? 0 : finished,
          planned: noWork ? 0 : planned,
          noWorkToDo: noWork,
        },
      };
    },
  };
}

// Size of .ninja_log before a build, so only this build's lines are read.
export async function ninjaLogSize(file) {
  try {
    return (await stat(file)).size;
  } catch {
    return null;
  }
}

// Steps CMake and Qt's AUTOMOC run on every build, even when no source
// changed: glob re-checks, build.ninja regeneration, autogen timestamps and
// metatype file lists. A step counts as bookkeeping only if all of its
// outputs match; a real moc or compile step produces other outputs.
const BOOKKEEPING_OUTPUTS = [
  /(^|\/)cmake\.verify_globs$/,
  /(^|\/)build\.ninja$/,
  /_autogen\/timestamp$/,
  /_autogen\/mocs_compilation\.cpp$/,
  /(^|\/)meta_types\/[^/]+_json_file_list\.txt(\.timestamp)?$/,
];
const BOOKKEEPING_DESCRIPTIONS = [/^Re-checking globbed directories/, /^Re-running CMake/];

export function isBookkeepingDescription(description) {
  return BOOKKEEPING_DESCRIPTIONS.some((pattern) => pattern.test(description));
}

// One line per output: start, end, restat mtime, output path, command hash.
export function parseNinjaLogLines(text) {
  const entries = [];
  for (const line of text.split("\n")) {
    if (!line || line.startsWith("#")) {
      continue;
    }
    const [start, end, , output, hash = ""] = line.split("\t");
    const startMs = Number(start);
    const endMs = Number(end);
    if (output && Number.isFinite(startMs) && Number.isFinite(endMs) && endMs >= startMs) {
      entries.push({ output, startMs, endMs, hash });
    }
  }
  return entries;
}

// A step with several outputs is logged once per output (sometimes with
// both relative and absolute paths); group them back into one step.
export function groupSteps(entries, buildDir = "") {
  const prefix = buildDir ? `${buildDir.replace(/\/$/, "")}/` : null;
  const steps = new Map();
  for (const entry of entries) {
    const output = prefix && entry.output.startsWith(prefix) ? entry.output.slice(prefix.length) : entry.output;
    const key = `${entry.startMs}\t${entry.endMs}\t${entry.hash}`;
    if (!steps.has(key)) {
      steps.set(key, { outputs: new Set(), durationMs: entry.endMs - entry.startMs });
    }
    steps.get(key).outputs.add(output);
  }
  return [...steps.values()].map((step) => {
    const outputs = [...step.outputs];
    return {
      output: outputs[0],
      outputCount: outputs.length,
      durationMs: step.durationMs,
      bookkeeping: outputs.every((output) => BOOKKEEPING_OUTPUTS.some((pattern) => pattern.test(output))),
    };
  });
}

export function summarizeNinjaSteps(steps, top = 10) {
  const work = steps.filter((step) => !step.bookkeeping);
  return {
    basis: "measured-by-ninja",
    stepsRecorded: steps.length,
    bookkeepingSteps: steps.length - work.length,
    workSteps: work.length,
    sumOfStepDurationsMs: steps.reduce((sum, step) => sum + step.durationMs, 0),
    slowestSteps: [...work]
      .sort((a, b) => b.durationMs - a.durationMs)
      .slice(0, top)
      .map(({ output, outputCount, durationMs }) => ({ output, outputCount, durationMs })),
  };
}

// Reads the lines Ninja appended to .ninja_log since `before`. If the log
// shrank (Ninja recompacts it now and then) the entries can't be told
// apart, so nothing is claimed. Paths are made relative to the build dir.
export async function readNinjaLogSince(file, before, buildDir = path.dirname(file)) {
  const after = await ninjaLogSize(file);
  if (after === null) {
    return { basis: "unavailable", reason: ".ninja_log not found" };
  }
  const offset = before ?? 0;
  if (after < offset) {
    return { basis: "unavailable", reason: ".ninja_log was recompacted during the build" };
  }
  const handle = await open(file, "r");
  try {
    const buffer = Buffer.alloc(after - offset);
    await handle.read(buffer, 0, buffer.length, offset);
    return summarizeNinjaSteps(groupSteps(parseNinjaLogLines(buffer.toString("utf8")), buildDir));
  } finally {
    await handle.close();
  }
}
