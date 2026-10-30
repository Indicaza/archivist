// Baseline commands and regression reports for the CLI. Collection (build,
// ui) stays in cli.mjs; everything here works on runs already recorded.
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { baselinesPath, readBaselines, removeBaseline, setBaseline } from "./baselines.mjs";
import { loadRuns } from "./dashboard/records.mjs";
import { buildReport, categoryLabel, categoryOf, CATEGORIES, resolveBudgets, summarizeCategories } from "./dashboard/public/regression.mjs";
import { formatRegressionReport } from "./regression-report.mjs";
import { makeRunId, writeJsonAtomic } from "./storage.mjs";

export const DEFAULT_BUDGETS = path.join(path.dirname(fileURLToPath(import.meta.url)), "budgets.json");
// `check --strict` when a regression is confirmed; distinct from the
// functional failure (1), usage (2) and refusal (3) codes.
export const EXIT_BUDGET_EXCEEDED = 5;

// A budgets file named with --budgets, else <Observatory folder>/budgets.json
// (a local override), else the repository's scripts/observatory/budgets.json.
export async function loadBudgets(root, file = null) {
  const candidates = file ? [path.resolve(file)] : [path.join(root, "budgets.json"), DEFAULT_BUDGETS];
  for (const candidate of candidates) {
    let text;
    try {
      text = await readFile(candidate, "utf8");
    } catch (error) {
      if (error.code === "ENOENT" && !file) continue;
      throw new Error(`Budgets file ${candidate} can't be read: ${error.message}`);
    }
    let raw;
    try {
      raw = JSON.parse(text);
    } catch (error) {
      throw new Error(`Budgets file ${candidate} is not valid JSON: ${error.message}`);
    }
    return { budgets: resolveBudgets(raw), source: candidate };
  }
  throw new Error("No budgets file found.");
}

export async function analyze(root, { candidates = null, budgetsFile = null } = {}) {
  const [runs, baselines, { budgets, source }] = await Promise.all([loadRuns(root), readBaselines(root), loadBudgets(root, budgetsFile)]);
  if (baselines.error) throw new Error(baselines.error);
  return buildReport({ runs, baselines: baselines.data, budgets, candidates, source: { budgets: source, baselines: baselinesPath(root) } });
}

export async function saveReport(root, report, text, kind) {
  const dir = path.join(root, "reports", makeRunId(kind));
  await mkdir(dir, { recursive: true });
  const jsonPath = path.join(dir, "report.json");
  const textPath = path.join(dir, "report.txt");
  await writeJsonAtomic(jsonPath, report);
  await writeFile(textPath, `${text}\n`, "utf8");
  return { dir, jsonPath, textPath };
}

const DASHBOARD_HINT = "npm run observatory:dashboard, Baselines page (or Observatory Here in the app)";

async function printAndSave(root, report, kind, { json = false } = {}) {
  const textNoPaths = formatRegressionReport(report, { dashboardUrl: DASHBOARD_HINT });
  const saved = await saveReport(root, report, textNoPaths, kind);
  if (json) {
    console.log(JSON.stringify(report, null, 2));
  } else {
    console.log(formatRegressionReport(report, { dashboardUrl: DASHBOARD_HINT, reportPath: saved.jsonPath }));
  }
  return saved;
}

function describeEntry(category, entry) {
  const ref = entry.reference ?? {};
  const where = `${ref.branch ?? "detached"} @ ${ref.commit ? ref.commit.slice(0, 9) : "?"}${ref.dirty ? " (dirty tree)" : ""}`;
  return [
    `${categoryLabel(category)} (${category})`,
    `  Runs:    ${entry.runIds.join(", ")}`,
    `  Source:  ${where}${ref.machine?.cpuModel ? ` · ${ref.machine.cpuModel}` : ""}${ref.config?.buildType ? ` · ${ref.config.buildType}` : ""}`,
    `  Set:     ${entry.setAt}${entry.note ? ` · ${entry.note}` : ""}`,
  ].join("\n");
}

// npm run observatory -- baseline set|list|remove|compare ...
export async function baselineCommand(root, args, options) {
  const [action, ...rest] = args;
  try {
    if (action === "set") {
      const { entry, previous } = await setBaseline(root, rest, { note: options.note ?? null });
      console.log(`Baseline set.\n${describeEntry(entry.category, entry)}`);
      if (previous) console.log(`Replaced the previous baseline (${previous.runIds.join(", ")}); it stays in the history. No runs were changed.`);
      if (entry.runIds.length < 3) {
        console.log(`Note: ${entry.runIds.length} run${entry.runIds.length === 1 ? "" : "s"}. The default budgets need 3 runs on each side before calling anything a regression; you can name several runs: baseline set <id> <id> <id>.`);
      }
      return 0;
    }
    if (action === "remove") {
      const previous = await removeBaseline(root, rest[0]);
      console.log(`Removed the ${categoryLabel(previous.category)} baseline (${previous.runIds.join(", ")}); it stays in the history. No runs were changed.`);
      return 0;
    }
    if (action === "list" || !action) {
      const { data, error } = await readBaselines(root);
      if (error) {
        console.error(error);
        return 1;
      }
      const entries = Object.entries(data.baselines);
      console.log(entries.length ? entries.map(([category, entry]) => describeEntry(category, entry)).join("\n\n") : "No baselines yet. Set one with: npm run observatory -- baseline set <run-id> [<run-id> ...]");
      const unset = Object.keys(CATEGORIES).filter((category) => !data.baselines[category]);
      if (entries.length && unset.length) console.log(`\nNo baseline: ${unset.join(", ")}`);
      if (data.history.length) console.log(`\n${data.history.length} earlier baseline${data.history.length === 1 ? "" : "s"} in ${baselinesPath(root)}.`);
      return 0;
    }
    if (action === "compare") {
      if (!rest.length) {
        console.error("Name the run(s) to compare: npm run observatory -- baseline compare <run-id> [<run-id> ...]");
        return 2;
      }
      const runs = await loadRuns(root);
      const candidates = {};
      for (const id of rest) {
        const run = runs.find((entry) => entry.runId === id);
        if (!run) throw new Error(`No run ${id} in ${root}.`);
        const category = categoryOf(run);
        if (!category) throw new Error(`${id} has no baseline category (only classified builds and UI scenario runs do).`);
        (candidates[category] ??= []).push(id);
      }
      const report = await analyze(root, { candidates, budgetsFile: options.budgets });
      await printAndSave(root, report, "compare", { json: options.json });
      return report.summary.functionalFailures ? 1 : 0;
    }
    console.error(`Unknown baseline action "${action}". Use set, list, remove or compare.`);
    return 2;
  } catch (error) {
    console.error(error.message);
    return 1;
  }
}

// After a build or UI run: compare with its baseline, if there is one.
// Never changes the command's exit code; a problem here is only reported.
export async function compareAfterRun(root, runIds) {
  try {
    const runs = await loadRuns(root);
    const { data } = await readBaselines(root);
    const categories = [...new Set(runIds.map((id) => categoryOf(runs.find((run) => run.runId === id))).filter(Boolean))];
    const withBaseline = categories.filter((category) => data?.baselines?.[category]);
    if (!withBaseline.length) {
      if (categories.length) console.log(`No ${categories.map(categoryLabel).join(" / ")} baseline yet. To compare later runs with this one: npm run observatory -- baseline set ${runIds.at(-1)}`);
      return null;
    }
    const report = await analyze(root);
    report.categories = report.categories.filter((category) => withBaseline.includes(category.category));
    report.summary = summarizeCategories(report.categories);
    return (await printAndSave(root, report, "after-run")).jsonPath;
  } catch (error) {
    console.log(`Baseline comparison skipped: ${error.message}`);
    return null;
  }
}

// The comparison half of `check`: every category with a baseline, against
// its newest compatible runs.
export async function checkReport(root, options) {
  const report = await analyze(root, { budgetsFile: options.budgets });
  if (options.scenario) {
    const keep = new Set(["build:clean", "build:incremental", "build:no-change", `ui:${options.scenario}`]);
    report.categories = report.categories.filter((category) => keep.has(category.category));
    report.summary = summarizeCategories(report.categories);
  }
  await printAndSave(root, report, "check", { json: options.json });
  if (report.summary.functionalFailures) return 1;
  if (options.strict && report.summary.regressions) return EXIT_BUDGET_EXCEEDED;
  return 0;
}
