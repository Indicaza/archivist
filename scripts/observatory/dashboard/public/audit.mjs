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
// 2: unattributed time is measured against the response window (v2
// ui-operations), idle observation steps are never slow steps, and startup
// phases after the event loop starts are rules of their own.
export const RULES_VERSION = 4;

export const UI_SCENARIOS = ["startup", "sidebar", "workspace", "chat"];
// The AI scenarios that never leave the machine (a local mock provider).
export const AI_SCENARIOS = ["ai-pipeline", "ai-turn"];
// Calls the real provider: only in the opt-in Provider Latency preset.
export const PROVIDER_SCENARIOS = ["ai-provider"];

export const PRESETS = {
  smoke: {
    id: "smoke", name: "Smoke (fast)", scenarios: ["startup", "sidebar", "workspace", "ai-pipeline"], build: false, repeat: 1, fast: true,
    description: "Fast developer check: one round, two measured interactions per step, no warm-up, shorter quiet waits. No build or paid calls. Not a statistical regression test.",
  },
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
  ai: {
    id: "ai", name: "AI pipeline (offline)", scenarios: ["ai-pipeline"], build: false, repeat: 3,
    description: "Sends chat messages against a synthetic Library and conversations in a disposable profile, with a local mock provider that answers at once: preparation, retrieval, context compilation, database work and presentation. No API calls.",
  },
  "ai-turn": {
    id: "ai-turn", name: "Full AI turn (mocked)", scenarios: ["ai-turn"], build: false, repeat: 3,
    description: "Whole AI turns against a local mock provider that is slow to answer, streams with pauses, refuses once and retries, calls a tool, and is cancelled part way. No API calls.",
  },
  provider: {
    id: "provider", name: "Provider latency (real calls)", scenarios: PROVIDER_SCENARIOS, build: false, repeat: 1, providerCalls: true,
    description: "Sends 3 short synthetic messages to the AI provider you have configured, through a local proxy that allows at most 6 requests and 64 output tokens each. This costs a little money and sends text to the provider. Only runs when you confirm it; never part of another preset.",
  },
  complete: {
    id: "complete", name: "Complete", scenarios: [...UI_SCENARIOS, ...AI_SCENARIOS], build: true, repeat: 3,
    description: "The build preset, then every UI scenario and both offline AI scenarios, three rounds each, against the binary it built. Never calls the AI provider.",
  },
};

export const SEVERITIES = ["high", "medium", "low", "info"];
const SEVERITY_RANK = Object.fromEntries(SEVERITIES.map((name, index) => [name, index]));

// Every rule, as shown on the page and in reports. `when` is the threshold in
// words; the code below applies exactly that.
export const RULES = [
  { id: "ui.failed-actions", subsystem: "ui-lab", title: "Actions failed", when: "Any scenario run did not pass, or any action failed.", severity: "high", basis: "UI Lab action records" },
  { id: "ui.no-operations", subsystem: "instrumentation", title: "No operations reported", when: "The app build didn't report LiveEvents operations to the UI Lab.", severity: "info", basis: "summary.operations.collected" },
  { id: "ui.slow-step", subsystem: "ui", title: "Slow step", when: "A step's settled latency, median over measured actions, is 100 ms or more (250 ms or more: high). Idle observation steps (op noop) are not interactions and never count; chat sends (op sendChatMessage) are AI turns, left to the AI rules.", severity: "medium / high", basis: "settled = input until the last frame before rendering went quiet" },
  { id: "ui.sparse-settled", subsystem: "instrumentation", title: "Not enough settled-frame samples", when: "A potentially slow step has fewer than two-thirds of repeated runs with a valid settled-frame measurement.", severity: "info", basis: "valid settled measurements vs completed scenario runs" },
  { id: "ui.slow-operation", subsystem: "(from the operation)", title: "Slow operation", when: "An instrumented operation's median inside a step is 50 ms or more (200 ms or more: high). AI turn spans (ai.*) and database queue waits are left to the AI and database rules.", severity: "medium / high", basis: "LiveEvents spans tagged with the running action" },
  { id: "ui.stalls", subsystem: "ui-thread", title: "Event-loop stalls", when: "The UI Lab probe fired 50 ms or more late during measured actions.", severity: "medium", basis: "50 ms probe on the UI thread" },
  { id: "ui.unattributed", subsystem: "instrumentation", title: "Time no operation explains", when: "In an interaction whose response window is 50 ms or more, half or more of that window is inside no instrumented operation.", severity: "low", basis: "response window (input to settled) minus the union of the spans inside it; both medians from the same actions (ui-operations v2)" },
  { id: "ui.frame-gap", subsystem: "rendering", title: "Long frame gaps", when: "The longest gap between frames, median over measured actions, is over 33.4 ms (two frames at an assumed 60 Hz).", severity: "low", basis: "frame swaps seen by the driver; 60 Hz is an assumption" },
  { id: "startup.slow-stage", subsystem: "startup", title: "Slow startup stage", when: "A startup stage measured in main() takes 250 ms or more (1000 ms or more: high).", severity: "medium / high", basis: "LiveEvents spans in main()" },
  { id: "startup.slow-phase", subsystem: "startup", title: "Slow startup phase", when: "The event loop to first frame, or first frame to usable UI, takes 250 ms or more (1000 ms or more: high).", severity: "medium / high", basis: "startup markers and the driver's ready check (ui-operations v2 phases)" },
  { id: "ai.failed-turns", subsystem: "ai", title: "AI turns failed", when: "An AI turn ended failed or interrupted (a cancelled turn is not a failure).", severity: "high", basis: "ai.run outcome and its http attempts (trace schema 1)" },
  { id: "ai.slow-preparation", subsystem: "ai", title: "Slow AI turn preparation", when: "Preparing a turn (loading the conversation, attachments, retrieval, context) takes 250 ms or more, median over turns (1000 ms or more: high).", severity: "medium / high", basis: "ai.prepare span and its stages" },
  { id: "ai.slow-retrieval", subsystem: "retrieval", title: "Slow Library retrieval", when: "Retrieval for a turn takes 150 ms or more, median over turns (500 ms or more: high).", severity: "medium / high", basis: "ai.retrieval span (library search, ranking, selection)" },
  { id: "ai.slow-context-compile", subsystem: "context", title: "Slow context compilation", when: "Compiling the context for a turn takes 100 ms or more, median over turns (400 ms or more: high).", severity: "medium / high", basis: "ai.context-compile span, with its message counts and estimated tokens" },
  { id: "db.queue-contention", subsystem: "database", title: "Database jobs wait in the queue", when: "A database job's median queue wait is 20 ms or more and at least as long as its execution (100 ms or more: high).", severity: "medium / high", basis: "db.queue-wait vs db.execute under each db job" },
  { id: "db.ui-thread-sync", subsystem: "ui-thread", title: "Database work on the UI thread", when: "A synchronous database read ran on the UI thread for 16 ms or more at once (one frame; 50 ms or more: high).", severity: "medium / high", basis: "db.execute spans whose thread is main" },
  { id: "ai.slow-tool", subsystem: "tools", title: "Slow tool call", when: "A tool's median duration is 250 ms or more (1000 ms or more: high).", severity: "medium / high", basis: "ai.tool spans" },
  { id: "ai.excess-tool-rounds", subsystem: "ai", title: "Many tool rounds in one turn", when: "A turn ran 4 or more tool rounds.", severity: "medium", basis: "ai.tool-round spans" },
  { id: "ai.first-token-latency", subsystem: "ai", title: "Slow first token", when: "The first model text arrives 1500 ms or more after the turn starts, median over turns (4000 ms or more: high). Split into Archivist's preparation and the wait for the provider.", severity: "medium / high", basis: "ai.first-delta marker, ai.prepare, ai.first-byte marker" },
  { id: "ai.stream-pauses", subsystem: "provider", title: "Pauses in the stream", when: "A turn's stream had a pause of 1 s or more between provider events, excluding explicitly expected mock-fixture pauses.", severity: "medium", basis: "provider round longGaps and maxGapMs (counted per event, never logged per token)" },
  { id: "ai.retries", subsystem: "provider", title: "Provider requests retried", when: "A provider request was refused and retried, excluding explicitly expected mock-fixture retries (1 s or more of waiting: medium).", severity: "low / medium", basis: "ai.http-attempt and ai.retry-wait spans" },
  { id: "ai.slow-presentation", subsystem: "ui", title: "Slow to publish initial reply text", when: "The first model text takes 100 ms or more to appear in the chat after it arrived, median over turns (250 ms or more: high).", severity: "medium / high", basis: "ai.first-delta vs ChatStore text publication marker, not actual screen paint" },
  { id: "ai.unattributed", subsystem: "instrumentation", title: "AI turn time no stage explains", when: "50 ms or more, and 20% or more, of a turn is inside no child span of ai.run, median over turns.", severity: "low", basis: "ai.run self time (no child durations are invented)" },
  { id: "ai.missing-instrumentation", subsystem: "instrumentation", title: "AI turn stages not measured", when: "A turn is missing a stage a fully instrumented turn reports.", severity: "info", basis: "trace analysis, expected stages" },
  { id: "trace.dropped-events", subsystem: "instrumentation", title: "Trace events dropped", when: "The recording buffer was full and events were dropped, so totals may be low.", severity: "info", basis: "operations dropped counter" },
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

export const OBSERVATION_OPS = new Set(["noop"]);
// Spans the AI and database rules read from the trace, with their own
// splits (provider wait vs Archivist's work, queue vs execution). The
// generic slow-operation rule leaves them alone so a cause is reported once.
const TRACE_RULED = (name) => /^(ai|retrieval|context)\./.test(name) || ["chat.ai-presentation", "db.read", "db.write", "db.run", "db.queue-wait", "db.callback-delay"].includes(name);
// Steps that send a chat message: their latency is the AI turn, which the
// AI rules split into its stages, so the generic slow-step rule skips them.
export const AI_TURN_OPS = new Set(["sendChatMessage"]);
const STARTUP_PHASE_RULED = new Set(["first-frame", "first-usable"]);

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

    // Steps: settled latency, unattributed time, stalls. Observation steps
    // (the idle after startup) only watch the app, so they are skipped.
    const observation = new Set();
    const aiSteps = new Set();
    for (const run of passed) {
      for (const step of run.summary.operations?.steps ?? []) {
        if (step.kind === "observation") observation.add(`${step.step}:${step.label}`);
      }
      for (const step of run.summary.stats?.steps ?? []) {
        if (OBSERVATION_OPS.has(step.op)) observation.add(`${step.step}:${step.label ?? step.op}`);
        if (AI_TURN_OPS.has(step.op)) aiSteps.add(`${step.step}:${step.label ?? step.op}`);
      }
    }
    const stepKeys = new Map();
    for (const run of passed) {
      for (const step of run.summary.stats?.steps ?? []) {
        const key = `${step.step}:${step.label ?? step.op}`;
        if (observation.has(key) || aiSteps.has(key)) continue;
        if (!stepKeys.has(key)) stepKeys.set(key, { step: step.step, label: step.label ?? step.op, values: [] });
        stepKeys.get(key).values.push({ runId: run.runId, ms: step.settledMs?.medianMs ?? null });
      }
    }
    for (const [key, entry] of stepKeys) {
      const typical = median(entry.values.map((value) => value.ms));
      if (typical == null || typical < 100) continue;
      const valid = entry.values.filter((value) => typeof value.ms === "number" && Number.isFinite(value.ms));
      const minimum = passed.length === 1 ? 1 : Math.ceil(passed.length * 2 / 3);
      if (valid.length < minimum) {
        add("ui.sparse-settled", `${scenario}:${key}`, {
          subsystem: "instrumentation", severity: "info",
          summary: `${name}, step ${entry.step} (${entry.label}): only ${valid.length} of ${passed.length} runs measured settled-frame latency. The ${msText(typical)} observed value is not sufficient to rank this step as slow.`,
          impact: null,
          evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, text: `settled ${msText(value.ms)}` })),
          confidence: confidence(valid.length, passed.length),
          nextStep: "Inspect missing or timed-out frame settling, active animations and UI-thread stalls before comparing this step.",
          scenario,
        });
        continue;
      }
      const seen = valid.filter((value) => value.ms >= 100).length;
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
          if (TRACE_RULED(op.name)) continue;
          const key = `${op.name}\t${stepKey}`;
          if (!opStats.has(key)) opStats.set(key, { name: op.name, step: step.step, label: step.label, values: [] });
          opStats.get(key).values.push({ runId: run.runId, ms: op.medianMs, max: op.maxMs, n: op.n });
          if (op.parent) opStats.get(key).parent = op.parent;
        }
        if (step.stalls) {
          if (!stallStats.has(stepKey)) stallStats.set(stepKey, { step: step.step, label: step.label, values: [] });
          stallStats.get(stepKey).values.push({ runId: run.runId, stalls: step.stalls, runs: step.runs });
        }
        // Only v2 operations measure unattributed time and its window the
        // same way; older runs are left out rather than compared wrongly.
        if (step.unattributedMedianMs != null && step.windowMedianMs != null && !observation.has(stepKey) && !step.expectedTransition) {
          if (!unattributed.has(stepKey)) unattributed.set(stepKey, { step: step.step, label: step.label, values: [] });
          unattributed.get(stepKey).values.push({ runId: run.runId, ms: step.unattributedMedianMs, window: step.windowMedianMs, basis: step.windowBasis });
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
      const window = median(entry.values.map((value) => value.window));
      if (typical == null || window == null || window < 50 || typical < window / 2) continue;
      add("ui.unattributed", `${scenario}:${stepKey}`, {
        subsystem: "instrumentation", severity: "low",
        summary: `${name}, step ${entry.step} (${entry.label}): ${msText(typical)} of its ${msText(window)} response window is inside no instrumented operation.`,
        impact: { value: round(typical), unit: "ms", text: `${pct(typical, window)}% of the response window unexplained` },
        evidence: entry.values.map((value) => ({ runId: value.runId, step: entry.step, text: `unattributed median ${msText(value.ms)} of a ${msText(value.window)} window (${value.basis})` })),
        confidence: confidence(entry.values.length, passed.length),
        nextStep: "Add a LiveEvents span around the code this step runs (QML bindings, model resets, layout), then audit again.",
        scenario,
      });
    }

    aiFindings({ scenario, name, passed, add });

    const gaps = passed.map((run) => ({ runId: run.runId, ms: run.summary.stats?.measured?.frameMaxIntervalMs?.medianMs ?? null }));
    const gap = median(gaps.map((value) => value.ms));
    const validGaps = gaps.filter((value) => typeof value.ms === "number" && Number.isFinite(value.ms));
    if (gap != null && gap > 33.4 && scenario !== "startup" && validGaps.length >= (passed.length === 1 ? 1 : Math.ceil(passed.length * 2 / 3))) {
      add("ui.frame-gap", scenario, {
        subsystem: "rendering", severity: "low",
        summary: `${name}: the longest gap between frames is ${msText(gap)} (median over measured actions); at 60 Hz that spans about ${Math.floor(gap / 16.7)} frame intervals (not a measured dropped-frame count).`,
        impact: { value: round(gap), unit: "ms", text: `${msText(gap)} median longest gap` },
        evidence: gaps.map((value) => ({ runId: value.runId, text: `median longest gap ${msText(value.ms)}` })),
        confidence: confidence(validGaps.filter((value) => value.ms > 33.4).length, passed.length),
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
      const phases = new Map();
      for (const run of passed) {
        for (const phase of run.summary.operations?.startup?.phases ?? []) {
          if (!STARTUP_PHASE_RULED.has(phase.id) || phase.ms == null) continue;
          if (!phases.has(phase.id)) phases.set(phase.id, { label: phase.label, values: [] });
          phases.get(phase.id).values.push({ runId: run.runId, ms: phase.ms });
        }
      }
      for (const [id, entry] of phases) {
        const typical = median(entry.values.map((value) => value.ms));
        if (typical == null || typical < 250) continue;
        add("startup.slow-phase", id, {
          subsystem: "startup", severity: typical >= 1000 ? "high" : "medium",
          summary: `Startup phase "${entry.label}" takes ${msText(typical)}.`,
          impact: { value: round(typical), unit: "ms", text: `${msText(typical)} median` },
          evidence: entry.values.map((value) => ({ runId: value.runId, operation: `startup.${id}`, text: msText(value.ms) })),
          confidence: confidence(entry.values.filter((value) => value.ms >= 250).length, entry.values.length),
          nextStep: id === "first-frame"
            ? "This is work QML deferred past the event loop (Component.onCompleted, Loaders, first polish and sync). Add spans or markers in the components created at startup."
            : "This is what runs between the first frame and the ready check. Look at the operations after first-frame on the startup timeline.",
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

// ---- AI turn and database findings (from the trace) ----------------------------

// What a trace finding says about where the time came from: a mocked
// provider's delays were put there by the scenario, so they are labelled.
function providerNote(passed) {
  const provider = passed[0]?.summary.ai?.provider ?? null;
  return provider === "mock" ? " (injected by the mock provider)" : provider === "real" ? " (real provider)" : "";
}

function aiFindings({ scenario, name, passed, add }) {
  const turns = [];
  const db = [];
  const dropped = [];
  for (const run of passed) {
    const trace = run.summary.operations?.trace;
    if (!trace?.available) continue;
    // runId is the Observatory run (evidence links to it); the turn's own
    // id is kept as aiRunId.
    for (const turn of trace.aiTurns ?? []) turns.push({ ...turn, aiRunId: turn.runId, runId: run.runId });
    for (const job of trace.db ?? []) db.push({ ...job, runId: run.runId });
    if (trace.dropped) dropped.push({ runId: run.runId, n: trace.dropped });
  }
  const injected = providerNote(passed);
  const expectedMock = (kind) => passed.length > 0 && passed.every((run) => run.summary.ai?.provider === "mock" && run.summary.ai?.expected?.[kind] === true);
  const runsWith = (list) => new Set(list.map((entry) => entry.runId)).size;
  const evidence = (list, text) => list.map((entry) => ({ runId: entry.runId, operation: "ai.run", aiRunId: entry.aiRunId ?? null, text: text(entry) }));

  if (dropped.length) {
    add("trace.dropped-events", scenario, {
      subsystem: "instrumentation", severity: "info",
      summary: `${name}: the recording dropped ${dropped.reduce((sum, entry) => sum + entry.n, 0)} events, so counts and totals here may be low.`,
      impact: null,
      evidence: dropped.map((entry) => ({ runId: entry.runId, text: `${entry.n} dropped` })),
      confidence: confidence(dropped.length, passed.length),
      nextStep: "Record fewer rounds per run, or raise the session's event limit.",
      scenario,
    });
  }

  if (turns.length) {
    const failed = turns.filter((turn) => turn.outcome === "failed" || turn.outcome === "interrupted");
    if (failed.length) {
      add("ai.failed-turns", scenario, {
        subsystem: "ai", severity: "high",
        summary: `${name}: ${failed.length} of ${turns.length} AI turn${turns.length === 1 ? "" : "s"} failed${injected}; first: ${describeAttempts(failed[0].failedAttempts)}.`,
        impact: { value: failed.length, unit: "turns", text: `${failed.length} of ${turns.length} turns` },
        evidence: evidence(failed, (turn) => `${turn.outcome}: ${describeAttempts(turn.failedAttempts)}`),
        confidence: confidence(runsWith(failed), passed.length),
        nextStep: "Open the turn on the timeline: the failed attempt shows its status or error kind and how far the stream got.",
        scenario,
      });
    }

    const slowMetric = (rule, value, { medium, high, subsystem, label, detail, next }) => {
      const values = turns.map((turn) => ({ turn, ms: value(turn) })).filter((entry) => entry.ms != null);
      const typical = median(values.map((entry) => entry.ms));
      if (typical == null || typical < medium) return;
      add(rule, scenario, {
        subsystem, severity: typical >= high ? "high" : "medium",
        summary: `${name}: ${label} takes ${msText(typical)} per AI turn (median of ${values.length})${detail ? `; ${detail(values)}` : ""}.`,
        impact: { value: round(typical), unit: "ms", text: `${msText(typical)} median, up to ${msText(Math.max(...values.map((entry) => entry.ms)))}` },
        evidence: perRun(values).map((entry) => ({ runId: entry.runId, operation: "ai.run", text: entry.n === 1 ? msText(entry.ms) : `median ${msText(entry.ms)} over ${entry.n} turns (up to ${msText(entry.max)})` })),
        confidence: confidence(runsWith(values.filter((entry) => entry.ms >= medium).map((entry) => entry.turn)), passed.length),
        nextStep: next,
        scenario,
      });
    };
    slowMetric("ai.slow-preparation", (turn) => turn.prepareMs, {
      medium: 250, high: 1000, subsystem: "ai", label: "preparing the turn",
      detail: (values) => {
        const stages = Object.entries(values[0].turn.stages ?? {}).map(([key, _]) => [key, median(values.map((entry) => entry.turn.stages?.[key]))]).filter(([, ms]) => ms != null).sort((a, b) => b[1] - a[1]);
        return stages.length ? `the largest stage is ${stages[0][0].replace(/Ms$/, "")} (${msText(stages[0][1])})` : "no stage inside it is measured";
      },
      next: "The prepare stages on the turn timeline show which one holds the time; prepare self time is work no stage covers.",
    });
    slowMetric("ai.slow-retrieval", (turn) => turn.stages?.retrievalMs, {
      medium: 150, high: 500, subsystem: "retrieval", label: "Library retrieval",
      next: "Retrieval's children split full-text query, ranking and index repair from selection.",
    });
    slowMetric("ai.slow-context-compile", (turn) => turn.stages?.contextCompileMs, {
      medium: 100, high: 400, subsystem: "context", label: "context compilation",
      detail: (values) => {
        const context = values.map((entry) => entry.turn.context).find(Boolean);
        return context ? `${context.compiler ?? "compiler"} over ${context.sourceMessages ?? "?"} messages, ${context.included ?? "?"} included, about ${context.estimatedTokens ?? "?"} tokens` : "";
      },
      next: "context.strategy's children show chat search and assembly; its self time is the strategy's own selection.",
    });
    slowMetric("ai.first-token-latency", (turn) => turn.firstModelDeltaMs, {
      medium: 1500, high: 4000, subsystem: "ai", label: "the first model text",
      detail: (values) => {
        const before = median(values.map((entry) => entry.turn.prepareMs));
        const toByte = median(values.map((entry) => (entry.turn.firstNetworkByteMs != null && entry.turn.prepareMs != null ? entry.turn.firstNetworkByteMs - entry.turn.prepareMs : null)));
        const queue = median(values.map((entry) => entry.turn.queueWaitMs));
        const retried = values.some((entry) => (entry.turn.retries ?? 0) > 0);
        return `about ${msText(before)} is Archivist preparing the request, about ${msText(toByte)} the wait for the provider's first byte${retried ? " (retries and their waits included)" : ""}${injected}${queue ? `, plus ${msText(queue)} queued before the turn started` : ""}`;
      },
      next: "Compare the provider's first-byte wait with preparation on the turn timeline; retries and their waits sit inside the first round.",
    });
    slowMetric("ai.slow-presentation", (turn) => (turn.firstVisibleTextMs != null && turn.firstModelDeltaMs != null ? turn.firstVisibleTextMs - turn.firstModelDeltaMs : null), {
      medium: 100, high: 250, subsystem: "ui", label: "showing the first text after it arrived",
      detail: (values) => {
        const flush = Math.max(0, ...values.map((entry) => entry.turn.presentation?.flushMaxMs ?? 0));
        return flush >= 1 ? `the slowest ChatStore text flush took ${msText(flush)}` : "ChatStore flush work was under 1 ms; remaining delay includes delivery and batching, not measured screen paint";
      },
      next: "The marker is emitted after ChatStore publishes text to QML, before screen paint; inspect queued delivery, batching and correlated UI stalls.",
    });

    const tools = new Map();
    for (const turn of turns) {
      for (const call of turn.toolCalls ?? []) {
        if (!tools.has(call.tool)) tools.set(call.tool, []);
        tools.get(call.tool).push({ runId: turn.runId, ms: call.durationMs, ok: call.ok });
      }
    }
    for (const [tool, calls] of tools) {
      const typical = median(calls.map((entry) => entry.ms));
      if (typical == null || typical < 250) continue;
      add("ai.slow-tool", `${scenario}:${tool}`, {
        subsystem: "tools", severity: typical >= 1000 ? "high" : "medium",
        summary: `${name}: the ${tool} tool takes ${msText(typical)} (median of ${calls.length} call${calls.length === 1 ? "" : "s"}).`,
        impact: { value: round(typical), unit: "ms", text: `${msText(typical)} median` },
        evidence: calls.map((entry) => ({ runId: entry.runId, operation: "ai.tool", text: `${msText(entry.ms)}${entry.ok === false ? ", failed" : ""}` })),
        confidence: confidence(runsWith(calls.filter((entry) => entry.ms >= 250)), passed.length),
        nextStep: `Add spans inside ${tool} if its database or file work isn't already a child of ai.tool.`,
        scenario, operation: "ai.tool",
      });
    }

    const manyRounds = turns.filter((turn) => (turn.toolRounds ?? 0) >= 4);
    if (manyRounds.length) {
      const most = Math.max(...manyRounds.map((turn) => turn.toolRounds));
      add("ai.excess-tool-rounds", scenario, {
        subsystem: "ai", severity: "medium",
        summary: `${name}: ${manyRounds.length} turn${manyRounds.length === 1 ? "" : "s"} ran 4 or more tool rounds (up to ${most}); each round is another provider request.`,
        impact: { value: most, unit: "rounds", text: `up to ${most} tool rounds` },
        evidence: evidence(manyRounds, (turn) => `${turn.toolRounds} tool rounds, ${turn.tools} tool calls`),
        confidence: confidence(runsWith(manyRounds), passed.length),
        nextStep: "Check which tools repeat across rounds on the turn timeline; repeated lookups can often be answered from context.",
        scenario,
      });
    }

    const paused = turns.filter((turn) => (turn.longStreamGaps ?? 0) > 0);
    if (paused.length && !expectedMock("streamPauses")) {
      const longest = Math.max(...paused.map((turn) => turn.maxStreamGapMs ?? 0));
      add("ai.stream-pauses", scenario, {
        subsystem: "provider", severity: "medium",
        summary: `${name}: ${paused.length} turn${paused.length === 1 ? "" : "s"} paused 1 s or more mid-stream${injected}; the longest pause was ${msText(longest)}.`,
        impact: { value: round(longest), unit: "ms", text: `${msText(longest)} longest pause` },
        evidence: evidence(paused, (turn) => `${turn.longStreamGaps} pause${turn.longStreamGaps === 1 ? "" : "s"} of 1 s or more, longest ${msText(turn.maxStreamGapMs)}`),
        confidence: confidence(runsWith(paused), passed.length),
        nextStep: "A pause between provider events is the provider's (or the network's) time, not Archivist's; compare with a Provider Latency audit.",
        scenario,
      });
    }

    const retried = turns.filter((turn) => (turn.retries ?? 0) > 0);
    if (retried.length && !expectedMock("retries")) {
      const waited = retried.reduce((sum, turn) => sum + (turn.retryWaitMs ?? 0), 0);
      add("ai.retries", scenario, {
        subsystem: "provider", severity: waited >= 1000 ? "medium" : "low",
        summary: `${name}: ${retried.length} turn${retried.length === 1 ? "" : "s"} retried a refused request${injected}, waiting ${msText(waited)} in all.`,
        impact: { value: round(waited), unit: "ms", text: `${msText(waited)} spent waiting to retry` },
        evidence: evidence(retried, (turn) => `${turn.retries} retr${turn.retries === 1 ? "y" : "ies"} (${describeAttempts(turn.failedAttempts)}), waited ${msText(turn.retryWaitMs)}`),
        confidence: confidence(runsWith(retried), passed.length),
        nextStep: "Retries are the provider refusing (429/5xx) and Archivist waiting as it asked; frequent ones point at rate limits.",
        scenario,
      });
    }

    const loose = turns.filter((turn) => turn.unattributedMs != null && turn.durationMs);
    const typicalLoose = median(loose.map((turn) => turn.unattributedMs));
    const typicalDuration = median(loose.map((turn) => turn.durationMs));
    if (typicalLoose != null && typicalLoose >= 50 && typicalLoose >= typicalDuration * 0.2) {
      add("ai.unattributed", scenario, {
        subsystem: "instrumentation", severity: "low",
        summary: `${name}: ${msText(typicalLoose)} of a ${msText(typicalDuration)} AI turn is inside no stage (median).`,
        impact: { value: round(typicalLoose), unit: "ms", text: `${pct(typicalLoose, typicalDuration)}% of the turn unexplained` },
        evidence: evidence(loose, (turn) => `${msText(turn.unattributedMs)} of ${msText(turn.durationMs)}`),
        confidence: confidence(runsWith(loose.filter((turn) => turn.unattributedMs >= 50)), passed.length),
        nextStep: "Gaps between the stages on the turn timeline are where the next span belongs.",
        scenario,
      });
    }

    const missing = new Map();
    for (const turn of turns) for (const item of turn.missing ?? []) missing.set(item, (missing.get(item) ?? 0) + 1);
    if (missing.size) {
      add("ai.missing-instrumentation", scenario, {
        subsystem: "instrumentation", severity: "info",
        summary: `${name}: not measured in some turns: ${[...missing.keys()].join("; ")}.`,
        impact: null,
        evidence: [...missing].map(([item, n]) => ({ text: `${item}: ${n} of ${turns.length} turns` })),
        confidence: confidence(runsWith(turns.filter((turn) => turn.missing?.length)), passed.length),
        nextStep: "A missing stage is reported, not counted as zero; rebuild the app from this branch if stages are missing everywhere.",
        scenario,
      });
    }
  }

  // Database jobs, by kind and op, over the runs.
  const jobs = new Map();
  for (const job of db) {
    const key = `${job.kind}:${job.op}`;
    if (!jobs.has(key)) jobs.set(key, { kind: job.kind, op: job.op, values: [] });
    jobs.get(key).values.push(job);
  }
  for (const [key, entry] of jobs) {
    const queue = median(entry.values.map((job) => job.queueWait?.medianMs));
    const execute = median(entry.values.map((job) => job.execute?.medianMs)) ?? 0;
    if (queue != null && queue >= 20 && queue >= execute) {
      add("db.queue-contention", `${scenario}:${key}`, {
        subsystem: "database", severity: queue >= 100 ? "high" : "medium",
        summary: `${name}: database ${entry.kind} "${entry.op}" waits ${msText(queue)} in the queue for ${msText(execute)} of work (medians); other jobs hold the database thread first.`,
        impact: { value: round(queue), unit: "ms", text: `${msText(queue)} median queue wait` },
        evidence: entry.values.map((job) => ({ runId: job.runId, operation: `db.${entry.kind}`, text: `${job.n} jobs, queue ${msText(job.queueWait?.medianMs)} (max ${msText(job.queueWait?.maxMs)}), execute ${msText(job.execute?.medianMs)}` })),
        confidence: confidence(runsWith(entry.values.filter((job) => (job.queueWait?.medianMs ?? 0) >= 20)), passed.length),
        nextStep: "Find what runs on the database thread just before on the timeline; large writes or scans ahead of it are the cause, not this job's query.",
        scenario, operation: `db.${entry.kind}`,
      });
    }
    const sync = entry.values.filter((job) => job.uiThreadSync);
    const worstSync = Math.max(0, ...sync.map((job) => job.uiThreadSync.maxMs ?? 0));
    if (worstSync >= 16) {
      add("db.ui-thread-sync", `${scenario}:${key}`, {
        subsystem: "ui-thread", severity: worstSync >= 50 ? "high" : "medium",
        summary: `${name}: database ${entry.kind} "${entry.op}" ran on the UI thread for up to ${msText(worstSync)} at once, blocking input and drawing.`,
        impact: { value: round(worstSync), unit: "ms", text: `${msText(worstSync)} longest blocking read` },
        evidence: sync.map((job) => ({ runId: job.runId, operation: `db.${entry.kind}`, text: `${job.uiThreadSync.n} on the UI thread, ${msText(job.uiThreadSync.totalMs)} in all, longest ${msText(job.uiThreadSync.maxMs)}` })),
        confidence: confidence(runsWith(sync.filter((job) => (job.uiThreadSync.maxMs ?? 0) >= 16)), passed.length),
        nextStep: "Move this read to CoreRuntime::read's asynchronous form, or page it.",
        scenario, operation: `db.${entry.kind}`,
      });
    }
  }
}

// One evidence line per Observatory run, not per turn.
function perRun(values) {
  const runs = new Map();
  for (const entry of values) {
    if (!runs.has(entry.turn.runId)) runs.set(entry.turn.runId, []);
    runs.get(entry.turn.runId).push(entry.ms);
  }
  return [...runs].map(([runId, list]) => ({ runId, ms: median(list), max: Math.max(...list), n: list.length }));
}

function describeAttempts(attempts = []) {
  if (!attempts.length) return "no failed request";
  return attempts.map((attempt) => (attempt.interrupted ? `stream cut off (${attempt.error ?? "closed"})` : attempt.status ? `HTTP ${attempt.status}` : attempt.error ?? "error")).join(", ");
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
    ai: aiHighlights(children),
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

// AI turns per scenario: medians over completed turns, and how many turns
// ended each way. Cancelled and failed turns are counted, never timed.
const AI_TIMINGS = [
  ["turnMs", "Turn (engine)", (turn) => turn.durationMs], ["queueWaitMs", "Queued", (turn) => turn.queueWaitMs], ["prepareMs", "Prepare", (turn) => turn.prepareMs],
  ["retrievalMs", "Retrieval", (turn) => turn.stages?.retrievalMs], ["contextCompileMs", "Context", (turn) => turn.stages?.contextCompileMs],
  ["firstNetworkByteMs", "First byte", (turn) => turn.firstNetworkByteMs], ["firstModelDeltaMs", "First model text", (turn) => turn.firstModelDeltaMs],
  ["firstVisibleTextMs", "First text published (UI)", (turn) => turn.firstVisibleTextMs], ["persistMs", "Save", (turn) => turn.persistMs],
];
export function aiHighlights(children) {
  const out = {};
  for (const child of children) {
    const summary = child.summary;
    const turns = summary?.kind === "ui-scenario" ? summary.operations?.trace?.aiTurns ?? [] : [];
    if (!turns.length) continue;
    const id = summary.scenario?.id ?? "unknown";
    const entry = (out[id] ??= { name: summary.scenario?.name ?? id, provider: summary.ai?.provider ?? null, runs: 0, turns: [] });
    entry.runs += 1;
    entry.turns.push(...turns);
    const expected = summary.ai?.provider === "mock" ? summary.ai.expected : null;
    if (!entry.expected) entry.expected = { retries: expected?.retries === true, streamPauses: expected?.streamPauses === true };
    else {
      entry.expected.retries = entry.expected.retries && expected?.retries === true;
      entry.expected.streamPauses = entry.expected.streamPauses && expected?.streamPauses === true;
    }
  }
  for (const entry of Object.values(out)) {
    const turns = entry.turns;
    const done = turns.filter((turn) => turn.outcome === "completed");
    entry.counts = {
      turns: turns.length, completed: done.length, cancelled: turns.filter((turn) => turn.outcome === "cancelled").length,
      failed: turns.filter((turn) => turn.outcome === "failed" || turn.outcome === "interrupted").length,
      retries: turns.reduce((sum, turn) => sum + (turn.retries ?? 0), 0), tools: turns.reduce((sum, turn) => sum + (turn.tools ?? 0), 0),
      retriedTurns: turns.filter((turn) => (turn.retries ?? 0) > 0).length,
      pausedTurns: turns.filter((turn) => (turn.longStreamGaps ?? 0) > 0).length,
    };
    entry.medians = Object.fromEntries(AI_TIMINGS.map(([key, , read]) => [key, round(median(done.map(read)))]));
    delete entry.turns;
  }
  return out;
}

// One finding on its own, for "Copy this finding" and agents.
export function findingMarkdown(finding, { auditId = null, redact = (text) => text } = {}) {
  const rule = RULE_BY_ID[finding.rule];
  return [
    `## [${finding.severity}] ${finding.title}`, "",
    redact(finding.summary), "",
    `- Rule: \`${finding.rule}\`: ${rule?.when ?? ""}`,
    `- Basis: ${rule?.basis ?? "?"}`,
    `- Subsystem: ${finding.subsystem}; ${finding.confidence?.text ?? ""}`,
    finding.impact ? `- Impact: ${finding.impact.text}` : null,
    `- Next: ${finding.nextStep}`,
    finding.evidence?.length ? `- Evidence:\n${finding.evidence.map((item) => `  - ${item.runId ?? ""}${item.operation ? ` ${item.operation}` : ""}${item.step != null ? ` step ${item.step}` : ""}: ${redact(item.text)}`).join("\n")}` : null,
    auditId ? `\nFrom audit ${auditId}. More: \`npm run observatory -- audit findings ${auditId} --json\`${finding.evidence?.[0]?.runId && finding.rule.startsWith("ai.") ? `, \`audit trace ${finding.evidence[0].runId} --json\`` : ""}.` : null,
    "",
  ].filter((line) => line != null).join("\n");
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
  const ai = Object.entries(audit.highlights?.ai ?? {});
  if (ai.length) {
    lines.push("## AI turns", "", "Medians over completed turns. Queued is measured before ai.run; other timestamps are offsets from ai.run start, while Turn (engine) is its duration. The UI publication marker may occur after ai.run ends and is not actual screen paint. Columns are independently aggregated. Cancelled and failed turns are counted, not timed. A mock provider's waits are scripted.", "");
    lines.push(`| Scenario | Provider | Turns | ${AI_TIMINGS.map(([, label]) => label).join(" | ")} |`, `|---|---|---:|${AI_TIMINGS.map(() => "---:").join("|")}|`);
    for (const [, entry] of ai) {
      const counts = entry.counts;
      const ended = [counts.cancelled && `${counts.cancelled} cancelled`, counts.failed && `${counts.failed} failed`].filter(Boolean).join(", ");
      lines.push(`| ${entry.name} | ${entry.provider ?? "?"} | ${counts.completed}/${counts.turns}${ended ? ` (${ended})` : ""} | ${AI_TIMINGS.map(([key]) => msText(entry.medians[key])).join(" | ")} |`);
    }
    lines.push("");
    const expectedChecks = ai.filter(([, entry]) => entry.provider === "mock" && (entry.expected?.retries || entry.expected?.streamPauses));
    if (expectedChecks.length) {
      lines.push("## Expected mock fixture events (not performance findings)", "");
      for (const [, entry] of expectedChecks) {
        const notes = [];
        if (entry.expected.retries) notes.push(`${entry.counts.retriedTurns} turns retried`);
        if (entry.expected.streamPauses) notes.push(`${entry.counts.pausedTurns} turns with stream pauses`);
        lines.push(`- ${entry.name}: ${notes.join(", ")}. These are deliberately scripted and remain visible in the timeline.`);
      }
      lines.push("");
    }
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
  lines.push("", "To dig further: `npm run observatory -- audit findings <audit-id> --json`, `audit operation <run-id> <action> --json`, `audit timeline <run-id> --json`, `audit trace <run-id> --json` (AI turns by cause).", "");
  return lines.join("\n");
}

export function blindSpots(audit) {
  const notes = [
    "Only code wrapped in LiveEvents spans is named; the rest of an action is reported as unattributed time.",
    "Scenarios run a separate app with a fresh profile and seeded data, not your own library.",
    "GPU and compositor time is not measured; frame gaps are what the driver saw, at an assumed 60 Hz.",
    "Expected UI transitions retain their timing in the timeline but do not classify animation/wait time as unexplained CPU execution.",
  ];
  const scenarios = audit.plan?.scenarios ?? [];
  if (scenarios.some((id) => AI_SCENARIOS.includes(id))) {
    notes.push("AI scenarios answer from a local mock provider: they measure Archivist's own side of a turn (preparation, retrieval, context, database, saving, showing the reply). The mock's delays are scripted, so provider timings there are not a provider's.");
    notes.push("AI traces carry timings, counts and sizes only. No prompt, reply, tool argument or SQL is recorded, so a slow stage can be located but its content can't be inspected from the audit.");
  }
  if (scenarios.some((id) => PROVIDER_SCENARIOS.includes(id))) {
    notes.push("Provider latency is a few capped requests with synthetic prompts at one moment, over this machine's network; provider latency varies by hour and region.");
  }
  if ((audit.plan?.repeat ?? 1) < 3) notes.push("Fewer than three rounds: enough to point at something slow, not enough to call a regression.");
  if (audit.plan?.build) notes.push("Build job times come from one build; another build of the same change can differ by seconds.");
  else notes.push("No build in this audit: it measured the binary already in build/frontend-debug.");
  return notes;
}
