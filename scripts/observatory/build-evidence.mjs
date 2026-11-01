// Build evidence beyond phase times: every job Ninja ran (with its start and
// end, category and source), why Ninja decided to rebuild, which headers
// fan out to many objects, wall time against summed job time, a dependency
// chain through the jobs that ran, and compiler-cache counters.
//
// Everything here comes from Ninja's own records and tools:
//   .ninja_log          start/end of each job (Ninja's clock, ms)
//   ninja -d explain -n why each output is out of date (before the build)
//   ninja -t query      a job's rule and its inputs (the dependency graph)
//   ninja -t deps       the headers each object included (from the compiler)
//   ninja -t compdb     the source file of each compile
// Nothing is guessed: what a tool can't establish is reported as unknown.
import { spawn } from "node:child_process";
import { open, stat } from "node:fs/promises";
import path from "node:path";
import { parseNinjaLogLines } from "./ninja.mjs";

export const BUILD_EVIDENCE_SCHEMA = "archivist.observatory.build-evidence";
export const BUILD_EVIDENCE_VERSION = 1;
// Bounds: what is kept on disk and in summary.json.
export const MAX_JOBS = 20000;
export const MAX_EXPLAIN_LINES = 200000;
const TOP = 15;

// ---- running Ninja ---------------------------------------------------------

// Runs ninja with these arguments in the build directory and returns its
// stdout (null if it can't run or exits non-zero). Output is capped.
export function runNinja(buildDir, args, { program = "ninja", maxBytes = 256 * 1024 * 1024, timeoutMs = 120_000 } = {}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(program, ["-C", buildDir, ...args], { stdio: ["ignore", "pipe", "pipe"] });
    } catch {
      resolve(null);
      return;
    }
    const chunks = [];
    let size = 0;
    let truncated = false;
    child.stdout.on("data", (chunk) => {
      if (size + chunk.length > maxBytes) {
        truncated = true;
        child.kill("SIGTERM");
        return;
      }
      size += chunk.length;
      chunks.push(chunk);
    });
    child.stderr.resume();
    const timer = setTimeout(() => child.kill("SIGTERM"), timeoutMs);
    child.on("error", () => {
      clearTimeout(timer);
      resolve(null);
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve(code === 0 && !truncated ? Buffer.concat(chunks).toString("utf8") : null);
    });
  });
}

// ---- why did this rebuild? -----------------------------------------------------

const EXPLAIN = [
  [/^output (.+) older than most recent input (.+) \(\d+ vs \d+\)$/, (m) => ({ kind: "input-newer", output: m[1], input: m[2] })],
  [/^recorded mtime of (.+) older than most recent input (.+) \(\d+ vs \d+\)$/, (m) => ({ kind: "input-newer", output: m[1], input: m[2] })],
  [/^restat of output (.+) older than most recent input (.+) \(\d+ vs \d+\)$/, (m) => ({ kind: "input-newer", output: m[1], input: m[2] })],
  [/^output (.+) of phony edge with no inputs doesn't exist$/, (m) => ({ kind: "phony-missing", output: m[1] })],
  [/^output (.+) doesn't exist$/, (m) => ({ kind: "missing-output", output: m[1] })],
  [/^command line changed for (.+)$/, (m) => ({ kind: "command-changed", output: m[1] })],
  [/^deps for '(.+)' are missing$/, (m) => ({ kind: "deps-missing", output: m[1] })],
  [/^(.+) is dirty$/, (m) => ({ kind: "dirty", output: m[1] })],
];

// One "ninja explain: ..." line, or null.
export function parseExplainLine(line) {
  const text = line.match(/^ninja explain: (.*)$/)?.[1];
  if (!text) return null;
  for (const [pattern, make] of EXPLAIN) {
    const match = text.match(pattern);
    if (match) return make(match);
  }
  return { kind: "other", text };
}

// Collects explain lines from the dry run (plan phase). Bounded.
export function explainCollector() {
  const lines = [];
  let dropped = 0;
  return {
    line(text) {
      if (!text.startsWith("ninja explain: ")) return;
      if (lines.length < MAX_EXPLAIN_LINES) lines.push(text);
      else dropped += 1;
    },
    result() {
      return { lines, dropped };
    },
  };
}

function relativeTo(file, dirs) {
  for (const [dir, prefix] of dirs) {
    if (dir && file.startsWith(`${dir}/`)) return `${prefix}${file.slice(dir.length + 1)}`;
  }
  return file;
}

// What kind of file changed: source, header, generated (made by the build),
// CMake input, or other.
export function inputKind(file, { buildDir, generated = new Set() } = {}) {
  if (generated.has(file) || (buildDir && (file.startsWith(`${buildDir}/`) || !path.isAbsolute(file)))) return "generated";
  if (/(^|\/)CMakeLists\.txt$|\.cmake$/.test(file)) return "cmake";
  if (/\.(h|hh|hpp|hxx|inl|ipp)$/i.test(file)) return "header";
  if (/\.(c|cc|cpp|cxx|mm|m)$/i.test(file)) return "source";
  if (/\.(qml|js|mjs|qrc|ui|json|svg|png)$/i.test(file)) return "resource";
  return "other";
}

// Turns explain lines into rebuild causes. A cause is "confirmed" when
// Ninja named it for that output; outputs that are dirty only because an
// input of theirs is rebuilt are counted as "propagated".
export function analyzeExplain(lines, { buildDir = null, root = null, isCompile = () => false } = {}) {
  const events = lines.map(parseExplainLine).filter(Boolean);
  const dirty = new Set();
  const direct = new Map(); // output -> reasons
  const byInput = new Map(); // input -> outputs
  const commandChanged = [];
  const missing = [];
  const other = [];
  for (const event of events) {
    if (event.kind === "dirty") {
      dirty.add(event.output);
      continue;
    }
    if (event.kind === "phony-missing") continue;
    if (event.output) {
      if (!direct.has(event.output)) direct.set(event.output, []);
      direct.get(event.output).push(event);
    }
    if (event.kind === "input-newer") {
      if (!byInput.has(event.input)) byInput.set(event.input, new Set());
      byInput.get(event.input).add(event.output);
    } else if (event.kind === "command-changed") commandChanged.push(event.output);
    else if (event.kind === "missing-output") missing.push(event.output);
    else if (event.kind === "other") other.push(event.text);
  }
  // Inputs that are themselves rebuilt are links in a chain, not causes.
  const rebuilt = new Set([...dirty, ...direct.keys()]);
  const dirs = [[root, ""], [buildDir, "<build>/"]];
  const causes = [...byInput.entries()]
    .filter(([input]) => !rebuilt.has(input))
    .map(([input, outputs]) => {
      const list = [...outputs];
      return {
        input: relativeTo(input, dirs),
        inputPath: input,
        inputKind: inputKind(input, { buildDir }),
        basis: "ninja-explain",
        outputs: list.length,
        compiles: list.filter((output) => isCompile(output)).length,
        examples: list.slice(0, 8).map((output) => relativeTo(output, dirs)),
      };
    })
    .sort((a, b) => b.outputs - a.outputs || a.input.localeCompare(b.input));
  const explained = new Set(direct.keys());
  const propagated = [...dirty].filter((output) => !explained.has(output));
  return {
    basis: "ninja -d explain -n, before the build",
    dirtyOutputs: rebuilt.size,
    directlyExplained: explained.size,
    propagated: propagated.length,
    causes,
    commandLineChanged: { count: commandChanged.length, examples: commandChanged.slice(0, 8).map((o) => relativeTo(o, dirs)) },
    missingOutputs: { count: missing.length, examples: missing.slice(0, 8).map((o) => relativeTo(o, dirs)) },
    unrecognized: other.slice(0, 8),
  };
}

// ---- the dependency graph ------------------------------------------------------

// `ninja -t query a b ...`: each node's rule, inputs (explicit, | implicit,
// || order-only) and outputs.
export function parseNinjaQuery(text) {
  const nodes = new Map();
  let current = null;
  let section = null;
  for (const raw of text.split("\n")) {
    if (!raw.trim()) continue;
    if (!raw.startsWith(" ")) {
      current = { rule: null, inputs: [], implicit: [], orderOnly: [], outputs: [] };
      nodes.set(raw.replace(/:$/, ""), current);
      section = null;
      continue;
    }
    const line = raw.trim();
    const header = line.match(/^(input|outputs):\s*(.*)$/);
    if (header) {
      section = header[1];
      if (section === "input") current.rule = header[2] || null;
      continue;
    }
    if (!current || !section) continue;
    if (section === "outputs") current.outputs.push(line);
    else if (line.startsWith("|| ")) current.orderOnly.push(line.slice(3));
    else if (line.startsWith("| ")) current.implicit.push(line.slice(2));
    else current.inputs.push(line);
  }
  return nodes;
}

// Queries in batches, following phony nodes (they group other outputs) a
// few levels down so a dependency through e.g. an autogen target is seen.
export async function queryGraph(buildDir, outputs, { run = runNinja, depth = 3, batch = 200 } = {}) {
  const graph = new Map();
  let pending = [...new Set(outputs)];
  for (let level = 0; level <= depth && pending.length; level += 1) {
    const next = [];
    for (let i = 0; i < pending.length; i += batch) {
      const text = await run(buildDir, ["-t", "query", ...pending.slice(i, i + batch)]);
      if (text === null) continue;
      for (const [name, node] of parseNinjaQuery(text)) {
        graph.set(name, node);
        if (node.rule === "phony" || level === 0) {
          for (const input of [...node.inputs, ...node.implicit, ...node.orderOnly]) {
            if (!graph.has(input)) next.push(input);
          }
        }
      }
    }
    pending = [...new Set(next)].filter((name) => !graph.has(name) && !path.isAbsolute(name));
  }
  return graph;
}

// `ninja -t compdb`: output -> { source, compile }.
export function parseCompdb(text) {
  const map = new Map();
  let entries = [];
  try {
    entries = JSON.parse(text);
  } catch {
    return map;
  }
  for (const entry of entries) {
    if (!entry.output || !entry.command) continue;
    const file = path.isAbsolute(entry.file) ? entry.file : path.join(entry.directory ?? "", entry.file);
    map.set(entry.output, { source: file, compile: /\s-c\s/.test(` ${entry.command} `) });
  }
  return map;
}

// `ninja -t deps`: how many objects include each file (by compiler deps).
export function countIncluders(text, keep = () => true) {
  const counts = new Map();
  let objects = 0;
  for (const line of text.split("\n")) {
    if (!line) continue;
    if (!line.startsWith(" ")) {
      objects += 1;
      continue;
    }
    const file = line.trim();
    if (file && keep(file)) counts.set(file, (counts.get(file) ?? 0) + 1);
  }
  return { objects, counts };
}

// ---- jobs --------------------------------------------------------------------

// .ninja_log lines -> jobs (one per edge: all outputs of one command).
export function groupJobs(entries, buildDir = "") {
  const prefix = buildDir ? `${buildDir.replace(/\/$/, "")}/` : null;
  const jobs = new Map();
  for (const entry of entries) {
    const output = prefix && entry.output.startsWith(prefix) ? entry.output.slice(prefix.length) : entry.output;
    const key = `${entry.startMs}\t${entry.endMs}\t${entry.hash}`;
    if (!jobs.has(key)) jobs.set(key, { outputs: [], startMs: entry.startMs, endMs: entry.endMs });
    const job = jobs.get(key);
    if (!job.outputs.includes(output)) job.outputs.push(output);
  }
  return [...jobs.values()]
    .sort((a, b) => a.startMs - b.startMs || a.endMs - b.endMs)
    .map((job, index) => ({ id: index + 1, ...job, durationMs: job.endMs - job.startMs }));
}

const BOOKKEEPING = [/(^|\/)cmake\.verify_globs$/, /(^|\/)build\.ninja$/, /_autogen\/timestamp$/, /(^|\/)meta_types\/[^/]+_json_file_list\.txt(\.timestamp)?$/];

// pch, compile, link, archive, autogen (moc/uic/rcc), qml, resources, cmake, other.
export function categorize(job, rule, compdb) {
  const output = job.outputs[0] ?? "";
  if (job.outputs.every((o) => BOOKKEEPING.some((p) => p.test(o))) || /^RERUN_CMAKE$/.test(rule ?? "")) return "cmake";
  if (/(^|\/)cmake_pch\.[^/]+\.(pch|gch)$/.test(output)) return "pch";
  if (rule && /_COMPILER__/.test(rule)) return "compile";
  if (compdb?.compile) return "compile";
  if (rule && /_STATIC_LIBRARY_LINKER__/.test(rule)) return "archive";
  if (rule && /_(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__/.test(rule)) return "link";
  if (/\.(o|obj)$/.test(output)) return "compile";
  if (/\.a$/.test(output)) return "archive";
  if (/\.(so(\.\d+)*|dylib)$/.test(output) || /\.app\/Contents\/MacOS\//.test(output)) return "link";
  if (/_autogen\/|(^|\/)moc_[^/]+\.cpp$|\.moc$|(^|\/)ui_[^/]+\.h$|mocs_compilation/.test(output)) return "autogen";
  if (/qmlcache|qmltyperegistrations|\.qmltypes$|_qmllint|qmldir$|cachegen/.test(output)) return "qml";
  if (/(^|\/)qrc_[^/]+\.cpp$|\.rcc$|\.qrc$/.test(output)) return "resources";
  return "other";
}

// Peak and average number of jobs running at once.
export function parallelism(jobs) {
  const points = [];
  for (const job of jobs) {
    points.push([job.startMs, 1], [job.endMs, -1]);
  }
  points.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  let running = 0;
  let peak = 0;
  for (const [, delta] of points) {
    running += delta;
    peak = Math.max(peak, running);
  }
  const wallMs = jobs.length ? Math.max(...jobs.map((j) => j.endMs)) - Math.min(...jobs.map((j) => j.startMs)) : 0;
  const summedMs = jobs.reduce((sum, job) => sum + job.durationMs, 0);
  return { wallMs, summedMs, peak, average: wallMs > 0 ? Math.round((summedMs / wallMs) * 100) / 100 : null };
}

// The longest chain of jobs that ran, each depending (through Ninja's
// graph) on the previous one. An approximation of the critical path: it
// ignores time a job waited for a free slot, and edges through jobs that
// didn't run are not followed.
export function criticalPath(jobs, graph) {
  const producer = new Map();
  for (const job of jobs) for (const output of job.outputs) producer.set(output, job);
  const depsOf = (job) => {
    const found = new Set();
    const seen = new Set();
    const visit = (name, level) => {
      if (seen.has(name) || level > 4) return;
      seen.add(name);
      const made = producer.get(name);
      if (made && made !== job) {
        found.add(made);
        return;
      }
      const node = graph.get(name);
      if (node && (level === 0 || node.rule === "phony")) {
        for (const input of [...node.inputs, ...node.implicit, ...node.orderOnly]) visit(input, level + 1);
      }
    };
    for (const output of job.outputs) visit(output, 0);
    return [...found].filter((dep) => dep.endMs <= job.startMs + 1);
  };
  const order = [...jobs].sort((a, b) => a.endMs - b.endMs);
  const best = new Map();
  for (const job of order) {
    let prev = null;
    let length = job.durationMs;
    for (const dep of depsOf(job)) {
      const candidate = best.get(dep);
      if (candidate && candidate.length + job.durationMs > length) {
        length = candidate.length + job.durationMs;
        prev = dep;
      }
    }
    best.set(job, { length, prev });
  }
  let end = null;
  for (const job of order) if (!end || best.get(job).length > best.get(end).length) end = job;
  const chain = [];
  for (let job = end; job; job = best.get(job).prev) chain.unshift(job);
  return {
    basis: graph.size ? "approximation: longest dependency chain among jobs that ran (ninja -t query)" : "unavailable: no dependency graph",
    available: graph.size > 0 && chain.length > 0,
    lengthMs: graph.size ? best.get(end)?.length ?? 0 : null,
    jobs: graph.size ? chain.map((job) => job.id) : [],
  };
}

// ---- compiler cache --------------------------------------------------------------

// ccache --print-stats (ccache 4): "key\tvalue" lines.
export function parseCcacheStats(text) {
  const values = {};
  for (const line of (text ?? "").split("\n")) {
    const [key, value] = line.split("\t");
    if (key && value !== undefined && /^\d+$/.test(value.trim())) values[key.trim()] = Number(value);
  }
  const hits = (values.direct_cache_hit ?? 0) + (values.preprocessed_cache_hit ?? 0);
  const misses = values.cache_miss ?? 0;
  const keys = Object.keys(values);
  if (!keys.length) return null;
  const uncacheable = keys.filter((k) => /^(unsupported|could_not|compiler_check|autoconf|bad_|called_for|multiple_source|no_input|output_to_stdout|preprocessor_error|compile_failed|internal_error|missing_cache_file|error_hashing|cache_size_kibibyte)/.test(k)).filter((k) => !/cache_size/.test(k)).reduce((sum, k) => sum + values[k], 0);
  return { hits, misses, uncacheable };
}

// sccache --show-stats --stats-format=json.
export function parseSccacheStats(text) {
  let data;
  try {
    data = JSON.parse(text ?? "");
  } catch {
    return null;
  }
  const stats = data.stats ?? data;
  const sum = (value) => (typeof value === "number" ? value : Object.values(value?.counts ?? value ?? {}).reduce((s, v) => s + (typeof v === "number" ? v : 0), 0));
  return {
    hits: sum(stats.cache_hits),
    misses: sum(stats.cache_misses),
    uncacheable: sum(stats.not_cached) + sum(stats.non_cacheable_compilations ?? 0),
  };
}

export function diffCache(before, after) {
  if (!before || !after) return null;
  const hits = after.hits - before.hits;
  const misses = after.misses - before.misses;
  const uncacheable = after.uncacheable - before.uncacheable;
  if (hits < 0 || misses < 0) return null; // stats were reset during the build
  return { hits, misses, uncacheable, hitRate: hits + misses > 0 ? Math.round((hits / (hits + misses)) * 1000) / 1000 : null };
}

// Which cache the build uses (from CMakeCache.txt), and a reader for its counters.
export function cacheTool(launcher) {
  const name = path.basename(String(launcher ?? "")).toLowerCase();
  if (name.startsWith("sccache")) return { name: "sccache", read: (run) => run("sccache", ["--show-stats", "--stats-format=json"]).then(parseSccacheStats) };
  if (name.startsWith("ccache")) return { name: "ccache", read: (run) => run("ccache", ["--print-stats"]).then(parseCcacheStats) };
  return null;
}

// ---- putting it together -------------------------------------------------------

async function readLogSince(file, before) {
  let size;
  try {
    size = (await stat(file)).size;
  } catch {
    return { entries: null, reason: ".ninja_log not found" };
  }
  const offset = before ?? 0;
  if (size < offset) return { entries: null, reason: ".ninja_log was recompacted during the build" };
  const handle = await open(file, "r");
  try {
    const buffer = Buffer.alloc(size - offset);
    await handle.read(buffer, 0, buffer.length, offset);
    return { entries: parseNinjaLogLines(buffer.toString("utf8")) };
  } finally {
    await handle.close();
  }
}

function underRoot(file, root) {
  return root && file.startsWith(`${root}/`);
}

// The full evidence for one build. Returns { summary, artifact }: summary is
// small (for summary.json), artifact has every job (build-evidence.json).
// cache: { name, before, after } counters read around the build, or null.
export async function collectBuildEvidence({ root, buildDir, logBefore, explain = null, cache = null, run = runNinja }) {
  const log = await readLogSince(path.join(buildDir, ".ninja_log"), logBefore);
  const notes = [];
  let jobs = log.entries ? groupJobs(log.entries, buildDir) : [];
  if (!log.entries) notes.push(`Jobs unavailable: ${log.reason}.`);
  const outputs = jobs.flatMap((job) => job.outputs);
  const [compdbText, graph] = await Promise.all([
    jobs.length ? run(buildDir, ["-t", "compdb"]) : null,
    jobs.length ? queryGraph(buildDir, jobs.map((job) => job.outputs[0]), { run }) : new Map(),
  ]);
  const compdb = compdbText ? parseCompdb(compdbText) : new Map();
  if (jobs.length && !compdbText) notes.push("Source files unavailable: ninja -t compdb failed.");
  if (jobs.length && !graph.size) notes.push("Dependency graph unavailable: ninja -t query failed.");
  const dirs = [[root, ""], [buildDir, "<build>/"]];
  for (const job of jobs) {
    const node = graph.get(job.outputs[0]);
    const entry = compdb.get(job.outputs[0]);
    job.rule = node?.rule ?? null;
    job.category = categorize(job, job.rule, entry);
    if (entry?.compile) {
      job.source = relativeTo(entry.source, dirs);
      job.generatedSource = !underRoot(entry.source, root) || entry.source.startsWith(`${buildDir}/`);
    }
  }
  const timing = parallelism(jobs);
  const path_ = criticalPath(jobs, graph);
  const byCategory = {};
  for (const job of jobs) {
    const bucket = (byCategory[job.category] ??= { jobs: 0, summedMs: 0, longestMs: 0 });
    bucket.jobs += 1;
    bucket.summedMs += job.durationMs;
    bucket.longestMs = Math.max(bucket.longestMs, job.durationMs);
  }

  // Why it rebuilt, and how far changed headers reach.
  const isCompile = (output) => compdb.get(output)?.compile || /\.(o|obj)$/.test(output);
  let rebuild = null;
  if (explain) {
    rebuild = analyzeExplain(explain.lines, { buildDir, root, isCompile });
    if (explain.dropped) rebuild.droppedExplainLines = explain.dropped;
  } else {
    notes.push("Rebuild causes unavailable: the dry run with -d explain did not run.");
  }
  let headers = null;
  const depsText = jobs.length || rebuild ? await run(buildDir, ["-t", "deps"]) : null;
  if (depsText) {
    const keep = (file) => underRoot(file, root) && !file.startsWith(`${buildDir}/`) && inputKind(file, { buildDir }) === "header";
    const { objects, counts } = countIncluders(depsText, (file) => keep(file) || rebuild?.causes.some((c) => c.inputPath === file));
    for (const cause of rebuild?.causes ?? []) {
      if (counts.has(cause.inputPath)) cause.includedBy = { objects: counts.get(cause.inputPath), basis: "ninja -t deps" };
    }
    headers = {
      basis: "ninja -t deps (headers the compiler reported for each object)",
      objects,
      mostIncluded: [...counts.entries()].filter(([file]) => keep(file))
        .sort((a, b) => b[1] - a[1]).slice(0, TOP)
        .map(([file, count]) => ({ header: relativeTo(file, dirs), objects: count })),
    };
  } else if (jobs.length) {
    notes.push("Header fan-out unavailable: ninja -t deps failed.");
  }

  let cacheResult = { configured: false };
  if (cache) {
    cacheResult = { configured: true, tool: cache.name, basis: `${cache.name} counters before and after the build`, observed: diffCache(cache.before, cache.after) };
    if (!cacheResult.observed) notes.push(`${cache.name} counters unavailable or reset during the build.`);
  }

  const kept = jobs.length > MAX_JOBS ? [...jobs].sort((a, b) => b.durationMs - a.durationMs).slice(0, MAX_JOBS) : jobs;
  const strip = ({ id, outputs: outs, startMs, endMs, durationMs, category, rule, source, generatedSource }) =>
    ({ id, output: outs[0], outputCount: outs.length, startMs, endMs, durationMs, category, rule, source: source ?? null, generatedSource: generatedSource ?? null });
  const top = (filter) => [...jobs].filter(filter).sort((a, b) => b.durationMs - a.durationMs).slice(0, TOP).map(strip);
  const chainJobs = path_.jobs.map((id) => jobs.find((job) => job.id === id)).filter(Boolean);
  const summary = {
    schema: BUILD_EVIDENCE_SCHEMA,
    schemaVersion: BUILD_EVIDENCE_VERSION,
    jobs: jobs.length,
    outputs: outputs.length,
    timing: {
      basis: ".ninja_log (Ninja's clock)",
      wallMs: timing.wallMs,
      summedJobMs: timing.summedMs,
      peakParallelJobs: timing.peak,
      averageParallelJobs: timing.average,
      longestJob: jobs.length ? strip([...jobs].sort((a, b) => b.durationMs - a.durationMs)[0]) : null,
    },
    criticalPath: { ...path_, jobs: chainJobs.map(strip) },
    byCategory,
    slowestCompiles: top((job) => job.category === "compile"),
    slowestOther: top((job) => job.category !== "compile" && job.category !== "cmake"),
    rebuild,
    headers,
    compilerCache: cacheResult,
    notes,
  };
  const artifact = {
    schema: BUILD_EVIDENCE_SCHEMA,
    schemaVersion: BUILD_EVIDENCE_VERSION,
    truncatedJobs: jobs.length - kept.length,
    jobs: kept.map(strip),
    explain: explain ? { lines: explain.lines.length, dropped: explain.dropped } : null,
  };
  return { summary, artifact };
}
