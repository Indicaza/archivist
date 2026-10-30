// Reads Ninja's own output and log. Counts from console output are marked
// "inferred"; durations from .ninja_log are Ninja's own measurements.
import { open, stat } from "node:fs/promises";

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

// One entry per line: start, end, restat mtime, output path, hash.
export function parseNinjaLogLines(text) {
  const entries = [];
  for (const line of text.split("\n")) {
    if (!line || line.startsWith("#")) {
      continue;
    }
    const [start, end, , output] = line.split("\t");
    const startMs = Number(start);
    const endMs = Number(end);
    if (output && Number.isFinite(startMs) && Number.isFinite(endMs) && endMs >= startMs) {
      entries.push({ output, durationMs: endMs - startMs });
    }
  }
  return entries;
}

export function summarizeNinjaEntries(entries, top = 10) {
  const slowest = [...entries].sort((a, b) => b.durationMs - a.durationMs).slice(0, top);
  return {
    basis: "measured-by-ninja",
    edgesRecorded: entries.length,
    sumOfEdgeDurationsMs: entries.reduce((sum, entry) => sum + entry.durationMs, 0),
    slowestEdges: slowest,
  };
}

// Reads the lines Ninja appended to .ninja_log since `before`. If the log
// shrank (Ninja recompacts it now and then) the entries can't be told
// apart, so nothing is claimed.
export async function readNinjaLogSince(file, before) {
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
    return summarizeNinjaEntries(parseNinjaLogLines(buffer.toString("utf8")));
  } finally {
    await handle.close();
  }
}
