// Plain text for the control operations when --json isn't given. Agents
// should use --json; this is only for people at a terminal.

function ms(value) {
  if (value == null) return "—";
  return value >= 1000 ? `${(value / 1000).toFixed(2)} s` : `${value.toFixed(1)} ms`;
}

function jobLines(job) {
  if (!job) return ["No test jobs yet. Start one: npm run observatory -- tests run --scenario sidebar"];
  const lines = [`Job ${job.jobId}: ${job.status}${job.error ? ` (${job.error})` : ""}`];
  lines.push(`  Plan:     ${job.plan?.scenarios?.join(", ")} × ${job.plan?.repeat}${job.plan?.build ? ", build first" : ", existing binary"}`);
  if (job.target?.binary) lines.push(`  Binary:   ${job.target.binary}${job.target.modifiedAt ? ` (built ${job.target.modifiedAt})` : ""}`);
  if (job.target?.source) lines.push(`  Source:   ${job.target.source.branch ?? "detached"} @ ${String(job.target.source.commit ?? "?").slice(0, 9)}${job.target.source.dirty ? " (dirty)" : ""}`);
  if (job.elapsedMs != null) lines.push(`  Elapsed:  ${ms(job.elapsedMs)}`);
  const current = job.progress?.current;
  if (current) {
    lines.push(`  Now:      ${current.scenario ?? ""} round ${current.round ?? "-"}, ${current.phase ?? ""}${current.actionsDone != null ? `, ${current.actionsDone}/${current.actionsPlanned ?? "?"} actions` : ""}`);
  }
  lines.push(`  Runs:     ${job.progress?.runsDone ?? 0} of ${job.progress?.runsPlanned ?? "?"}`);
  for (const run of job.results ?? []) {
    lines.push(`    ${run.runId} ${run.status}${run.settledMedianMs != null ? ` · settled median ${ms(run.settledMedianMs)}` : ""}${run.failedActions ? ` · ${run.failedActions} failed actions` : ""}`);
  }
  const summary = job.comparison?.summary;
  if (summary) {
    lines.push(`  Baseline: ${summary.regressions} regressions, ${summary.improvements} improvements, ${summary.insufficientEvidence} need more runs`);
  }
  return lines;
}

export function formatResponse(response) {
  if (!response.ok) return `Error (${response.error.code}): ${response.error.message}`;
  const result = response.result;
  switch (response.operation) {
    case "observatory.capabilities":
      return [
        `Observatory controls v${result.controlVersion}. Records: ${result.observatoryRoot}`,
        `Test binary: ${result.target.binary ?? "none built"}${result.target.modifiedAt ? ` (built ${result.target.modifiedAt})` : ""}`,
        `Source: ${result.target.source?.branch ?? "detached"} @ ${String(result.target.source?.commit ?? "?").slice(0, 9)}${result.target.source?.dirty ? " (dirty)" : ""}`,
        "", "Operations:", ...result.operations.map((op) => `  ${op.name.padEnd(30)} ${op.cli}`),
        "", "Add --json for the full description of every input and side effect.",
      ].join("\n");
    case "observatory.scenarios.list":
      return result.scenarios.map((entry) => `${entry.id} (v${entry.version}): ${entry.name}, ${entry.plannedActions} actions\n  ${entry.description ?? ""}`).join("\n");
    case "observatory.tests.run":
    case "observatory.tests.status":
    case "observatory.tests.cancel":
      return jobLines(result.job).join("\n");
    case "observatory.tests.list":
      return result.jobs.length ? result.jobs.map((job) => `${job.jobId} ${job.status} ${job.scenarios?.join(",") ?? ""} ×${job.repeat ?? "?"}`).join("\n") : "No test jobs yet.";
    case "observatory.runs.list":
      return result.runs.length
        ? result.runs.map((run) => `${run.runId} ${run.status}${run.settledMedianMs != null ? ` settled ${ms(run.settledMedianMs)}` : ""}${run.totalMs != null ? ` total ${ms(run.totalMs)}` : ""}`).join("\n")
        : "No runs match.";
    case "observatory.runs.artifact":
      return [`${result.name} of ${result.runId}, lines ${result.fromLine}-${result.totalLines}${result.redacted ? " (redacted)" : ""}:`, ...result.lines].join("\n");
    case "observatory.reports.generate":
      return result.markdown;
    case "observatory.reports.export":
      return [`Evidence for ${result.manifest.runId}:`, `  ${result.zip}`, `  ${result.dir}/ (${result.manifest.files.length} files, start with report.md)`,
        result.manifest.redaction.rawLogsIncluded ? "  Includes the full, unredacted log." : "  Redacted; no raw logs."].join("\n");
    default:
      return JSON.stringify(result, null, 2);
  }
}
