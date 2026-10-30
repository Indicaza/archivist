// The "Copy for AI" diagnostic report: one Observatory run turned into a
// short Markdown brief a coding agent can act on without other context.
// Pure (no I/O), shared by the page and the CLI (`report`, `export`).
//
// It states measured facts, marks what is only observed together, lists
// missing evidence, and never names a cause: a slow step is a measurement,
// not a diagnosis. Raw logs are never included; at most a few redacted
// error lines.
import { normalizeRun } from "./normalize.mjs";
import { logExcerpt } from "./redact.mjs";

export const DIAGNOSTIC_SCHEMA = "archivist.observatory.diagnostic";
export const DIAGNOSTIC_SCHEMA_VERSION = 1;

const OK = new Set(["success", "passed"]);
const num = (value) => (typeof value === "number" && Number.isFinite(value) ? value : null);

function ms(value) {
  if (value == null) return "n/a";
  if (Math.abs(value) >= 1000) return `${(value / 1000).toFixed(2)} s`;
  return `${value.toFixed(Math.abs(value) >= 100 ? 0 : 1)} ms`;
}

function gb(bytes) {
  return bytes ? `${Math.round(bytes / 2 ** 30)} GB` : null;
}

// ---- facts -------------------------------------------------------------------

function identity(run, summary, redact) {
  const m = run.machine ?? {};
  const toolchain = summary.toolchain ?? summary.build?.toolchain ?? {};
  return {
    runId: run.runId,
    kind: run.kind,
    schemaVersion: run.schemaVersion ?? null,
    createdAt: run.createdAt,
    label: run.label ? redact(run.label) : null,
    commit: run.source?.commit ?? null,
    branch: run.source?.branch ? redact(run.source.branch) : null,
    dirty: run.source?.dirty ?? null,
    changedFiles: num(summary.source?.changedFiles),
    binary: summary.build?.binary ? redact(summary.build.binary) : null,
    binarySha256: summary.build?.sha256 ?? null,
    binaryModifiedAt: summary.build?.modifiedAt ?? null,
    buildType: toolchain.buildType ?? null,
    compiler: run.config?.compiler ?? null,
    qtVersion: toolchain.qtVersion ?? null,
    platform: [m.platform, m.osVersion, m.architecture].filter(Boolean).join(" ") || null,
    cpu: m.cpuModel ?? null,
    logicalCpus: m.logicalCpus ?? null,
    memory: gb(m.totalMemoryBytes),
  };
}

function slowestActions(events, count = 5) {
  return (events ?? [])
    .filter((action) => action.phase === "measured" && num(action.settledMs) !== null)
    .sort((a, b) => b.settledMs - a.settledMs)
    .slice(0, count)
    .map((action) => ({
      index: action.index, label: action.label ?? action.op, op: action.op, iteration: action.iteration ?? null,
      settledMs: action.settledMs, stateMs: num(action.stateMs), frameGapMs: num(action.frames?.maxIntervalMs), ok: action.ok !== false,
    }));
}

function uiFacts(run, summary, events, scenario, redact) {
  const ui = run.ui ?? {};
  const measured = summary.stats?.measured ?? {};
  const failedActions = (events ?? []).filter((action) => action.ok === false);
  const failureText = (action) => `action ${action.index} (${action.op}${action.label ? `, ${action.label}` : ""}): ${action.error}`;
  const firstFailure = summary.actions?.firstFailure;
  const errors = [summary.error, ...(events ? failedActions.map(failureText) : firstFailure ? [failureText(firstFailure)] : [])]
    .filter(Boolean).map(redact);
  const steps = [...(ui.steps ?? [])].filter((step) => step.settledMedianMs !== null).sort((a, b) => b.settledMedianMs - a.settledMedianMs).slice(0, 5);
  const methods = scenario?.methods ?? {};
  const slow = slowestActions(events);
  const frameMedian = num(measured.frameMaxIntervalMs?.medianMs);
  // Observed together, not a cause: a slow action during which the app also
  // went a long time without a new frame.
  const correlations = frameMedian === null ? [] : slow
    .filter((action) => action.frameGapMs !== null && action.frameGapMs >= Math.max(50, frameMedian * 2))
    .map((action) => `Action ${action.index} (${action.label}) settled in ${ms(action.settledMs)} and had a ${ms(action.frameGapMs)} gap between frames (median gap across measured actions: ${ms(frameMedian)}).`);
  return {
    experiment: {
      scenario: ui.scenarioName ?? summary.scenario?.name ?? null,
      scenarioId: ui.scenarioId,
      scenarioVersion: ui.scenarioVersion,
      driverVersion: ui.driverVersion,
      description: scenario?.description ? redact(scenario.description) : null,
      initialConditions: scenario?.initialConditions ? redact(scenario.initialConditions) : null,
      warmupIterations: num(summary.scenario?.warmupIterations),
      iterations: num(summary.scenario?.iterations),
      plannedActions: num(summary.actions?.planned),
      isolatedProfile: summary.app?.isolation?.ok ?? null,
      profileRemoved: summary.profile?.removed ?? null,
    },
    results: {
      status: run.status,
      error: summary.error ? redact(summary.error) : null,
      actions: { total: num(summary.actions?.total), passed: num(summary.actions?.passed), failed: num(summary.actions?.failed), warmup: num(summary.actions?.warmup) },
      settled: ui.settled, state: ui.state, frameGap: ui.frameGap, dispatch: ui.dispatch,
      appDurationMs: num(summary.app?.durationMs), readyMs: num(summary.app?.readyMs), appExitCode: summary.app?.exitCode ?? null, appSignal: summary.app?.signal ?? null,
    },
    evidence: {
      slowestSteps: steps.map((step) => ({ ...step, label: step.label, codePath: methods[(scenario?.steps ?? [])[step.step]?.op] ?? null })),
      slowestActions: slow,
      errors: [...new Set(errors)].slice(0, 6),
      timing: Object.entries(summary.runtimeTiming?.summary ?? {}).map(([operation, stats]) => ({ operation, ...stats })),
      correlations,
      // Only the operations behind failed actions and the slowest steps.
      codePaths: relevantOps(summary, failedActions, firstFailure).filter((op) => methods[op]).map((op) => ({ op, how: redact(methods[op]) })),
    },
  };
}

function relevantOps(summary, failedActions, firstFailure) {
  const slowest = [...(summary.stats?.steps ?? [])]
    .filter((step) => typeof step.settledMs?.medianMs === "number")
    .sort((a, b) => b.settledMs.medianMs - a.settledMs.medianMs)
    .slice(0, 2)
    .map((step) => step.op);
  return [...new Set([...failedActions.map((action) => action.op), firstFailure?.op, ...slowest].filter(Boolean))];
}

function buildFacts(run, summary, redact) {
  const b = run.build ?? {};
  const failed = (summary.phases ?? []).find((phase) => phase.status === "failure" || phase.status === "interrupted");
  const ninja = (summary.phases ?? []).find((phase) => phase.name === "native-build")?.details?.ninjaLog;
  const excerpt = Array.isArray(failed?.error?.excerpt) ? failed.error.excerpt : [];
  return {
    experiment: {
      classification: b.classification,
      classificationBasis: summary.buildConditions?.classification?.basis ?? null,
      ideCacheReused: b.ideCacheReused,
      plannedWorkSteps: b.ninjaWorkSteps,
    },
    results: {
      status: run.status,
      totalMs: b.totalMs,
      phases: (summary.phases ?? []).map((phase) => ({ name: phase.name, status: phase.status, durationMs: num(phase.durationMs), exitCode: phase.exitCode ?? null })),
      failedPhase: failed?.name ?? null,
    },
    evidence: {
      errors: [...(summary.errors ?? []).map((error) => `${error.phase}: ${error.message}`), ...logExcerpt(excerpt.join("\n"), redact).lines].map(redact).slice(0, 10),
      // Measured by ninja (.ninja_log); the source file is read off the object name.
      slowestCompileSteps: ninja?.basis === "measured-by-ninja"
        ? (ninja.slowestSteps ?? []).slice(0, 5).map((step) => ({ output: redact(step.output), durationMs: step.durationMs, source: sourceFromObject(step.output) }))
        : [],
    },
  };
}

function sourceFromObject(output) {
  const match = String(output ?? "").match(/([^/]+\.(?:c|cc|cpp|cxx|mm|m))\.o$/);
  return match ? match[1] : null;
}

function comparisonFacts(category) {
  if (!category) return null;
  const pick = (metric) => ({
    metric: metric.label, verdict: metric.verdict, overBudget: metric.overBudget, unit: metric.unit,
    baselineMedian: metric.baseline?.median ?? null, candidateMedian: metric.candidate?.median ?? null,
    baselineN: metric.baseline?.n ?? 0, candidateN: metric.candidate?.n ?? 0,
    delta: metric.delta ?? null, deltaPercent: metric.deltaPercent ?? null, reasons: metric.reasons ?? [],
  });
  const all = [...(category.metrics ?? []), ...(category.steps ?? [])].filter((metric) => metric.verdict !== "not-measured");
  return {
    category: category.category,
    status: category.status,
    verdict: category.verdict,
    reasons: category.reasons ?? [],
    baselineRunIds: category.baseline?.runIds ?? [],
    candidateRunIds: category.candidate?.runIds ?? [],
    notable: all.filter((metric) => metric.verdict !== "no-meaningful-change" || metric.overBudget).map(pick).slice(0, 8),
    unchanged: all.filter((metric) => metric.verdict === "no-meaningful-change" && !metric.overBudget).length,
  };
}

function missingEvidence(run, facts, comparison, hasEvents) {
  const missing = [];
  if (run.kind === "ui-scenario") {
    if (!hasEvents) missing.push("Per-action records (ui-events.json) were not read, so single slow actions are not listed.");
    if (facts.results.settled && facts.results.settled.p95Ms == null) missing.push("p95 latency was not computed: fewer than 20 measured actions.");
    missing.push("No CPU profile, trace or QML profiler data was recorded, so the cause of a slow step is not known.");
    missing.push("Timings are wall-clock measurements from the in-app driver on one machine, under whatever else was running.");
  } else if (run.kind === "build") {
    missing.push("Per-file compile costs come from .ninja_log; there is no compiler time trace.");
  }
  if (!comparison) missing.push("No baseline comparison: either no baseline is set for this kind of run or it wasn't loaded.");
  else if (comparison.status !== "compared") missing.push(`Baseline comparison not made: ${comparison.reasons.join("; ") || comparison.status}.`);
  else if (comparison.candidateRunIds.length < 3) missing.push("Fewer than 3 runs on the candidate side: differences are unconfirmed.");
  if (run.source?.dirty) missing.push(`The working tree was dirty (${facts.identity.changedFiles ?? "some"} changed files): the commit alone does not identify the code that ran.`);
  return missing;
}

function reproduce(run) {
  const commands = [];
  if (run.kind === "ui-scenario" && run.ui?.scenarioId) {
    commands.push(`npm run observatory:ui -- --scenario ${run.ui.scenarioId} --skip-build`);
  } else if (run.kind === "build") {
    commands.push("npm run observatory:build");
  }
  commands.push(`npm run observatory -- show ${run.runId} --json`, `npm run observatory -- report ${run.runId}`, `npm run observatory -- export ${run.runId}`);
  return commands;
}

// The report's data. `category` is this run's category from regression.mjs
// buildReport (optional); `events` the run's ui-events.json actions;
// `scenario` its scenario.json; `logText` a log to take error lines from.
export function diagnosticFacts({ runId, summary, events = null, scenario = null, category = null, logText = null, redact = (text) => text }) {
  const run = normalizeRun(runId, summary);
  const facts = {
    schema: DIAGNOSTIC_SCHEMA,
    schemaVersion: DIAGNOSTIC_SCHEMA_VERSION,
    runId,
    kind: run.kind,
    status: run.status,
    supported: run.supported,
    problems: run.problems ?? [],
    identity: identity(run, summary ?? {}, redact),
  };
  if (run.supported && run.kind === "ui-scenario") Object.assign(facts, uiFacts(run, summary, events, scenario, redact));
  else if (run.supported && run.kind === "build") Object.assign(facts, buildFacts(run, summary, redact));
  else facts.results = { status: run.status, error: run.error ? redact(run.error) : null };
  if (logText) {
    const excerpt = logExcerpt(logText, redact);
    facts.evidence = { ...(facts.evidence ?? {}), logExcerpt: excerpt.lines, logLines: excerpt.totalLines };
  }
  facts.comparison = comparisonFacts(category);
  facts.missingEvidence = missingEvidence(run, facts, facts.comparison, Array.isArray(events));
  facts.reproduce = reproduce(run);
  facts.artifacts = Object.entries(summary?.artifacts ?? {}).filter(([, file]) => typeof file === "string").map(([name, file]) => ({ name, file }));
  return facts;
}

// ---- markdown ----------------------------------------------------------------

function statLine(name, stat) {
  if (!stat || stat.medianMs == null) return null;
  return `- ${name}: median ${ms(stat.medianMs)}, p95 ${stat.p95Ms == null ? "not computed" : ms(stat.p95Ms)}, max ${ms(stat.maxMs)} (n=${stat.n ?? "?"})`;
}

function verdictWord(entry) {
  const words = { regression: "REGRESSION", improvement: "improvement", "no-meaningful-change": "no meaningful change", "insufficient-evidence": "needs more runs", incompatible: "incompatible" };
  return `${words[entry.verdict] ?? entry.verdict}${entry.overBudget && entry.verdict !== "regression" ? " (over budget, unconfirmed)" : ""}`;
}

function comparisonLines(comparison) {
  if (!comparison) return ["- Not compared (no baseline loaded for this run's category)."];
  const lines = [`- Category \`${comparison.category}\`: ${comparison.status === "compared" ? verdictWord(comparison) : comparison.status}`];
  if (comparison.baselineRunIds.length) lines.push(`- Baseline runs: ${comparison.baselineRunIds.join(", ")}`);
  if (comparison.candidateRunIds.length) lines.push(`- Compared runs: ${comparison.candidateRunIds.join(", ")}`);
  for (const reason of comparison.reasons) lines.push(`- Note: ${reason}`);
  for (const entry of comparison.notable) {
    const change = entry.delta == null ? "" : ` ${entry.delta > 0 ? "+" : ""}${entry.unit === "ms" ? ms(entry.delta) : entry.delta}${entry.deltaPercent == null ? "" : ` (${entry.delta > 0 ? "+" : ""}${entry.deltaPercent}%)`}`;
    const values = entry.baselineMedian == null ? "" : ` ${ms(entry.baselineMedian)} → ${ms(entry.candidateMedian)}`;
    const measured = [values.trim(), change.trim()].filter(Boolean).join(" ");
    lines.push(`- ${entry.metric}: ${measured ? `${measured}, ` : ""}n=${entry.baselineN}/${entry.candidateN}: ${verdictWord(entry)}`);
  }
  if (comparison.unchanged) lines.push(`- ${comparison.unchanged} other metrics: no meaningful change.`);
  lines.push("- Rule: confirmed only with enough runs on both sides, past the budget, and no overlap between runs. Not a significance test.");
  return lines;
}

export function diagnosticMarkdown(facts) {
  const id = facts.identity;
  const title = facts.kind === "ui-scenario" ? facts.experiment?.scenario ?? "UI scenario" : facts.kind === "build" ? "Build" : facts.kind;
  const out = [`# Archivist Observatory: ${title} (${facts.status})`, "",
    `Observatory record \`${facts.runId}\`. Everything below is measured unless a section says otherwise. Paths and secrets are redacted.`, "",
    "## Identity",
    `- Source: ${id.branch ?? "detached"} @ ${id.commit ?? "unknown commit"}${id.dirty ? ` (dirty tree${id.changedFiles != null ? `, ${id.changedFiles} changed files` : ""})` : id.dirty === false ? " (clean tree)" : ""}`,
  ];
  if (id.binary) out.push(`- Binary: ${id.binary}${id.binarySha256 ? `, sha256 ${id.binarySha256.slice(0, 16)}` : ""}${id.binaryModifiedAt ? `, built ${id.binaryModifiedAt}` : ""}`);
  const build = [id.buildType, id.compiler, id.qtVersion ? `Qt ${id.qtVersion}` : null].filter(Boolean).join(", ");
  if (build) out.push(`- Build: ${build}`);
  out.push(`- Machine: ${[id.platform, id.cpu, id.logicalCpus ? `${id.logicalCpus} cores` : null, id.memory].filter(Boolean).join(", ") || "unknown"}`);
  out.push(`- Recorded: ${id.createdAt ?? "unknown"}${id.label ? `, label "${id.label}"` : ""}`);
  for (const problem of facts.problems) out.push(`- Record problem: ${problem}`);

  if (facts.kind === "ui-scenario" && facts.experiment) {
    const e = facts.experiment;
    const r = facts.results;
    out.push("", "## Experiment", `- Scenario: ${e.scenario} (\`${e.scenarioId}\` v${e.scenarioVersion ?? "?"}, driver v${e.driverVersion ?? "?"})`);
    if (e.description) out.push(`- What it does: ${e.description}`);
    if (e.initialConditions) out.push(`- Initial conditions: ${e.initialConditions}`);
    out.push(`- Plan: ${e.warmupIterations ?? "?"} warm-up + ${e.iterations ?? "?"} measured iterations, ${e.plannedActions ?? "?"} actions, in a disposable profile (isolation check ${e.isolatedProfile === true ? "passed" : e.isolatedProfile === false ? "FAILED" : "not reported"})`);
    out.push("", "## Result");
    out.push(`- Status: ${r.status}${r.error ? `: ${r.error}` : ""}`);
    out.push(`- Actions: ${r.actions.passed ?? "?"} of ${r.actions.total ?? "?"} passed, ${r.actions.failed ?? 0} failed (${r.actions.warmup ?? 0} warm-up)`);
    for (const line of [statLine("Settled latency per measured action", r.settled), statLine("UI state reached", r.state), statLine("Longest gap between frames per action", r.frameGap), statLine("Input dispatch", r.dispatch)]) if (line) out.push(line);
    out.push(`- App: ran ${ms(r.appDurationMs)}, ready after ${ms(r.readyMs)}, exit code ${r.appExitCode ?? "none"}${r.appSignal ? `, signal ${r.appSignal}` : ""}`);
  } else if (facts.kind === "build" && facts.results?.phases) {
    const e = facts.experiment;
    const r = facts.results;
    out.push("", "## Experiment", `- Build kind: ${e.classification ?? "unclassified"}${e.classificationBasis ? ` (${e.classificationBasis})` : ""}, IDE bundle cache ${e.ideCacheReused === true ? "reused" : e.ideCacheReused === false ? "rebuilt" : "unknown"}, ${e.plannedWorkSteps ?? "?"} planned compile/link steps`);
    out.push("", "## Result", `- Status: ${r.status}, total ${ms(r.totalMs)}${r.failedPhase ? `, failed in ${r.failedPhase}` : ""}`);
    out.push(`- Phases: ${r.phases.map((phase) => `${phase.name} ${ms(phase.durationMs)}${phase.status !== "success" ? ` (${phase.status})` : ""}`).join(", ")}`);
  } else {
    out.push("", "## Result", `- Status: ${facts.results?.status}${facts.results?.error ? `: ${facts.results.error}` : ""}`);
  }

  out.push("", "## Baseline comparison", ...comparisonLines(facts.comparison));

  const ev = facts.evidence ?? {};
  out.push("", "## Evidence");
  if (ev.slowestSteps?.length) {
    out.push("Slowest steps (median settled latency over measured iterations):");
    for (const step of ev.slowestSteps) out.push(`- Step ${step.step} "${step.label}": median ${ms(step.settledMedianMs)}, max ${ms(step.settledMaxMs)}${step.failed ? `, ${step.failed} failed` : ""}`);
  }
  if (ev.slowestActions?.length) {
    out.push("Slowest single actions:");
    for (const action of ev.slowestActions) out.push(`- #${action.index} ${action.label} (iteration ${action.iteration ?? "?"}): settled ${ms(action.settledMs)}, longest frame gap ${ms(action.frameGapMs)}${action.ok ? "" : ", FAILED"}`);
  }
  if (ev.slowestCompileSteps?.length) {
    out.push("Slowest compile steps (measured by ninja; source read off the object name):");
    for (const step of ev.slowestCompileSteps) out.push(`- ${step.source ?? step.output}: ${ms(step.durationMs)}`);
  }
  if (ev.errors?.length) {
    out.push("Errors:");
    for (const error of ev.errors) out.push(`- ${error}`);
  }
  if (ev.logExcerpt?.length) {
    out.push(`Log lines mentioning errors or warnings (redacted, ${ev.logExcerpt.length} of ${ev.logLines} lines):`, "```", ...ev.logExcerpt, "```");
  }
  if (ev.timing?.length) {
    out.push("Runtime timing events (archivist.wells.timing, same process):");
    for (const entry of ev.timing.slice(0, 6)) out.push(`- ${entry.operation}: ${entry.count}× mean ${entry.meanMs} ms, max ${entry.maxMs} ms (${entry.field})`);
  }
  if (!ev.slowestSteps?.length && !ev.slowestActions?.length && !ev.slowestCompileSteps?.length && !ev.errors?.length && !ev.timing?.length) out.push("- Nothing beyond the result above was recorded.");

  if (ev.correlations?.length) {
    out.push("", "## Observed together (correlation, not a cause)", ...ev.correlations.map((line) => `- ${line}`));
  }
  if (ev.codePaths?.length) {
    out.push("", "## Hypotheses (not verified)", "- Where to look first: how the scenario drives the failed or slowest steps, from its definition (not from a trace):");
    for (const path of ev.codePaths) out.push(`  - ${path.op}: ${path.how}`);
  } else {
    out.push("", "## Hypotheses", "- None. This report lists measurements; it does not identify causes.");
  }
  out.push("", "## Missing evidence", ...facts.missingEvidence.map((line) => `- ${line}`));
  out.push("", "## Reproduce", "```", ...facts.reproduce, "```");
  if (facts.artifacts.length) out.push("", "## Artifacts (in the run's folder)", `- ${facts.artifacts.map((entry) => `${entry.name}: ${entry.file}`).join(", ")}, summary.json`);
  return `${out.join("\n")}\n`;
}

export function diagnosticReport(input) {
  const facts = diagnosticFacts(input);
  return { facts, markdown: diagnosticMarkdown(facts) };
}

// A regression comparison on its own (the Baselines page's Copy for AI).
export function comparisonMarkdown(category, { createdAt = null, redact = (text) => text } = {}) {
  const facts = comparisonFacts(category);
  const out = [`# Archivist Observatory: ${category.label ?? category.category} vs baseline`, "",
    `Comparison of recorded runs against the \`${category.category}\` baseline${createdAt ? `, worked out ${createdAt}` : ""}. Measured values only.`, "",
    "## Comparison", ...comparisonLines(facts)];
  if (category.baseline) {
    out.push("", "## Identity", `- Baseline: ${category.baseline.branch ?? "detached"} @ ${category.baseline.commit ?? "?"}${category.baseline.note ? `, note "${redact(category.baseline.note)}"` : ""}`);
    if (category.candidate?.commits?.length) out.push(`- Compared runs: ${(category.candidate.branches ?? []).join(", ") || "?"} @ ${category.candidate.commits.join(", ")}`);
  }
  const failures = category.functional?.failedRuns ?? [];
  if (failures.length) out.push("", "## Functional failures (not counted as performance)", ...failures.map((failure) => `- ${failure.runId}: ${failure.status}${failure.error ? `: ${redact(failure.error)}` : ""}`));
  out.push("", "## Missing evidence", "- No profile or trace: a regression here says what got slower, not why.",
    "", "## Reproduce", "```", `npm run observatory -- baseline compare ${(facts.candidateRunIds ?? []).join(" ") || "<run-id>"} --json`, ...(facts.candidateRunIds ?? []).slice(0, 1).map((id) => `npm run observatory -- report ${id}`), "```");
  return `${out.join("\n")}\n`;
}
