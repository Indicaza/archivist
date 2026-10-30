// The readable regression report printed after a comparison. The same
// report is saved as JSON next to it for tools and agents.
import { categoryLabel } from "./dashboard/public/regression.mjs";

const RULE = "─".repeat(52);
const WORDS = {
  regression: "REGRESSION",
  improvement: "IMPROVED",
  "no-meaningful-change": "NO CHANGE",
  "insufficient-evidence": "NEEDS MORE RUNS",
  incompatible: "INCOMPATIBLE",
  "not-measured": "NOT MEASURED",
};

function amount(value, unit) {
  if (value == null) return "—";
  if (unit !== "ms") return String(value);
  return Math.abs(value) >= 1000 ? `${(value / 1000).toFixed(2)} s` : `${value.toFixed(1)} ms`;
}

function signed(value, unit) {
  if (value == null) return "—";
  return `${value > 0 ? "+" : value < 0 ? "−" : "±"}${amount(Math.abs(value), unit)}`;
}

function percent(value) {
  if (value == null) return "    —";
  return `${value > 0 ? "+" : ""}${value.toFixed(1)}%`.padStart(7);
}

function who(commit, branch) {
  return `${branch ?? "detached"} @ ${commit ? commit.slice(0, 9) : "?"}`;
}

function metricLine(metric) {
  const word = metric.overBudget && metric.verdict !== "regression" ? `${WORDS[metric.verdict]} (over budget, unconfirmed)` : WORDS[metric.verdict];
  const evidence = `n=${metric.baseline.n}/${metric.candidate.n}`;
  return `  ${metric.label.padEnd(38).slice(0, 38)} ${percent(metric.deltaPercent)} ${signed(metric.delta, metric.unit).padStart(10)}  ${evidence.padEnd(7)} ${word}`;
}

export function formatRegressionReport(report, { dashboardUrl = null, reportPath = null } = {}) {
  const lines = ["ARCHIVIST OBSERVATORY", "Performance comparison against baselines", RULE];
  for (const category of report.categories) {
    lines.push("", categoryLabel(category.category).toUpperCase());
    if (category.baseline) {
      lines.push(`  Baseline:  ${who(category.baseline.commit, category.baseline.branch)} · ${category.baseline.runIds.length} run${category.baseline.runIds.length === 1 ? "" : "s"}: ${category.baseline.runIds.join(", ")}`);
    }
    if (category.candidate.runIds.length) {
      const branches = category.candidate.branches.join(", ") || "?";
      const commits = category.candidate.commits.map((commit) => commit.slice(0, 9)).join(", ") || "?";
      lines.push(`  Candidate: ${branches} @ ${commits} · ${category.candidate.runIds.length} run${category.candidate.runIds.length === 1 ? "" : "s"}: ${category.candidate.runIds.join(", ")}`);
    }
    for (const reason of category.reasons) lines.push(`  ${category.verdict === "incompatible" ? "Incompatible" : "Note"}: ${reason}`);
    for (const skip of category.skipped) lines.push(`  Skipped ${skip.runId}: ${skip.reason}`);
    for (const failure of category.functional.failedRuns) {
      lines.push(`  FUNCTIONAL FAILURE ${failure.runId}: ${failure.status}${failure.error ? ` (${failure.error})` : ""}`);
    }
    if (category.functional.failedActions) lines.push(`  Failed actions: ${category.functional.failedActions}`);
    for (const metric of category.metrics.filter((entry) => entry.verdict !== "not-measured")) {
      lines.push(metricLine(metric));
      if (metric.verdict === "insufficient-evidence" || metric.verdict === "regression" || metric.verdict === "improvement") {
        lines.push(`      ${metric.reasons.join("; ")}`);
      }
    }
    const notable = category.steps.filter((step) => step.verdict === "regression" || step.verdict === "improvement" || step.overBudget);
    if (category.steps.length) {
      lines.push(`  Slowest steps (candidate median): ${category.steps.slice(0, 3).map((step) => `${step.label} ${amount(step.candidate.median, "ms")}`).join(" · ")}`);
    }
    for (const step of notable) lines.push(metricLine(step));
  }
  const s = report.summary;
  lines.push("", "Overall:");
  const overall = [
    s.functionalFailures && `${s.functionalFailures} functional failure${s.functionalFailures === 1 ? "" : "s"} (failed runs, reported apart from performance).`,
    `${s.regressions} confirmed regression${s.regressions === 1 ? "" : "s"}, ${s.improvements} confirmed improvement${s.improvements === 1 ? "" : "s"}.`,
    s.unconfirmedOverBudget && `${s.unconfirmedOverBudget} metric${s.unconfirmedOverBudget === 1 ? " is" : "s are"} past budget but unconfirmed: record more runs.`,
    s.insufficientEvidence && `${s.insufficientEvidence} comparison${s.insufficientEvidence === 1 ? " needs" : "s need"} more evidence.`,
    s.incompatibleCategories && `${s.incompatibleCategories} categor${s.incompatibleCategories === 1 ? "y is" : "ies are"} incompatible with its baseline.`,
    s.categoriesWithoutBaseline && `${s.categoriesWithoutBaseline} categor${s.categoriesWithoutBaseline === 1 ? "y has" : "ies have"} no baseline.`,
  ].filter(Boolean);
  lines.push(...overall.map((line) => `  ${line}`));
  lines.push("", "Confirmed means: enough runs on each side, past the budget, and no overlap between the runs. It is a rule of thumb, not a significance test.");
  if (dashboardUrl) lines.push(`Details: ${dashboardUrl}`);
  if (reportPath) lines.push(`Report:  ${reportPath}`);
  lines.push(RULE);
  return lines.join("\n");
}
