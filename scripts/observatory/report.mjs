// The short terminal summary printed after a capture.
import { PHASE_LABELS } from "./build-phases.mjs";

const RULE = "─".repeat(45);

function seconds(ms) {
  return ms == null ? "—" : `${(ms / 1000).toFixed(1)} s`;
}

function row(label, value) {
  return `${`${label}:`.padEnd(13)}${value}`;
}

export function phaseLine(phase) {
  const label = PHASE_LABELS[phase.name] ?? phase.name;
  if (phase.status === "skipped") {
    return row(label, "skipped");
  }
  let value = seconds(phase.durationMs);
  if (phase.name === "ide-bundle" && phase.status === "success") {
    value = `${phase.details.cacheReused ? "cached" : "rebuilt"} (${value})`;
  } else if (phase.name === "build-plan" && phase.status === "success") {
    value = `${phase.details.plannedWorkSteps} steps + ${phase.details.plannedBookkeepingSteps} CMake checks (${value})`;
  } else if (phase.name === "native-build" && phase.details?.ninjaLog?.basis === "measured-by-ninja") {
    value = `${value}  (${phase.details.ninjaLog.workSteps} steps run)`;
  }
  if (phase.status !== "success") {
    value += `  ${phase.status.toUpperCase()}`;
  }
  return row(label, value);
}

export function formatBuildSummary(summary, { summaryPath, logPath }) {
  const { source } = summary;
  const lines = ["", "ARCHIVIST OBSERVATORY", RULE];
  lines.push(row("Build", summary.status.toUpperCase()));
  if (summary.label) {
    lines.push(row("Label", summary.label));
  }
  lines.push(row("Git", `${source.branch ?? "(detached)"} @ ${source.commit?.slice(0, 9) ?? "unknown"}`));
  lines.push(row("Workspace", source.dirty == null ? "unknown" : source.dirty ? `dirty (${source.changedFiles} changed)` : "clean"));
  if (summary.buildConditions?.classification?.value) {
    lines.push(row("Kind", summary.buildConditions.classification.value));
  }
  lines.push("");
  summary.phases.forEach((phase) => lines.push(phaseLine(phase)));
  lines.push(row("Total", seconds(summary.totalDurationMs)));

  const failed = summary.phases.find((phase) => phase.error);
  if (failed) {
    lines.push("", `${PHASE_LABELS[failed.name] ?? failed.name} ${failed.status}: ${failed.error.message}`);
    failed.error.excerpt.slice(0, 5).forEach((line) => lines.push(`  ${line.slice(0, 160)}`));
  }
  lines.push("", row("Report", summaryPath), row("Log", logPath), RULE, "");
  return lines.join("\n");
}
