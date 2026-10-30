// Filtering the run history. Every filter is optional; values compare as
// plain text (case-insensitive substring for branch, commit and search).
function includes(value, needle) {
  return String(value ?? "").toLowerCase().includes(String(needle).toLowerCase());
}

export function filterRuns(runs, filters = {}) {
  const from = filters.from ? Date.parse(filters.from) : null;
  // A bare date for "to" includes that whole day.
  const to = filters.to ? Date.parse(filters.to) + (/^\d{4}-\d{2}-\d{2}$/.test(filters.to) ? 86_400_000 : 0) : null;
  return runs.filter((run) => {
    const created = run.createdAt ? Date.parse(run.createdAt) : null;
    if (filters.kind && run.kind !== filters.kind) return false;
    if (filters.status && run.status !== filters.status) return false;
    if (filters.scenario && run.ui?.scenarioId !== filters.scenario) return false;
    if (filters.branch && !includes(run.source?.branch, filters.branch)) return false;
    if (filters.commit && !includes(run.source?.commit, filters.commit)) return false;
    if (from !== null && (created === null || created < from)) return false;
    if (to !== null && (created === null || created >= to)) return false;
    if (filters.q && ![run.runId, run.label, run.source?.branch, run.source?.commit, run.ui?.scenarioName, run.error]
      .some((value) => includes(value, filters.q))) return false;
    return true;
  });
}

// Values present in the history, for filter menus.
export function filterOptions(runs) {
  const unique = (values) => [...new Set(values.filter(Boolean))].sort();
  return {
    kinds: unique(runs.map((run) => run.kind)),
    statuses: unique(runs.map((run) => run.status)),
    branches: unique(runs.map((run) => run.source?.branch)),
    scenarios: unique(runs.map((run) => run.ui?.scenarioId)),
  };
}
