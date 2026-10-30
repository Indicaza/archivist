// The short terminal summary printed after a UI Lab run.
const RULE = "─".repeat(45);

function row(label, value) {
  return `${`${label}:`.padEnd(19)}${value}`;
}

function ms(value) {
  return value == null ? "—" : `${value.toFixed(1)} ms`;
}

function spread(stats) {
  return `${ms(stats.medianMs)} / ${stats.p95Ms == null ? "—" : ms(stats.p95Ms)} / ${ms(stats.maxMs)}`;
}

export function formatUiSummary(summary, paths) {
  const { source, scenario, stats } = summary;
  const measured = stats?.measured;
  const lines = ["", "ARCHIVIST — UI TORTURE LAB", RULE];
  lines.push(row("Scenario", `${scenario.name} (v${scenario.version})`));
  if (summary.label) {
    lines.push(row("Label", summary.label));
  }
  lines.push(row("Git", `${source.branch ?? "(detached)"} @ ${source.commit?.slice(0, 9) ?? "unknown"}${source.dirty ? " (dirty)" : ""}`));
  if (measured) {
    const all = summary.actions;
    lines.push(row("Actions", `${all.total} (${measured.actions} measured, ${all.warmup} warm-up)`));
    lines.push(row("Passed", String(all.passed)));
    lines.push(row("Failed", String(all.failed)));
    lines.push("", "Measured actions     median / p95 / max");
    lines.push(row("  Settled (frames)", spread(measured.settledMs)));
    lines.push(row("  UI state reached", spread(measured.stateMs)));
    lines.push(row("  Dispatch", spread(measured.dispatchMs)));
    lines.push(row("  Longest frame gap", spread(measured.frameMaxIntervalMs)));
  }
  if (summary.runtimeTiming?.recognizedEvents) {
    lines.push(row("Wells timing", `${summary.runtimeTiming.recognizedEvents} events in runtime.log`));
  }
  lines.push("", row("Result", summary.status === "passed" ? "PASS" : summary.status.toUpperCase()));
  if (summary.error) {
    lines.push(summary.error.slice(0, 300));
  }
  lines.push("", row("Report", paths.summaryPath), row("Trace", paths.eventsPath), row("Logs", paths.logPath), RULE, "");
  return lines.join("\n");
}
