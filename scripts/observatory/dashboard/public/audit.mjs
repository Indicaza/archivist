// Audits: one press runs a preset (scenarios, a build, or both) through the
// existing job runner, and the results become a list of findings, each with
// the rule that produced it, the measured numbers behind it and what to do
// next. Pure (no Node or browser APIs): the CLI, agents and the page all use
// the same presets, rules and wording.
//
// What this does not do: score anything, or call one run a regression.
// Findings report what was measured in this audit; whether something got
// worse is the regression rule in regression.mjs (several runs on each side,
// no overlap), applied by compareAudits below.
import { analyzeCategory, categoryLabel, categoryOf, resolveBudgets } from "./regression.mjs";

export const AUDIT_SCHEMA = "archivist.observatory.audit";
export const AUDIT_SCHEMA_VERSION = 1;
export const FINDINGS_SCHEMA = "archivist.observatory.findings";
// Bump when a rule's threshold or meaning changes; findings from audits with
// another version are not compared one to one.
export const RULES_VERSION = 1;

export const UI_SCENARIOS = ["startup", "sidebar", "workspace", "chat"];

export const PRESETS = {
  quick: {
    id: "quick", name: "Quick", scenarios: ["startup", "workspace"], build: false, repeat: 1,
    description: "Startup and the workspace scenario, once each. A first look; one round is never enough to call a regression.",
  },
  ui: {
    id: "ui", name: "Full UI", scenarios: UI_SCENARIOS, build: false, repeat: 3,
    description: "Every UI scenario (startup, sidebar, workspace, chat), three rounds each, so two audits can be compared.",
  },
  build: {
    id: "build", name: "Build", scenarios: [], build: true, repeat: 1,
    description: "An incremental build of build/frontend-debug with full job evidence: what rebuilt, why, and the slowest chain. Nothing is cleaned.",
  },
  complete: {
    id: "complete", name: "Complete", scenarios: UI_SCENARIOS, build: true, repeat: 3,
    description: "The build preset, then every UI scenario three rounds each against the binary it built.",
  },
};

export const SEVERITIES = ["high", "medium", "low", "info"];
const SEVERITY_RANK = Object.fromEntries(SEVERITIES.map((name, index) => [name, index]));

// Every rule, as shown on the page and in reports. `when` is the threshold in
// words; the code below applies exactly that.
export const RULES = [
  { id: "ui.failed-actions", subsystem: "ui-lab", title: "Actions failed", when: "Any scenario run did not pass, or any action failed.", severity: "high", basis: "UI Lab action records" },
  { id: "ui.no-operations", subsystem: "instrumentation", title: "No operations reported", when: "The app build didn't report LiveEvents operations to the UI Lab.", severity: "info", basis: "summary.operations.collected" },
  { id: "ui.slow-step", subsystem: "ui", title: "Slow step", when: "A step's settled latency, median over measured actions, is 100 ms or more (250 ms or more: high).", severity: "medium / high", basis: "settled = input until no frame for the settle window" },
  { id: "ui.slow-operation", subsystem: "(from the operation)", title: "Slow operation", when: "An instrumented operation's median inside a step is 50 ms or more (200 ms or more: high).", severity: "medium / high", basis: "LiveEvents spans tagged with the running action" },
  { id: "ui.stalls", subsystem: "ui-thread", title: "Event-loop stalls", when: "The UI Lab probe fired 50 ms or more late during measured actions.", severity: "medium", basis: "50 ms probe on the UI thread" },
  { id: "ui.unattributed", subsystem: "instrumentation", title: "Time no operation explains", when: "In a step of 50 ms or more, half or more of the action is outside every instrumented operation.", severity: "low", basis: "action time minus the union of its top-level spans" },
  { id: "ui.frame-gap", subsystem: "rendering", title: "Long frame gaps", when: "The longest gap between frames, median over measured actions, is over 33.4 ms (two frames at an assumed 60 Hz).", severity: "low", basis: "frame swaps seen by the driver; 60 Hz is an assumption" },
  { id: "startup.slow-stage", subsystem: "startup", title: "Slow startup stage", when: "A startup stage measured in main() takes 250 ms or more (1000 ms or more: high).", severity: "medium / high", basis: "LiveEvents spans in main()" },
  { id: "build.failed", subsystem: "build", title: "Build failed", when: "The build did not succeed.", severity: "high", basis: "build phases" },
  { id: "build.missing-evidence", subsystem: "build", title: "No job evidence", when: "The build recorded no per-job evidence (older record, or Ninja's log was unreadable).", severity: "info", basis: "summary.evidence" },
  { id: "build.dominant-unit", subsystem: "compile", title: "One file dominates compile time", when: "One compile takes 20% or more of all compile time and at least 5 s.", severity: "medium", basis: ".ninja_log job times" },
  { id: "build.header-fanout", subsystem: "compile", title: "A changed header rebuilt many files", when: "A changed header caused 10 or more compiles.", severity: "medium", basis: "ninja -d explain, confirmed with ninja -t deps" },
  { id: "build.link-share", subsystem: "link", title: "Linking takes a large share", when: "Linking and archiving take 25% or more of wall time and at least 2 s.", severity: "medium", basis: ".ninja_log job times (summed link time vs wall time)" },
  { id: "build.autogen-share", subsystem: "autogen", title: "Code generation takes a large share", when: "moc, rcc, qmlcachegen and other generators take 15% or more of summed job time and at least 2 s.", severity: "low", basis: ".ninja_log job times" },
  { id: "build.chain-bound", subsystem: "build", title: "The build waits on one chain", when: "The longest dependency chain is 80% or more of wall time, for a build of 10 s or more.", severity: "info", basis: "critical path from ninja -t query (an approximation)" },
  { id: "build.cache-misses", subsystem: "compiler-cache", title: "Compiler cache mostly missed", when: "A configured ccache or sccache hit under 50% of 10 or more cacheable compiles.", severity: "low", basis: "cache counters before and after the build" },
  { id: "build.cmake-rerun", subsystem: "cmake", title: "CMake ran again", when: "A CMake input changed, so the build re-ran CMake.", severity: "info", basis: "ninja -d explain" },
];

export const RULE_BY_ID = Object.fromEntries(RULES.map((rule) => [rule.id, rule]));

const round = (value) => (value == null ? null : Math.round(value * 10) / 10);
const msText = (value) => (value == null ? "n/a" : value >= 10000 ? `${(value / 1000).toFixed(1)} s` : value >= 1000 ? `${(value / 1000).toFixed(2)} s` : `${Math.round(value * 10) / 10} ms`);
const pct = (part, whole) => (whole ? Math.round((part / whole) * 100) : null);

function median(values) {
  const list = values.filter((value) => typeof value === "number" && Number.isFinite(value)).sort((a, b) => a - b);
  if (!list.length) return null;
  const mid = Math.floor(list.length / 2);
  return list.length % 2 ? list[mid] : (list[mid - 1] + list[mid]) / 2;
}

// How many of the rounds showed it: the honest version of "confidence".
function confidence(seen, rounds) {
  if (rounds <= 1) return { level: "single-run", text: "one run; rerun to confirm" };
  if (seen === rounds) return { level: "consistent", text: `in all ${rounds} runs` };
  return { level: "intermittent", text: `in ${seen} of ${rounds} runs` };
}

// The slowest operation in a step with no slower child, over the runs.
function slowestLeaf(runs, stepKey) {
  const values = new Map();
  for (const run of runs) {
    const step = (run.summary.operations?.steps ?? []).find((entry) => `${entry.step}:${entry.label}` === stepKey);
    for (const op of step?.operations ?? []) {
      if (!values.has(op.name)) values.set(op.name, { parent: op.parent ?? null, ms: [] });
      values.get(op.name).ms.push(op.medianMs);
    }
  }
  const typical = [...values.entries()].map(([name, entry]) => ({ name, parent: entry.parent, ms: median(entry.ms) }));
  const leaves = typical.filter((op) => !typical.some((other) => other.parent === op.name && other.ms >= op.ms * 0.6));
  return leaves.sort((a, b) => b.ms - a.ms)[0] ?? null;
}

const subsystemOf = (name) => String(name).split(".")[0] || "app";

// ---- findings -----------------------------------------------------------------

// children: [{ runId, summary }] (raw summary.json of each run the audit made).
// Returns findings, most severe first. Every finding names its rule, the runs
// it came from and the numbers it is based on.
export function evaluateFindings(children) {
  const findings = [];
  const add = (rule, subject, finding) => {
    findings.push({ id: `${rule}:${subject}`, rule, title: RULE_BY_ID[rule].title, ...finding });
  };
  const ui = children.filter((child) => child.summary?.kind === "ui-scenario");
  const byScenario = new Map();
  for (const child of ui) {
    const id = child.summary.scenario?.id ?? "unknown";
    if (!byScenario.has(id)) byScenario.set(id, []);
    byScenario.get(id).push(child);
  }

  for (const [scenario, runs] of byScenario) {
    const name = runs[0].summary.scenario?.name ?? scenario;
    const rounds = runs.length;
    const runIds = runs.map((run) => run.runId);

    const failed = runs.filter((run) => run.summary.status !== "passed" || (run.summary.actions?.failed ?? 0) > 0);
    if (failed.length) {
      const first = failed[0].summary;
      add("ui.failed-actions", scenario, {
        subsystem: "ui-lab", severity: "high",
        summary: `${name}: ${failed.length} of ${rounds} run${rounds === 1 ? "" : "s"} did not pass${first.actions?.firstFailure ? `; first failure: action ${first.actions.firstFailure.index} (${first.actions.firstFailure.label ?? first.actions.firstFailure.op ?? "?"}) ${first.actions.firstFailure.error ?? ""}`.trimEnd() : first.error ? `: ${first.error}` : ""}.`,
        impact: { value: failed.length, unit: "runs", text: `${failed.length} of ${rounds} runs` },
        evidence: failed.map((run) => ({ runId: run.runId, text: `status ${run.summary.status}, ${run.summary.actions?.failed ?? 0} failed actions` })),
        confidence: confidence(failed.length, rounds),
        nextStep: "Open the run's actions and log. Timings from failed actions are left out of every other finding.",
      });
    }

    const passed = runs.filter((run) => run.summary.status === "passed");
    if (!passed.length) continue;
    const noOps = passed.filter((run) => run.summary.operations && run.summary.operations.collected === false);
    if (noOps.length === passed.length) {
      add("ui.no-operations", scenario, {
        subsystem: "instrumentation", severity: "info",
        summary: `${name}: the app reported no operations, so this audit can say how long actions took but not what inside the app took the time.`,
        impact: null,
        evidence: noOps.map((run) => ({ runId: run.runId, text: run.summary.operations.reason ?? "operations not collected" })),
        confidence: confidence(noOps.length, passed.length),
        nextStep: "Rebuild the app from this branch (npm run observatory:build) and run the audit again.",
      });
    }

    // Steps: settled latency, unattributed time, stalls.
    const stepKeys = new Map();
    for (const run of passed) {
      for (const step of run.summary.stats?.steps ?? []) {
        const key = `${step.step}:${step.label ?? step.op}`;
        if (!stepKeys.has(key)) stepKeys.set(key, { step: step.step, label: step.label ?? step.op, values: [] });
        stepKeys.get(key).values.push({ runId: run.runId, ms: step.settledMs?.medianMs ?? null });
      }
    }
    for (const [key, entry] of stepKeys) {
      const typical = median(entry.values.map((value) => value.ms));
      if (typical == null || typical < 100) continue;
      const seen = entry.values.filter((value) => value.ms >= 100).length;
      const cause = slowestLeaf(passed, key);
      add("ui.slow-step", `${scenario}:${key}`, {
        subsystem: "ui", severity: typical >= 250 ? "high" : "medium",
        summary: `${name}, step ${entry.step} (${entry.label}) takes ${msText(typical)} to settle${cause ? `; the slowest operation inside it is ${cause.name} (${msText(cause.ms)})` : ""}.`,
        impact: { value: round(typical), unit: "ms", text: `${msText(typical)} median settled` },
        evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, text: `median settled ${msText(value.ms)}` })),
        confidence: confidence(seen, entry.values.length),
        nextStep: "Look at this step's operation tree in the audit to see which operation holds the time.",
        scenario,
      });
    }

    const opStats = new Map();
    const stallStats = new Map();
    const unattributed = new Map();
    for (const run of passed) {
      const ops = run.summary.operations;
      if (!ops?.collected) continue;
      for (const step of ops.steps ?? []) {
        const stepKey = `${step.step}:${step.label}`;
        for (const op of step.operations ?? []) {
          const key = `${op.name}\t${stepKey}`;
          if (!opStats.has(key)) opStats.set(key, { name: op.name, step: step.step, label: step.label, values: [] });
          opStats.get(key).values.push({ runId: run.runId, ms: op.medianMs, max: op.maxMs, n: op.n });
          if (op.parent) opStats.get(key).parent = op.parent;
        }
        if (step.stalls) {
          if (!stallStats.has(stepKey)) stallStats.set(stepKey, { step: step.step, label: step.label, values: [] });
          stallStats.get(stepKey).values.push({ runId: run.runId, stalls: step.stalls, runs: step.runs });
        }
        if (step.unattributedMedianMs != null) {
          if (!unattributed.has(stepKey)) unattributed.set(stepKey, { step: step.step, label: step.label, values: [] });
          unattributed.get(stepKey).values.push({ runId: run.runId, ms: step.unattributedMedianMs });
        }
      }
    }
    // Per operation, its worst step only, so one slow operation is one finding.
    const worst = new Map();
    for (const entry of opStats.values()) {
      const typical = median(entry.values.map((value) => value.ms));
      if (typical == null || typical < 50) continue;
      if (!worst.has(entry.name) || worst.get(entry.name).typical < typical) worst.set(entry.name, { ...entry, typical });
    }
    // A slow operation whose own child holds most of its time is reported
    // as the child, "inside" the parent: one finding per cause.
    for (const entry of worst.values()) {
      const child = [...worst.values()].find((other) => other.parent === entry.name && other.step === entry.step && other.typical >= entry.typical * 0.6);
      if (child) entry.explainedBy = child.name;
    }
    for (const entry of worst.values()) {
      if (entry.explainedBy) continue;
      const parent = entry.parent && worst.get(entry.parent)?.explainedBy === entry.name ? worst.get(entry.parent) : null;
      add("ui.slow-operation", `${scenario}:${entry.name}`, {
        subsystem: subsystemOf(entry.name), severity: entry.typical >= 200 ? "high" : "medium",
        summary: `${entry.name} takes ${msText(entry.typical)} in ${name}, step ${entry.step} (${entry.label})${parent ? `, inside ${parent.name} (${msText(parent.typical)})` : ""}.`,
        impact: { value: round(entry.typical), unit: "ms", text: `${msText(entry.typical)} median, up to ${msText(Math.max(...entry.values.map((value) => value.max ?? 0)))}` },
        evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, operation: entry.name, text: `median ${msText(value.ms)} over ${value.n} action${value.n === 1 ? "" : "s"}` })),
        confidence: confidence(entry.values.filter((value) => value.ms >= 50).length, passed.length),
        nextStep: `Open the operation tree for step ${entry.step}: its children show where inside ${entry.name} the time goes; self time with no children means the next span to add is inside it.`,
        scenario, operation: entry.name,
      });
    }
    for (const [stepKey, entry] of stallStats) {
      const total = entry.values.reduce((sum, value) => sum + value.stalls, 0);
      add("ui.stalls", `${scenario}:${stepKey}`, {
        subsystem: "ui-thread", severity: "medium",
        summary: `${name}, step ${entry.step} (${entry.label}): the UI thread was blocked ${total} time${total === 1 ? "" : "s"} (probe 50 ms or more late).`,
        impact: { value: total, unit: "stalls", text: `${total} stall${total === 1 ? "" : "s"}` },
        evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, text: `${value.stalls} stall${value.stalls === 1 ? "" : "s"} in ${value.runs} measured action${value.runs === 1 ? "" : "s"}` })),
        confidence: confidence(entry.values.length, passed.length),
        nextStep: "Work on the UI thread during this step blocks input and drawing; the operation tree shows which span the stall falls in.",
        scenario,
      });
    }
    for (const [stepKey, entry] of unattributed) {
      const typical = median(entry.values.map((value) => value.ms));
      const settled = stepKeys.get(stepKey) ? median(stepKeys.get(stepKey).values.map((value) => value.ms)) : null;
      if (typical == null || settled == null || settled < 50 || typical < settled / 2) continue;
      add("ui.unattributed", `${scenario}:${stepKey}`, {
        subsystem: "instrumentation", severity: "low",
        summary: `${name}, step ${entry.step} (${entry.label}): ${msText(typical)} of ${msText(settled)} is outside every instrumented operation.`,
        impact: { value: round(typical), unit: "ms", text: `${pct(typical, settled)}% of the step unexplained` },
        evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, text: `unattributed median ${msText(value.ms)}` })),
        confidence: confidence(entry.values.length, passed.length),
        nextStep: "Add a LiveEvents span around the code this step runs (QML bindings, model resets, layout), then audit again.",
        scenario,
      });
    }

    const gaps = passed.map((run) => ({ runId: run.runId, ms: run.summary.stats?.measured?.frameMaxIntervalMs?.medianMs ?? null }));
    const gap = median(gaps.map((value) => value.ms));
    if (gap != null && gap > 33.4 && scenario !== "startup") {
      add("ui.frame-gap", scenario, {
        subsystem: "rendering", severity: "low",
        summary: `${name}: the longest gap between frames is ${msText(gap)} (median over measured actions); at 60 Hz that is ${Math.floor(gap / 16.7)} frames missed.`,
        impact: { value: round(gap), unit: "ms", text: `${msText(gap)} median longest gap` },
        evidence: gaps.map((value) => ({ runId: value.runId, text: `median longest gap ${msText(value.ms)}` })),
        confidence: confidence(gaps.filter((value) => value.ms > 33.4).length, gaps.length),
        nextStep: "Frame gaps line up with slow operations or stalls in the same step; the 60 Hz figure is an assumption about the display.",
        scenario,
      });
    }

    const stages = new Map();
    for (const run of passed) {
      for (const stage of run.summary.operations?.startup?.stages ?? []) {
        if (!stages.has(stage.name)) stages.set(stage.name, []);
        stages.get(stage.name).push({ runId: run.runId, ms: stage.durationMs });
      }
    }
    if (scenario === "startup" || !byScenario.has("startup")) {
      for (const [stage, values] of stages) {
        const typical = median(values.map((value) => value.ms));
        if (typical == null || typical < 250) continue;
        add("startup.slow-stage", stage, {
          subsystem: "startup", severity: typical >= 1000 ? "high" : "medium",
          summary: `Startup stage ${stage} takes ${msText(typical)}.`,
          impact: { value: round(typical), unit: "ms", text: `${msText(typical)} median` },
          evidence: values.map((value) => ({ runId: value.runId, operation: `startup.${stage}`, text: `${msText(value.ms)}` })),
          confidence: confidence(values.filter((value) => value.ms >= 250).length, values.length),
          nextStep: `Add spans inside ${stage} in main() (or the code it calls) to split it further.`,
          scenario,
        });
      }
    }
  }

  for (const child of children.filter((entry) => entry.summary?.kind === "build")) {
    findings.push(...buildFindings(child.runId, child.summary));
  }
  return findings.sort((a, b) => SEVERITY_RANK[a.severity] - SEVERITY_RANK[b.severity] || (b.impact?.unit === "ms" ? b.impact.value : 0) - (a.impact?.unit === "ms" ? a.impact.value : 0));
}

function buildFindings(runId, summary) {
  const out = [];
  const add = (rule, subject, finding) => out.push({
    id: `${rule}:${subject}`, rule, title: RULE_BY_ID[rule].title,
    confidence: { level: "single-run", text: "one build; job times vary between builds" },
    ...finding,
  });
  if (summary.status !== "success") {
    const failed = (summary.phases ?? []).find((phase) => phase.status === "failure" || phase.status === "interrupted");
    add("build.failed", "build", {
      subsystem: "build", severity: "high",
      summary: `The build ${summary.status === "interrupted" ? "was interrupted" : "failed"}${failed ? ` in ${failed.name}` : ""}${failed?.error?.message ? `: ${failed.error.message}` : ""}.`,
      impact: null, evidence: [{ runId, text: `status ${summary.status}` }],
      confidence: { level: "measured", text: "the build's exit status" },
      nextStep: "Fix the build first; UI scenarios in this audit ran the binary that was already there, if any.",
    });
    return out;
  }
  const evidence = summary.evidence;
  if (!evidence?.timing) {
    add("build.missing-evidence", "build", {
      subsystem: "build", severity: "info", summary: "This build has no per-job evidence, so there is nothing to say about its jobs.",
      impact: null, evidence: [{ runId, text: "summary.evidence missing" }], confidence: { level: "measured", text: "absent from the record" },
      nextStep: "Build with this branch's Observatory (npm run observatory:build).",
    });
    return out;
  }
  const wall = evidence.timing.wallMs ?? 0;
  const summed = evidence.timing.summedJobMs ?? 0;
  const compile = evidence.byCategory?.compile?.summedMs ?? 0;
  const top = evidence.slowestCompiles?.[0];
  if (top && compile && top.durationMs >= 5000 && top.durationMs >= compile * 0.2) {
    add("build.dominant-unit", top.source ?? top.output, {
      subsystem: "compile", severity: "medium",
      summary: `${top.source ?? top.output} takes ${msText(top.durationMs)} to compile, ${pct(top.durationMs, compile)}% of all compile time in this build.`,
      impact: { value: top.durationMs, unit: "ms", text: `${msText(top.durationMs)}, ${pct(top.durationMs, compile)}% of compile time` },
      evidence: [{ runId, artifact: "evidence", job: top.id, text: `${top.output}: ${msText(top.durationMs)}` }],
      nextStep: "Build once with --time-trace (npm run observatory -- build --time-trace) to see which headers and templates this file spends its time on.",
    });
  }
  for (const cause of evidence.rebuild?.causes ?? []) {
    if (cause.inputKind !== "header" || cause.compiles < 10) continue;
    add("build.header-fanout", cause.input, {
      subsystem: "compile", severity: "medium",
      summary: `Changing ${cause.input} rebuilt ${cause.compiles} files${cause.includedBy ? ` (${cause.includedBy.objects} objects include it)` : ""}.`,
      impact: { value: cause.compiles, unit: "compiles", text: `${cause.compiles} compiles` },
      evidence: [{ runId, artifact: "evidence", text: `ninja explain: ${cause.outputs} outputs older than ${cause.input}` }],
      nextStep: "Move what changes often out of this header, or forward-declare instead of including it, so fewer files depend on it.",
    });
  }
  const link = (evidence.byCategory?.link?.summedMs ?? 0) + (evidence.byCategory?.archive?.summedMs ?? 0);
  if (wall && link >= 2000 && link >= wall * 0.25) {
    add("build.link-share", "link", {
      subsystem: "link", severity: "medium",
      summary: `Linking and archiving took ${msText(link)}, ${pct(link, wall)}% of the build's ${msText(wall)} wall time.`,
      impact: { value: link, unit: "ms", text: `${msText(link)} (${pct(link, wall)}% of wall time)` },
      evidence: [{ runId, artifact: "evidence", text: `link ${msText(evidence.byCategory?.link?.summedMs ?? 0)}, archive ${msText(evidence.byCategory?.archive?.summedMs ?? 0)}` }],
      nextStep: "Links run mostly one at a time at the end. A faster linker (lld) or fewer, larger libraries shorten this.",
    });
  }
  const autogen = evidence.byCategory?.autogen?.summedMs ?? 0;
  if (summed && autogen >= 2000 && autogen >= summed * 0.15) {
    add("build.autogen-share", "autogen", {
      subsystem: "autogen", severity: "low",
      summary: `Code generation (moc, rcc, qmlcachegen) took ${msText(autogen)}, ${pct(autogen, summed)}% of summed job time.`,
      impact: { value: autogen, unit: "ms", text: `${msText(autogen)} summed` },
      evidence: [{ runId, artifact: "evidence", text: `${evidence.byCategory.autogen.jobs} generator jobs` }],
      nextStep: "See which generated files rebuilt in the jobs list; a header change that reruns moc for many files shows up here.",
    });
  }
  const chain = evidence.criticalPath;
  if (chain?.available && wall >= 10000 && chain.lengthMs >= wall * 0.8) {
    add("build.chain-bound", "critical-path", {
      subsystem: "build", severity: "info",
      summary: `The longest dependency chain (${chain.jobs.length} jobs, ${msText(chain.lengthMs)}) is ${pct(chain.lengthMs, wall)}% of the wall time: more cores would not make this build much faster.`,
      impact: { value: chain.lengthMs, unit: "ms", text: `${msText(chain.lengthMs)} chain` },
      evidence: [{ runId, artifact: "evidence", text: chain.jobs.map((job) => job.source ?? job.output).slice(-4).join(" → ") }],
      nextStep: "Shorten the chain: the slowest jobs on it are listed in the build section.",
    });
  }
  const cache = evidence.compilerCache;
  const observed = cache?.observed;
  if (cache?.configured && observed && observed.hits + observed.misses >= 10 && observed.hitRate < 0.5) {
    add("build.cache-misses", cache.tool, {
      subsystem: "compiler-cache", severity: "low",
      summary: `${cache.tool} hit ${Math.round(observed.hitRate * 100)}% of ${observed.hits + observed.misses} cacheable compiles.`,
      impact: { value: observed.misses, unit: "misses", text: `${observed.misses} misses` },
      evidence: [{ runId, artifact: "evidence", text: `${observed.hits} hits, ${observed.misses} misses, ${observed.uncacheable} uncacheable` }],
      nextStep: "Misses after a real change are expected. Many misses on a rebuild of unchanged code point at changing flags or absolute paths in the command line.",
    });
  }
  const cmake = (evidence.rebuild?.causes ?? []).filter((cause) => cause.inputKind === "cmake");
  if (cmake.length) {
    add("build.cmake-rerun", "cmake", {
      subsystem: "cmake", severity: "info",
      summary: `CMake ran again because ${cmake.map((cause) => cause.input).slice(0, 3).join(", ")} changed.`,
      impact: null,
      evidence: [{ runId, artifact: "evidence", text: cmake.map((cause) => cause.input).join(", ") }],
      nextStep: "Expected after editing CMake files; if nothing was edited, a generated file or glob is touching it on every build.",
    });
  }
  return out;
}

// The headline numbers an audit keeps in its summary, so History, reports
// and agents don't have to open every child run.
export function auditHighlights(children) {
  const ops = new Map();
  for (const child of children) {
    const summary = child.summary;
    if (summary?.kind !== "ui-scenario" || summary.status !== "passed" || !summary.operations?.collected) continue;
    for (const step of summary.operations.steps ?? []) {
      for (const op of step.operations ?? []) {
        const key = `${summary.scenario?.id}\t${op.name}`;
        const entry = ops.get(key) ?? { name: op.name, scenario: summary.scenario?.id, medians: [], maxMs: 0, runs: new Set() };
        entry.medians.push(op.medianMs);
        entry.maxMs = Math.max(entry.maxMs, op.maxMs ?? 0);
        entry.runs.add(child.runId);
        ops.set(key, entry);
      }
    }
  }
  const operations = [...ops.values()]
    .map((entry) => ({ name: entry.name, scenario: entry.scenario, medianMs: round(median(entry.medians)), maxMs: round(entry.maxMs), runs: entry.runs.size }))
    .sort((a, b) => b.medianMs - a.medianMs)
    .slice(0, 15);
  const scenarios = {};
  for (const child of children.filter((entry) => entry.summary?.kind === "ui-scenario")) {
    const id = child.summary.scenario?.id ?? "unknown";
    const entry = (scenarios[id] ??= { name: child.summary.scenario?.name ?? id, runs: 0, passed: 0, settledMedians: [] });
    entry.runs += 1;
    if (child.summary.status === "passed") {
      entry.passed += 1;
      entry.settledMedians.push(child.summary.stats?.measured?.settledMs?.medianMs ?? null);
    }
    const startup = child.summary.operations?.startup;
    if (startup && id === "startup") {
      (entry.firstFrame ??= []).push(startup.firstFrameMs);
    }
  }
  for (const entry of Object.values(scenarios)) {
    entry.settledMedianMs = round(median(entry.settledMedians));
    delete entry.settledMedians;
    if (entry.firstFrame) {
      entry.firstFrameMedianMs = round(median(entry.firstFrame));
      delete entry.firstFrame;
    }
  }
  const build = children.find((entry) => entry.summary?.kind === "build")?.summary ?? null;
  const evidence = build?.evidence;
  return {
    operations,
    scenarios,
    build: build ? {
      status: build.status,
      classification: build.buildConditions?.classification?.value ?? null,
      totalMs: build.totalDurationMs ?? null,
      jobs: evidence?.jobs ?? null,
      wallMs: evidence?.timing?.wallMs ?? null,
      summedMs: evidence?.timing?.summedJobMs ?? null,
      peakParallel: evidence?.timing?.peakParallelJobs ?? null,
      criticalPathMs: evidence?.criticalPath?.available ? evidence.criticalPath.lengthMs : null,
      causes: (evidence?.rebuild?.causes ?? []).slice(0, 5).map((cause) => ({ input: cause.input, kind: cause.inputKind, compiles: cause.compiles })),
      slowest: (evidence?.slowestCompiles ?? []).slice(0, 5).map((job) => ({ name: job.source ?? job.output, durationMs: job.durationMs })),
    } : null,
  };
}

export function countBySeverity(findings) {
  return Object.fromEntries(SEVERITIES.map((severity) => [severity, findings.filter((finding) => finding.severity === severity).length]));
}

// ---- comparing audits -------------------------------------------------------------

// audit, baseline: normalized audit records (normalize.mjs); runs: normalized
// runs that include both audits' child runs; budgets: budgets.json (raw or
// resolved). Per category, the baseline audit's runs are compared with this
// audit's by regression.mjs, so the same rules decide (several runs each side,
// a change past the budget, no overlap). Findings are matched by id.
export function compareAudits({ audit, baseline, runs, budgets }) {
  const blockers = [];
  if (!audit?.audit || !baseline?.audit) blockers.push("both runs must be audits");
  else {
    if (audit.audit.preset !== baseline.audit.preset) blockers.push(`different presets (${baseline.audit.preset} vs ${audit.audit.preset})`);
    if (audit.audit.rulesVersion !== baseline.audit.rulesVersion) blockers.push(`findings rules v${baseline.audit.rulesVersion} vs v${audit.audit.rulesVersion}`);
  }
  if (blockers.length) return { status: "incompatible", blockers, categories: [], findings: null };
  const resolved = Array.isArray(budgets?.problems) ? budgets : resolveBudgets(budgets);
  const byId = new Map(runs.map((run) => [run.runId, run]));
  const group = (ids) => {
    const out = {};
    for (const id of ids) {
      const run = byId.get(id);
      const category = run && categoryOf(run);
      if (category) (out[category] ??= []).push(run);
    }
    return out;
  };
  const before = group(baseline.audit.runIds);
  const after = group(audit.audit.runIds);
  const categories = [];
  for (const category of [...new Set([...Object.keys(before), ...Object.keys(after)])].sort()) {
    if (!before[category] || !after[category]) {
      categories.push({ category, label: categoryLabel(category), status: "one-side-only", verdict: "insufficient-evidence", reasons: [`only the ${before[category] ? "baseline" : "newer"} audit has ${categoryLabel(category)} runs`], metrics: [], steps: [], functional: { failedRuns: [], failedActions: 0 }, skipped: [] });
      continue;
    }
    const budget = resolved.categories?.[category] ?? null;
    categories.push(analyzeCategory({
      category,
      baselineEntry: { runIds: before[category].map((run) => run.runId), note: `audit ${baseline.runId}` },
      baselineRuns: before[category],
      candidateRuns: after[category],
      budget,
    }));
  }
  const oldFindings = new Map((baseline.audit.findings ?? []).map((finding) => [finding.id, finding]));
  const newFindings = new Map((audit.audit.findings ?? []).map((finding) => [finding.id, finding]));
  const findings = {
    // "New" and "gone" say only that the rule's threshold was or wasn't met
    // in each audit; a finding near its threshold can flip on noise.
    appeared: [...newFindings.values()].filter((finding) => !oldFindings.has(finding.id)),
    resolved: [...oldFindings.values()].filter((finding) => !newFindings.has(finding.id)),
    persisting: [...newFindings.values()].filter((finding) => oldFindings.has(finding.id)).map((finding) => ({ ...finding, before: oldFindings.get(finding.id).impact })),
  };
  const verdicts = categories.map((category) => category.verdict);
  const verdict = verdicts.includes("incompatible") ? "incompatible"
    : verdicts.includes("regression") ? "regression"
    : verdicts.includes("insufficient-evidence") ? "insufficient-evidence"
    : verdicts.includes("improvement") ? "improvement" : "no-meaningful-change";
  return { status: "compared", verdict, baselineAuditId: baseline.runId, auditId: audit.runId, categories, findings };
}

// The newest earlier audit with the same preset that finished.
export function previousAudit(runs, audit) {
  return runs
    .filter((run) => run.kind === "audit" && run.runId < audit.runId && run.audit?.preset === audit.audit?.preset && run.readable && run.supported && run.status !== "running")
    .sort((a, b) => b.runId.localeCompare(a.runId))[0] ?? null;
}

// ---- the AI report -----------------------------------------------------------------

const VERDICT_WORDS = {
  regression: "regression", improvement: "improvement", "no-meaningful-change": "no meaningful change",
  "insufficient-evidence": "not enough runs to tell", incompatible: "not comparable",
};

// Compact Markdown for an agent: what ran, what was found (with rules and
// evidence ids), the comparison, and what this audit could not see.
export function auditMarkdown({ auditRun, summary, comparison = null, redact = (text) => text }) {
  const audit = summary;
  const lines = [];
  const preset = PRESETS[audit.preset] ?? { name: audit.preset };
  lines.push(`# Archivist Observatory audit: ${preset.name}`, "");
  lines.push(`- Audit: ${auditRun.runId} (${audit.status})`);
  lines.push(`- Source: ${audit.source?.branch ?? "detached"} @ ${audit.source?.commit?.slice(0, 12) ?? "unknown"}${audit.source?.dirty ? " (uncommitted changes)" : ""}`);
  lines.push(`- Machine: ${[audit.environment?.platform, audit.environment?.architecture, audit.environment?.cpuModel, audit.environment?.logicalCpus && `${audit.environment.logicalCpus} cores`].filter(Boolean).join(", ")}`);
  lines.push(`- Ran: ${audit.plan?.build ? "an incremental build, then " : ""}${audit.plan?.scenarios?.length ? `${audit.plan.scenarios.join(", ")} × ${audit.plan.repeat} round${audit.plan.repeat === 1 ? "" : "s"}` : "no UI scenarios"}`);
  lines.push(`- Runs: ${(audit.children ?? []).map((child) => `${child.runId} (${child.kind}${child.scenario ? ` ${child.scenario}` : ""}, ${child.status})`).join("; ") || "none"}`);
  lines.push(`- Findings rules v${audit.rulesVersion}; thresholds are listed at the end.`, "");
  if (audit.error) lines.push(`**Audit problem:** ${redact(audit.error)}`, "");

  const findings = audit.findings ?? [];
  lines.push(`## Findings (${findings.length})`, "");
  if (!findings.length) lines.push("No rule's threshold was met. That is not proof that nothing is slow: see what the audit can't see below.", "");
  findings.forEach((finding, index) => {
    lines.push(`${index + 1}. **[${finding.severity}] ${finding.title}** (${finding.subsystem}; rule \`${finding.rule}\`; ${finding.confidence?.text ?? ""})`);
    lines.push(`   ${redact(finding.summary)}`);
    if (finding.evidence?.length) lines.push(`   Evidence: ${finding.evidence.slice(0, 4).map((item) => `${item.runId}${item.operation ? ` ${item.operation}` : ""}${item.step != null ? ` step ${item.step}` : ""}: ${redact(item.text)}`).join("; ")}`);
    lines.push(`   Next: ${finding.nextStep}`);
  });
  lines.push("");

  const tree = audit.highlights?.operations ?? [];
  if (tree.length) {
    lines.push("## Slowest instrumented operations", "", "| Operation | Scenario | Median | Max | Runs |", "|---|---|---:|---:|---:|");
    for (const op of tree.slice(0, 10)) lines.push(`| ${op.name} | ${op.scenario} | ${msText(op.medianMs)} | ${msText(op.maxMs)} | ${op.runs} |`);
    lines.push("");
  }
  if (audit.highlights?.build) {
    const build = audit.highlights.build;
    lines.push("## Build", "", `- ${build.classification ?? "?"} build, ${msText(build.totalMs)} total; ${build.jobs ?? "?"} jobs, ${msText(build.wallMs)} wall, ${msText(build.summedMs)} summed across jobs, peak ${build.peakParallel ?? "?"} at once`);
    if (build.criticalPathMs != null) lines.push(`- Longest dependency chain (approximation): ${msText(build.criticalPathMs)}`);
    if (build.causes?.length) lines.push(`- Rebuilt because: ${build.causes.map((cause) => `${redact(cause.input)} (${cause.compiles} compiles)`).join(", ")}`);
    if (build.slowest?.length) lines.push(`- Slowest jobs: ${build.slowest.map((job) => `${redact(job.name)} ${msText(job.durationMs)}`).join(", ")}`);
    lines.push("");
  }

  lines.push("## Compared with the previous audit", "");
  if (!comparison) lines.push("No earlier audit with this preset, so nothing was compared.", "");
  else if (comparison.status === "incompatible") lines.push(`Not comparable: ${comparison.blockers.join("; ")}.`, "");
  else {
    lines.push(`Baseline audit ${comparison.baselineAuditId}. Overall: ${VERDICT_WORDS[comparison.verdict] ?? comparison.verdict}.`, "");
    for (const category of comparison.categories) {
      lines.push(`- ${category.label}: ${VERDICT_WORDS[category.verdict] ?? category.verdict}${category.reasons?.length ? ` (${category.reasons.join("; ")})` : ""}`);
      for (const metric of [...category.metrics, ...category.steps].filter((entry) => entry.verdict === "regression" || entry.verdict === "improvement" || entry.overBudget)) {
        lines.push(`  - ${metric.label}: ${msText(metric.baseline.median)} → ${msText(metric.candidate.median)} (${metric.deltaPercent > 0 ? "+" : ""}${metric.deltaPercent}%): ${metric.verdict}${metric.overBudget && metric.verdict !== "regression" ? ", over budget but unconfirmed" : ""}`);
      }
    }
    const { appeared = [], resolved = [] } = comparison.findings ?? {};
    if (appeared.length) lines.push(`- Findings new in this audit: ${appeared.map((finding) => finding.id).join(", ")}`);
    if (resolved.length) lines.push(`- Findings gone since the baseline: ${resolved.map((finding) => finding.id).join(", ")}`);
    lines.push("", "A regression needs at least the budget's minimum runs on each side and no overlap between them; one run is never called a regression.", "");
  }

  lines.push("## What this audit can't see", "");
  for (const note of blindSpots(audit)) lines.push(`- ${note}`);
  lines.push("", "## Rules", "", "| Rule | Threshold |", "|---|---|");
  for (const rule of RULES) lines.push(`| ${rule.id} | ${rule.when} |`);
  lines.push("", "To dig further: `npm run observatory -- audit findings <audit-id> --json`, `audit operation <run-id> <action> --json`, `audit timeline <run-id> --json`.", "");
  return lines.join("\n");
}

export function blindSpots(audit) {
  const notes = [
    "Only code wrapped in LiveEvents spans is named; the rest of an action is reported as unattributed time.",
    "Scenarios run a separate app with a fresh profile and seeded data, not your own library.",
    "GPU and compositor time is not measured; frame gaps are what the driver saw, at an assumed 60 Hz.",
  ];
  if ((audit.plan?.repeat ?? 1) < 3) notes.push("Fewer than three rounds: enough to point at something slow, not enough to call a regression.");
  if (audit.plan?.build) notes.push("Build job times come from one build; another build of the same change can differ by seconds.");
  else notes.push("No build in this audit: it measured the binary already in build/frontend-debug.");
  return notes;
}
