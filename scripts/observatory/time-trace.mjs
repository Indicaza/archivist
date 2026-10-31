// Opt-in compiler profiling: reads Clang's -ftime-trace files (one JSON per
// translation unit, Chrome trace format) and sums where the compiler spent
// its time: parsing which headers, instantiating which templates, frontend
// against backend.
//
// Clang only records events longer than its granularity (500 µs by
// default), and "Source" and "Instantiate*" times are inclusive (a header's
// time contains the headers it includes), so these are rankings, not a
// breakdown that adds up.
import { readFile } from "node:fs/promises";
import path from "node:path";

export const TIME_TRACE_SCHEMA = "archivist.observatory.time-trace";
export const TIME_TRACE_VERSION = 1;
const TOP = 25;

const ms = (us) => Math.round(us / 100) / 10;

// One translation unit's trace.
export function summarizeTrace(trace) {
  const events = Array.isArray(trace?.traceEvents) ? trace.traceEvents : [];
  const totals = {};
  const headers = new Map();
  const templates = new Map();
  for (const event of events) {
    if (event.ph !== "X" || typeof event.dur !== "number") continue;
    if (event.name?.startsWith("Total ")) {
      totals[event.name.slice(6)] = event.dur;
      continue;
    }
    const detail = event.args?.detail;
    if (!detail) continue;
    if (event.name === "Source") headers.set(detail, (headers.get(detail) ?? 0) + event.dur);
    else if (event.name === "InstantiateClass" || event.name === "InstantiateFunction") templates.set(detail, (templates.get(detail) ?? 0) + event.dur);
  }
  return {
    compileUs: totals.ExecuteCompiler ?? null,
    frontendUs: totals.Frontend ?? null,
    backendUs: totals.Backend ?? null,
    sourceUs: totals.Source ?? null,
    instantiateUs: (totals.InstantiateClass ?? 0) + (totals.InstantiateFunction ?? 0) || null,
    headers,
    templates,
  };
}

// Where Clang writes the trace for an object: the output path with its
// last extension replaced by .json (a.cpp.o -> a.cpp.json).
export function traceFileFor(buildDir, output) {
  return path.join(buildDir, output.replace(/\.(o|obj)$/, "") + ".json");
}

function top(map, units, relative) {
  return [...map.entries()]
    .sort((a, b) => b[1].us - a[1].us)
    .slice(0, TOP)
    .map(([name, value]) => ({ name: relative(name), totalMs: ms(value.us), units: value.units }));
}

// Sums the traces of these objects. `relative` shortens paths for display.
export async function summarizeTraces(buildDir, outputs, { relative = (name) => name, read = (file) => readFile(file, "utf8") } = {}) {
  const headers = new Map();
  const templates = new Map();
  const units = [];
  let missing = 0;
  let unreadable = 0;
  for (const output of outputs) {
    let text;
    try {
      text = await read(traceFileFor(buildDir, output));
    } catch {
      missing += 1;
      continue;
    }
    let trace;
    try {
      trace = JSON.parse(text);
    } catch {
      unreadable += 1;
      continue;
    }
    const unit = summarizeTrace(trace);
    units.push({ output, compileMs: unit.compileUs == null ? null : ms(unit.compileUs), frontendMs: unit.frontendUs == null ? null : ms(unit.frontendUs), backendMs: unit.backendUs == null ? null : ms(unit.backendUs), instantiateMs: unit.instantiateUs == null ? null : ms(unit.instantiateUs) });
    for (const [map, from] of [[headers, unit.headers], [templates, unit.templates]]) {
      for (const [name, us] of from) {
        const entry = map.get(name) ?? { us: 0, units: 0 };
        entry.us += us;
        entry.units += 1;
        map.set(name, entry);
      }
    }
  }
  const sum = (key) => units.reduce((total, unit) => total + (unit[key] ?? 0), 0);
  return {
    schema: TIME_TRACE_SCHEMA,
    schemaVersion: TIME_TRACE_VERSION,
    basis: "clang -ftime-trace (events over 500 µs; header and template times are inclusive)",
    units: units.length,
    missing,
    unreadable,
    totals: {
      compileMs: Math.round(sum("compileMs")),
      frontendMs: Math.round(sum("frontendMs")),
      backendMs: Math.round(sum("backendMs")),
      instantiateMs: Math.round(sum("instantiateMs")),
    },
    headers: top(headers, units.length, relative),
    templates: top(templates, units.length, (name) => name),
    slowestUnits: [...units].sort((a, b) => (b.compileMs ?? 0) - (a.compileMs ?? 0)).slice(0, TOP),
  };
}
