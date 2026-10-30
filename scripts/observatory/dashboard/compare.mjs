// Compares two normalized runs (baseline, candidate). Lower is better for
// every duration here. A difference within SAME_WITHIN_PCT is shown as
// "about the same"; nothing here claims statistical significance.
export const SAME_WITHIN_PCT = 2;

const PHASE_LABELS = {
  "ide-bundle": "IDE bundle",
  configure: "Configure",
  "build-plan": "Build plan",
  "native-build": "Native build",
};

function metric(key, label, baseline, candidate, { unit = "ms", group = null } = {}) {
  const missing = baseline == null || candidate == null;
  const delta = missing ? null : candidate - baseline;
  const deltaPct = missing || baseline === 0 ? null : (delta / baseline) * 100;
  let verdict = "missing";
  if (!missing) {
    if (delta === 0 || (deltaPct !== null && Math.abs(deltaPct) < SAME_WITHIN_PCT)) {
      verdict = "same";
    } else {
      verdict = delta < 0 ? "better" : "worse";
    }
  }
  return {
    key,
    label,
    group,
    unit,
    baseline: baseline ?? null,
    candidate: candidate ?? null,
    delta: delta === null ? null : Math.round(delta * 100) / 100,
    deltaPct: deltaPct === null ? null : Math.round(deltaPct * 10) / 10,
    verdict,
    missingIn: missing ? [baseline == null ? "baseline" : null, candidate == null ? "candidate" : null].filter(Boolean) : [],
  };
}

function differs(a, b) {
  return a != null && b != null && a !== b;
}

// Conditions that make numbers hard to compare: warnings, not blockers.
function conditionWarnings(a, b) {
  const warnings = [];
  const machine = ["platform", "architecture", "cpuModel", "logicalCpus", "totalMemoryBytes", "osVersion"];
  const changed = machine.filter((field) => differs(a.machine?.[field], b.machine?.[field]));
  if (changed.length) {
    warnings.push({ code: "machine", message: `Different machine or OS (${changed.join(", ")}). Timings from different hardware aren't directly comparable.` });
  }
  if (differs(a.config?.buildType, b.config?.buildType)) {
    warnings.push({ code: "build-type", message: `Build type differs: ${a.config.buildType} vs ${b.config.buildType}.` });
  }
  for (const [field, name] of [["compiler", "Compiler"], ["qtVersion", "Qt"], ["cmakeVersion", "CMake"]]) {
    if (differs(a.config?.[field], b.config?.[field])) {
      warnings.push({ code: field, message: `${name} differs: ${a.config[field]} vs ${b.config[field]}.` });
    }
  }
  for (const [run, role] of [[a, "Baseline"], [b, "Candidate"]]) {
    if (run.source?.dirty) {
      warnings.push({ code: "dirty", message: `${role} was built from a working tree with uncommitted changes.` });
    }
    if (!["success", "passed"].includes(run.status)) {
      warnings.push({ code: "status", message: `${role} did not succeed (${run.status}); its numbers cover only what ran.` });
    }
  }
  return warnings;
}

function buildComparison(a, b, warnings) {
  if (differs(a.build.classification, b.build.classification)) {
    warnings.push({
      code: "classification",
      message: `Different kinds of build: ${a.build.classification} vs ${b.build.classification}. A clean build and a no-change build measure different work.`,
    });
  }
  if (differs(a.build.ideCacheReused, b.build.ideCacheReused)) {
    warnings.push({ code: "ide-cache", message: "The IDE bundle was cached in one run and rebuilt in the other." });
  }
  return [
    metric("totalMs", "Total build", a.build.totalMs, b.build.totalMs),
    ...Object.keys(PHASE_LABELS).map((name) => metric(`phase.${name}`, PHASE_LABELS[name], a.build.phases[name], b.build.phases[name], { group: "Phases" })),
    metric("ninjaWorkSteps", "Ninja steps run", a.build.ninjaWorkSteps, b.build.ninjaWorkSteps, { unit: "steps" }),
  ];
}

function uiComparison(a, b, blockers, warnings) {
  if (a.ui.scenarioId !== b.ui.scenarioId) {
    blockers.push({ code: "scenario", message: `Different scenarios: ${a.ui.scenarioId} vs ${b.ui.scenarioId}.` });
    return [];
  }
  if (a.ui.scenarioVersion !== b.ui.scenarioVersion) {
    blockers.push({ code: "scenario-version", message: `Scenario ${a.ui.scenarioId} changed between runs (v${a.ui.scenarioVersion} vs v${b.ui.scenarioVersion}), so the workload differs.` });
    return [];
  }
  if (differs(a.ui.driverVersion, b.ui.driverVersion)) {
    warnings.push({ code: "driver", message: `The in-app driver changed (v${a.ui.driverVersion} vs v${b.ui.driverVersion}); timing definitions may differ.` });
  }
  const pair = (field, label) => [
    metric(`${field}.median`, `${label} median`, a.ui[field]?.medianMs, b.ui[field]?.medianMs, { group: label }),
    metric(`${field}.p95`, `${label} p95`, a.ui[field]?.p95Ms, b.ui[field]?.p95Ms, { group: label }),
    metric(`${field}.max`, `${label} max`, a.ui[field]?.maxMs, b.ui[field]?.maxMs, { group: label }),
  ];
  const steps = a.ui.steps.map((step) => {
    const other = b.ui.steps.find((candidate) => candidate.step === step.step && candidate.label === step.label);
    return metric(`step.${step.step}`, step.label, step.settledMedianMs, other?.settledMedianMs, { group: "Settled median per step" });
  });
  return [
    ...pair("settled", "Settled"),
    ...pair("state", "UI state reached"),
    ...pair("dispatch", "Dispatch"),
    metric("frameGap.median", "Longest frame gap (median)", a.ui.frameGap?.medianMs, b.ui.frameGap?.medianMs, { group: "Frames" }),
    metric("failedActions", "Failed actions", a.ui.failedActions, b.ui.failedActions, { unit: "actions" }),
    metric("appDurationMs", "Whole run (app start to exit)", a.ui.appDurationMs, b.ui.appDurationMs),
    ...steps,
  ];
}

export function compareRuns(baseline, candidate) {
  const blockers = [];
  const warnings = [];
  let metrics = [];
  for (const [run, role] of [[baseline, "Baseline"], [candidate, "Candidate"]]) {
    if (!run.readable || !run.supported) {
      blockers.push({ code: "unreadable", message: `${role} ${run.runId} can't be read: ${(run.problems ?? []).join("; ")}` });
    }
  }
  if (!blockers.length && baseline.kind !== candidate.kind) {
    blockers.push({ code: "kind", message: `Different kinds of run: ${baseline.kind} vs ${candidate.kind}.` });
  }
  if (!blockers.length) {
    warnings.push(...conditionWarnings(baseline, candidate));
    if (baseline.kind === "build") {
      metrics = buildComparison(baseline, candidate, warnings);
    } else if (baseline.kind === "ui-scenario") {
      metrics = uiComparison(baseline, candidate, blockers, warnings);
    } else {
      blockers.push({ code: "kind", message: `Runs of kind ${baseline.kind} have no comparable measurements.` });
    }
  }
  const counted = metrics.filter((entry) => entry.verdict !== "missing");
  return {
    baseline: baseline.runId,
    candidate: candidate.runId,
    comparable: blockers.length === 0,
    blockers,
    warnings,
    metrics: blockers.length ? [] : metrics,
    totals: {
      better: counted.filter((entry) => entry.verdict === "better").length,
      worse: counted.filter((entry) => entry.verdict === "worse").length,
      same: counted.filter((entry) => entry.verdict === "same").length,
      missing: metrics.length - counted.length,
    },
    note: "One run each: differences can be noise. Repeat both runs before trusting a small change.",
  };
}
