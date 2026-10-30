// Summary statistics over UI Lab action records. Raw records are kept in
// ui-events.json; these are conveniences that can always be recomputed.

const TIMINGS = ["dispatchMs", "stateMs", "settledMs"];
// p95 from fewer samples than this is just the maximum, so it's left out.
export const MIN_SAMPLES_FOR_P95 = 20;

function round(value) {
  return value == null ? null : Math.round(value * 100) / 100;
}

// Nearest-rank percentile of an ascending array.
export function percentile(sorted, p) {
  if (!sorted.length) {
    return null;
  }
  const rank = Math.ceil((p / 100) * sorted.length);
  return sorted[Math.min(sorted.length, Math.max(1, rank)) - 1];
}

export function describe(values) {
  const sorted = values.filter((value) => typeof value === "number" && Number.isFinite(value)).sort((a, b) => a - b);
  return {
    n: sorted.length,
    medianMs: round(percentile(sorted, 50)),
    p95Ms: sorted.length >= MIN_SAMPLES_FOR_P95 ? round(percentile(sorted, 95)) : null,
    maxMs: round(sorted.at(-1) ?? null),
  };
}

function group(actions) {
  const stats = {
    actions: actions.length,
    passed: actions.filter((action) => action.ok).length,
    failed: actions.filter((action) => !action.ok).length,
    timedOut: actions.filter((action) => action.timedOut).length,
    settleTimedOut: actions.filter((action) => action.settleTimedOut).length,
  };
  for (const field of TIMINGS) {
    stats[field] = describe(actions.filter((action) => action.ok).map((action) => action[field]));
  }
  stats.frameMaxIntervalMs = describe(actions.filter((action) => action.ok).map((action) => action.frames?.maxIntervalMs));
  return stats;
}

// Overall and per-step statistics for the measured iterations (warm-up,
// setup and teardown actions are counted separately, not mixed in).
export function summarizeActions(actions) {
  const measured = actions.filter((action) => action.phase === "measured");
  const bySteps = new Map();
  for (const action of measured) {
    const key = `${action.step}:${action.label ?? action.op}`;
    if (!bySteps.has(key)) {
      bySteps.set(key, { step: action.step, op: action.op, label: action.label ?? null, method: action.method, actions: [] });
    }
    bySteps.get(key).actions.push(action);
  }
  return {
    phases: Object.fromEntries(["setup", "warmup", "measured", "teardown"].map((phase) => {
      const list = actions.filter((action) => action.phase === phase);
      return [phase, { actions: list.length, failed: list.filter((action) => !action.ok).length }];
    })),
    measured: group(measured),
    steps: [...bySteps.values()].map(({ actions: list, ...info }) => ({ ...info, ...group(list) })),
  };
}
