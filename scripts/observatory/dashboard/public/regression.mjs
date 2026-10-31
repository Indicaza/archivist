// Baseline vs candidate regression analysis for Observatory runs.
//
// Pure (no Node or browser APIs), so the CLI, its reports and the dashboard
// page all reach the same verdicts. Input is normalized run records
// (normalize.mjs), a baseline entry (baselines.json) and the budgets
// (scripts/observatory/budgets.json).
//
// The rules, in order, for each metric that has a budget:
//   1. Only runs that succeeded count. Failed runs are functional failures,
//      reported on their own, never as slow or fast.
//   2. Each side is summarized by its median, min and max over runs.
//   3. Fewer runs than the budget's minSamples on either side, or a metric
//      not measured: "insufficient-evidence". The numbers are still shown.
//   4. The change must exceed the budget's percentage AND its minimum
//      absolute change to count at all; otherwise "no-meaningful-change".
//   5. A change past the budget counts only when the two groups don't
//      overlap (every candidate run is worse, or better, than every baseline
//      run). Otherwise "insufficient-evidence": the spread between runs is as
//      large as the change.
// This is a transparent rule of thumb, not a statistical test. Nothing here
// is described as statistically significant.

export const REPORT_SCHEMA = "archivist.observatory.regression-report";
export const REPORT_SCHEMA_VERSION = 1;
export const BUDGETS_SCHEMA_VERSION = 1;
// What each metric key means. Bump when a definition changes, so baselines
// recorded under the old meaning are recognized.
export const METRIC_DEFINITIONS_VERSION = 1;

export const VERDICTS = ["regression", "improvement", "no-meaningful-change", "insufficient-evidence", "incompatible"];

const OK = new Set(["success", "passed"]);

export const CATEGORIES = {
  "build:clean": { label: "Clean build", kind: "build", classification: "clean" },
  "build:incremental": { label: "Incremental build", kind: "build", classification: "incremental" },
  "build:no-change": { label: "No-change build", kind: "build", classification: "no-change" },
  "ui:sidebar": { label: "Sidebar Stress", kind: "ui-scenario", scenario: "sidebar" },
  "ui:workspace": { label: "Workspace Stress", kind: "ui-scenario", scenario: "workspace" },
  "ui:chat": { label: "Chat", kind: "ui-scenario", scenario: "chat" },
  "ui:startup": { label: "Startup", kind: "ui-scenario", scenario: "startup" },
};

// Metric keys a budget can name. Each reads one number from a normalized
// run, or null when that run didn't measure it.
export const METRICS = {
  totalMs: { label: "Total build time", unit: "ms", read: (run) => run.build?.totalMs },
  "phase.ide-bundle": { label: "IDE bundle", unit: "ms", read: (run) => run.build?.phases?.["ide-bundle"] },
  "phase.configure": { label: "CMake configure", unit: "ms", read: (run) => run.build?.phases?.configure },
  "phase.build-plan": { label: "Build plan", unit: "ms", read: (run) => run.build?.phases?.["build-plan"] },
  "phase.native-build": { label: "Native build", unit: "ms", read: (run) => run.build?.phases?.["native-build"] },
  "settled.median": { label: "Action latency, median (settled)", unit: "ms", read: (run) => run.ui?.settled?.medianMs },
  "settled.p95": { label: "Action latency, p95 (settled)", unit: "ms", read: (run) => run.ui?.settled?.p95Ms },
  "state.median": { label: "UI state reached, median", unit: "ms", read: (run) => run.ui?.state?.medianMs },
  "frameGap.median": { label: "Longest frame gap, median", unit: "ms", read: (run) => run.ui?.frameGap?.medianMs },
  appDurationMs: { label: "Scenario duration (app start to exit)", unit: "ms", read: (run) => run.ui?.appDurationMs },
  "startup.firstFrame": { label: "First frame after main()", unit: "ms", read: (run) => run.ui?.startup?.firstFrameMs },
  "startup.eventLoop": { label: "Event loop entered after main()", unit: "ms", read: (run) => run.ui?.startup?.eventLoopMs },
};

function finite(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function round(value, places = 2) {
  if (value == null) return null;
  const factor = 10 ** places;
  return Math.round(value * factor) / factor;
}

// ---- categories --------------------------------------------------------------

// The workload a run measures: builds by what kind of build it was (a clean
// build is never compared with an incremental one), UI runs by scenario.
export function categoryOf(run) {
  if (!run?.readable || !run.supported) return null;
  if (run.kind === "build") {
    const value = run.build?.classification;
    return value && CATEGORIES[`build:${value}`] ? `build:${value}` : null;
  }
  if (run.kind === "ui-scenario") {
    const id = run.ui?.scenarioId;
    return id ? `ui:${id}` : null;
  }
  return null;
}

export function categoryLabel(category) {
  return CATEGORIES[category]?.label ?? category;
}

// ---- budgets -----------------------------------------------------------------

const COMPATIBILITY_FIELDS = {
  platform: (run) => run.machine?.platform,
  architecture: (run) => run.machine?.architecture,
  cpuModel: (run) => run.machine?.cpuModel,
  logicalCpus: (run) => run.machine?.logicalCpus,
  totalMemoryBytes: (run) => run.machine?.totalMemoryBytes,
  osVersion: (run) => run.machine?.osVersion,
  buildType: (run) => run.config?.buildType,
  compiler: (run) => run.config?.compiler,
  qtVersion: (run) => run.config?.qtVersion,
  scenarioVersion: (run) => run.ui?.scenarioVersion,
  driverVersion: (run) => run.ui?.driverVersion,
};

// Checks a budgets file and fills in defaults. Problems are listed, and a
// category or metric with a problem is left out rather than guessed at.
export function resolveBudgets(raw) {
  const problems = [];
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) {
    return { schemaVersion: null, categories: {}, problems: ["the budgets file is not a JSON object"] };
  }
  if (raw.schemaVersion !== BUDGETS_SCHEMA_VERSION) {
    return { schemaVersion: raw.schemaVersion ?? null, categories: {}, problems: [`budgets schemaVersion ${raw.schemaVersion ?? "missing"} is not ${BUDGETS_SCHEMA_VERSION}`] };
  }
  const defaults = raw.defaults ?? {};
  const categories = {};
  for (const [category, entry] of Object.entries(raw.categories ?? {})) {
    if (!CATEGORIES[category]) {
      problems.push(`unknown category "${category}" (known: ${Object.keys(CATEGORIES).join(", ")})`);
      continue;
    }
    const minSamples = positiveInteger(entry.minSamples ?? defaults.minSamples ?? 3);
    const compatibility = entry.compatibility ?? defaults.compatibility ?? ["platform", "architecture", "cpuModel", "logicalCpus", "buildType"];
    const unknownFields = compatibility.filter((field) => !COMPATIBILITY_FIELDS[field]);
    if (unknownFields.length) problems.push(`${category}: unknown compatibility fields ${unknownFields.join(", ")}`);
    const resolved = {
      minSamples,
      missingData: (entry.missingData ?? defaults.missingData ?? "insufficient-evidence") === "ignore" ? "ignore" : "insufficient-evidence",
      compatibility: compatibility.filter((field) => COMPATIBILITY_FIELDS[field]),
      metrics: {},
      steps: null,
    };
    if (CATEGORIES[category].kind === "ui-scenario" && !resolved.compatibility.includes("scenarioVersion")) {
      // A changed scenario is a different workload, whatever the file says.
      resolved.compatibility.push("scenarioVersion");
    }
    for (const [key, metric] of Object.entries(entry.metrics ?? {})) {
      const checked = resolveMetricBudget(`${category} ${key}`, metric, resolved.minSamples, problems);
      if (!METRICS[key]) {
        problems.push(`${category}: unknown metric "${key}" (known: ${Object.keys(METRICS).join(", ")})`);
      } else if (checked) {
        resolved.metrics[key] = checked;
      }
    }
    if (entry.steps && CATEGORIES[category].kind === "ui-scenario") {
      resolved.steps = resolveMetricBudget(`${category} steps`, entry.steps, resolved.minSamples, problems);
    }
    categories[category] = resolved;
  }
  return { schemaVersion: raw.schemaVersion, categories, problems };
}

function positiveInteger(value) {
  const number = Math.floor(Number(value));
  return Number.isFinite(number) && number >= 1 ? number : 1;
}

function resolveMetricBudget(where, metric, minSamples, problems) {
  if (!metric || typeof metric !== "object") {
    problems.push(`${where}: not an object`);
    return null;
  }
  if (!["lower", "higher"].includes(metric.direction)) {
    problems.push(`${where}: direction must be "lower" or "higher" (which is better)`);
    return null;
  }
  const percent = Number(metric.regressionPercent);
  if (!Number.isFinite(percent) || percent <= 0) {
    problems.push(`${where}: regressionPercent must be a positive number`);
    return null;
  }
  const improvement = metric.improvementPercent == null ? percent : Number(metric.improvementPercent);
  return {
    direction: metric.direction,
    regressionPercent: percent,
    improvementPercent: Number.isFinite(improvement) && improvement > 0 ? improvement : percent,
    minAbsolute: Math.max(0, Number(metric.minAbsoluteMs ?? metric.minAbsolute ?? 0) || 0),
    minSamples: positiveInteger(metric.minSamples ?? minSamples),
  };
}

// ---- comparing groups ----------------------------------------------------------

function describe(values) {
  if (!values.length) return { n: 0, median: null, min: null, max: null, spreadPercent: null, values: [] };
  const mid = median(values);
  const min = Math.min(...values);
  const max = Math.max(...values);
  return {
    n: values.length,
    median: round(mid),
    min: round(min),
    max: round(max),
    // Range relative to the median: how much these runs disagree.
    spreadPercent: mid ? round(((max - min) / Math.abs(mid)) * 100, 1) : null,
    values: values.map((value) => round(value)),
  };
}

// One metric, baseline group vs candidate group.
export function compareMetric({ key, label, unit = "ms", budget, baselineValues, candidateValues, missingData = "insufficient-evidence" }) {
  const base = describe(baselineValues.map(finite).filter((value) => value !== null));
  const cand = describe(candidateValues.map(finite).filter((value) => value !== null));
  const result = {
    key, label, unit,
    direction: budget.direction,
    budget: { regressionPercent: budget.regressionPercent, improvementPercent: budget.improvementPercent, minAbsolute: budget.minAbsolute, minSamples: budget.minSamples },
    baseline: base,
    candidate: cand,
    delta: null,
    deltaPercent: null,
    overBudget: false,
    verdict: "insufficient-evidence",
    reasons: [],
  };
  if (!base.n || !cand.n) {
    const where = [!base.n && "baseline", !cand.n && "candidate"].filter(Boolean).join(" and ");
    result.reasons.push(`not measured in the ${where}`);
    result.verdict = missingData === "ignore" ? "not-measured" : "insufficient-evidence";
    return result;
  }
  const delta = cand.median - base.median;
  result.delta = round(delta);
  result.deltaPercent = base.median ? round((delta / Math.abs(base.median)) * 100, 1) : null;
  const worse = budget.direction === "lower" ? delta > 0 : delta < 0;
  const better = budget.direction === "lower" ? delta < 0 : delta > 0;
  const percent = result.deltaPercent == null ? Infinity : Math.abs(result.deltaPercent);
  const bigEnough = Math.abs(delta) >= budget.minAbsolute;
  const pastRegression = worse && bigEnough && percent > budget.regressionPercent;
  const pastImprovement = better && bigEnough && percent > budget.improvementPercent;
  result.overBudget = pastRegression;
  const enough = base.n >= budget.minSamples && cand.n >= budget.minSamples;
  // Every candidate run on the far side of every baseline run.
  const separated = budget.direction === "lower"
    ? (worse ? cand.min > base.max : cand.max < base.min)
    : (worse ? cand.max < base.min : cand.min > base.max);

  if (!enough) {
    result.reasons.push(`${base.n} baseline and ${cand.n} candidate run${cand.n === 1 ? "" : "s"}; this budget needs at least ${budget.minSamples} on each side`);
    if (pastRegression) result.reasons.push(`the change is past the ${budget.regressionPercent}% budget, but unconfirmed`);
    result.verdict = "insufficient-evidence";
    return result;
  }
  if (!pastRegression && !pastImprovement) {
    result.verdict = "no-meaningful-change";
    result.reasons.push(`within ${worse ? budget.regressionPercent : budget.improvementPercent}%${budget.minAbsolute ? ` or under ${budget.minAbsolute} ${unit}` : ""}`);
    return result;
  }
  if (!separated) {
    result.verdict = "insufficient-evidence";
    result.reasons.push(`past the ${pastRegression ? budget.regressionPercent : budget.improvementPercent}% budget, but the runs overlap: the spread between runs is as large as the change`);
    return result;
  }
  result.verdict = pastRegression ? "regression" : "improvement";
  result.reasons.push(`every candidate run is ${pastRegression ? "worse" : "better"} than every baseline run, by more than the ${pastRegression ? budget.regressionPercent : budget.improvementPercent}% budget`);
  return result;
}

function compatibilityProblems(reference, run, fields) {
  return fields
    .map((field) => [field, COMPATIBILITY_FIELDS[field](reference), COMPATIBILITY_FIELDS[field](run)])
    .filter(([, a, b]) => a != null && b != null && a !== b)
    .map(([field, a, b]) => `${field}: ${a} vs ${b}`);
}

// Runs to compare with a baseline when none are named: the newest runs of
// the same category recorded after the baseline, up to `count` usable ones.
// Incompatible and failed runs met on the way are returned too, so the
// report can say why they were left out.
export function selectCandidates(runs, baselineEntry, budget, count) {
  const baselineIds = new Set(baselineEntry.runIds);
  const newest = baselineEntry.runIds.slice().sort().at(-1);
  const reference = runs.find((run) => baselineIds.has(run.runId));
  const usable = [];
  const failed = [];
  const incompatible = [];
  const newer = runs
    .filter((run) => !baselineIds.has(run.runId) && run.runId > newest && categoryOf(run) === baselineEntry.category)
    .sort((a, b) => b.runId.localeCompare(a.runId));
  for (const run of newer) {
    if (usable.length >= count) break;
    if (!OK.has(run.status)) {
      failed.push(run);
      continue;
    }
    const problems = reference ? compatibilityProblems(reference, run, budget.compatibility) : [];
    if (problems.length) incompatible.push({ run, problems });
    else usable.push(run);
  }
  return { usable, failed, incompatible };
}

// One category: the baseline group against the candidate group.
export function analyzeCategory({ category, baselineEntry, baselineRuns, candidateRuns, budget, skipped = [] }) {
  const result = {
    category,
    label: categoryLabel(category),
    status: "compared",
    verdict: "no-meaningful-change",
    baseline: baselineEntry ? {
      runIds: baselineEntry.runIds,
      setAt: baselineEntry.setAt ?? null,
      note: baselineEntry.note ?? null,
      commit: baselineEntry.reference?.commit ?? null,
      branch: baselineEntry.reference?.branch ?? null,
    } : null,
    candidate: {
      runIds: candidateRuns.map((run) => run.runId),
      commits: [...new Set(candidateRuns.map((run) => run.source?.commit).filter(Boolean))],
      branches: [...new Set(candidateRuns.map((run) => run.source?.branch).filter(Boolean))],
    },
    reasons: [],
    functional: { failedRuns: [], failedActions: 0 },
    skipped: skipped.map(({ run, problems }) => ({ runId: run.runId, reason: `incompatible: ${problems.join("; ")}` })),
    metrics: [],
    steps: [],
  };
  if (!budget) {
    result.status = "no-budget";
    result.verdict = "insufficient-evidence";
    result.reasons.push(`no budget for ${category} in the budgets file`);
    return result;
  }
  if (!baselineEntry) {
    result.status = "no-baseline";
    result.verdict = "insufficient-evidence";
    result.reasons.push(`no baseline set: npm run observatory -- baseline set <run-id>`);
    return result;
  }

  // Functional failures are their own finding, never a performance number.
  for (const run of candidateRuns) {
    if (!OK.has(run.status)) {
      result.functional.failedRuns.push({ runId: run.runId, status: run.status, error: run.error ?? null });
    }
    result.functional.failedActions += run.ui?.failedActions ?? 0;
  }
  const baseOk = baselineRuns.filter((run) => OK.has(run.status));
  const candOk = candidateRuns.filter((run) => OK.has(run.status));
  const missingBaseline = baselineEntry.runIds.filter((id) => !baselineRuns.some((run) => run.runId === id && run.readable && run.supported));
  if (missingBaseline.length) {
    result.reasons.push(`baseline run${missingBaseline.length === 1 ? "" : "s"} ${missingBaseline.join(", ")} can't be read any more`);
  }

  // Every run in both groups must be the same workload on the same footing.
  const blockers = [];
  const reference = baseOk[0];
  for (const run of [...baselineRuns, ...candidateRuns]) {
    if (!run.readable || !run.supported) continue;
    if (categoryOf(run) !== category) blockers.push(`${run.runId} is a ${categoryLabel(categoryOf(run) ?? "unknown")} run`);
    else if (reference) {
      const problems = compatibilityProblems(reference, run, budget.compatibility);
      if (problems.length) blockers.push(`${run.runId}: ${problems.join("; ")}`);
    }
  }
  if (baselineEntry.metricDefinitionsVersion != null && baselineEntry.metricDefinitionsVersion !== METRIC_DEFINITIONS_VERSION) {
    blockers.push(`the baseline was recorded with metric definitions v${baselineEntry.metricDefinitionsVersion}; this is v${METRIC_DEFINITIONS_VERSION}`);
  }
  if (blockers.length) {
    result.status = "incompatible";
    result.verdict = "incompatible";
    result.reasons.push(...blockers);
    return result;
  }
  if (!candOk.length) {
    result.status = candidateRuns.length ? "no-usable-candidates" : "no-candidates";
    result.verdict = "insufficient-evidence";
    result.reasons.push(candidateRuns.length ? "every candidate run failed; see the functional failures" : "no compatible runs recorded since the baseline");
    return result;
  }

  for (const [key, metricBudget] of Object.entries(budget.metrics)) {
    const definition = METRICS[key];
    result.metrics.push(compareMetric({
      key, label: definition.label, unit: definition.unit, budget: metricBudget, missingData: budget.missingData,
      baselineValues: baseOk.map(definition.read), candidateValues: candOk.map(definition.read),
    }));
  }
  if (budget.steps) {
    // Per-step settled latency: where in the scenario the time went.
    const steps = new Map();
    for (const run of baseOk) for (const step of run.ui?.steps ?? []) steps.set(`${step.step}:${step.label}`, step);
    for (const [stepKey, step] of steps) {
      const read = (run) => run.ui?.steps?.find((entry) => `${entry.step}:${entry.label}` === stepKey)?.settledMedianMs;
      result.steps.push(compareMetric({
        key: `step:${stepKey}`, label: `Step ${step.step}: ${step.label}`, unit: "ms", budget: budget.steps, missingData: budget.missingData,
        baselineValues: baseOk.map(read), candidateValues: candOk.map(read),
      }));
    }
    result.steps.sort((a, b) => (b.candidate.median ?? -1) - (a.candidate.median ?? -1));
  }
  result.verdict = overallVerdict([...result.metrics, ...result.steps].map((metric) => metric.verdict));
  return result;
}

function overallVerdict(verdicts) {
  if (verdicts.includes("regression")) return "regression";
  if (verdicts.includes("insufficient-evidence")) return "insufficient-evidence";
  if (verdicts.includes("improvement")) return "improvement";
  return "no-meaningful-change";
}

// ---- the whole report -------------------------------------------------------

// `baselines` is the parsed baselines.json; `candidates` optionally names the
// candidate runs per category (otherwise the newest compatible runs are used).
export function buildReport({ runs, baselines, budgets, candidates = null, createdAt = new Date().toISOString(), source = {} }) {
  const resolved = budgets?.categories ? budgets : resolveBudgets(budgets);
  const byId = new Map(runs.map((run) => [run.runId, run]));
  const entries = baselines?.baselines ?? {};
  const wanted = candidates ? Object.keys(candidates) : [...new Set([...Object.keys(entries), ...Object.keys(resolved.categories)])];
  const categories = [];
  for (const category of wanted.filter((name) => CATEGORIES[name])) {
    const budget = resolved.categories[category] ?? null;
    const entry = entries[category] ?? null;
    const baselineRuns = (entry?.runIds ?? []).map((id) => byId.get(id) ?? { runId: id, readable: false, supported: false, status: "missing" });
    let candidateRuns = [];
    let skipped = [];
    if (candidates) {
      candidateRuns = candidates[category].map((id) => byId.get(id) ?? { runId: id, readable: false, supported: false, status: "missing" });
    } else if (entry && budget) {
      const count = Math.max(budget.minSamples, ...Object.values(budget.metrics).map((metric) => metric.minSamples));
      const selection = selectCandidates(runs, entry, budget, count);
      candidateRuns = [...selection.usable, ...selection.failed].sort((a, b) => b.runId.localeCompare(a.runId));
      skipped = selection.incompatible;
    }
    if (!candidates && !entry && !candidateRuns.length) {
      categories.push({ category, label: categoryLabel(category), status: "no-baseline", verdict: "insufficient-evidence", baseline: null, candidate: { runIds: [], commits: [], branches: [] }, reasons: ["no baseline set"], functional: { failedRuns: [], failedActions: 0 }, skipped: [], metrics: [], steps: [] });
      continue;
    }
    categories.push(analyzeCategory({ category, baselineEntry: entry, baselineRuns, candidateRuns, budget, skipped }));
  }
  return {
    schema: REPORT_SCHEMA,
    schemaVersion: REPORT_SCHEMA_VERSION,
    createdAt,
    metricDefinitionsVersion: METRIC_DEFINITIONS_VERSION,
    budgets: { schemaVersion: resolved.schemaVersion, source: source.budgets ?? null, problems: resolved.problems ?? [] },
    baselinesFile: source.baselines ?? null,
    method: "Per metric: median of each group. A regression or improvement needs at least minSamples runs on each side, a change past the budget's percentage and minimum absolute change, and no overlap between the two groups' runs. Otherwise the result is no meaningful change or insufficient evidence. This is a rule of thumb, not a statistical significance test.",
    summary: summarizeCategories(categories),
    categories,
  };
}

// Totals over a report's categories (recomputed when a report is narrowed).
export function summarizeCategories(categories) {
  const all = categories.flatMap((category) => [...category.metrics, ...category.steps]);
  return {
    regressions: all.filter((metric) => metric.verdict === "regression").length,
    improvements: all.filter((metric) => metric.verdict === "improvement").length,
    noMeaningfulChange: all.filter((metric) => metric.verdict === "no-meaningful-change").length,
    insufficientEvidence: all.filter((metric) => metric.verdict === "insufficient-evidence").length,
    unconfirmedOverBudget: all.filter((metric) => metric.overBudget && metric.verdict !== "regression").length,
    incompatibleCategories: categories.filter((category) => category.verdict === "incompatible").length,
    functionalFailures: categories.reduce((total, category) => total + category.functional.failedRuns.length, 0),
    categoriesWithoutBaseline: categories.filter((category) => category.status === "no-baseline").length,
  };
}
