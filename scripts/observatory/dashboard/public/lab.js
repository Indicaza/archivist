// The Test Lab page (run UI scenarios, watch them, read the results) and
// the Copy for AI / Export evidence buttons used across the dashboard.
// Every action calls the same control operations an agent calls through
// the CLI (../../control/operations.mjs); nothing starts unless a person
// presses a button.
import { control, hasControlKey, liveArtifacts, loadBaselines, runDetails, runEvents, runLogText, runScenario } from "./data.js";
import { comparisonMarkdown, diagnosticReport } from "./diagnostic.mjs";
import { empty, h, kv, ms, statusPill, table, when } from "./dom.js";
import { makeRedactor } from "./redact.mjs";
import { buildReport, categoryLabel, categoryOf } from "./regression.mjs";

let context = { getState: () => ({ runs: [], root: "" }), refresh: async () => {} };

export function configureLab(value) {
  context = value;
}

const ACTIVE = new Set(["queued", "starting", "running"]);
const VERDICT_WORDS = {
  regression: "regression", improvement: "improvement", "no-meaningful-change": "no meaningful change",
  "insufficient-evidence": "needs more runs", incompatible: "incompatible",
};
// Each Test Lab render gets a number; an older render's polling stops.
let generation = 0;

// ---- Copy for AI ----------------------------------------------------------------

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    // Older engines and some embedded views: a hidden text area and copy.
    const area = h("textarea", { class: "offscreen", readonly: true });
    area.value = text;
    document.body.append(area);
    area.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch {
      ok = false;
    }
    area.remove();
    return ok;
  }
}

// This run's category analysis, with `withRunIds` (e.g. a job's other rounds)
// as the compared group; the same rule as observatory.reports.generate.
async function categoryFor(runId, withRunIds = []) {
  const { runs } = context.getState();
  const run = runs.find((entry) => entry.runId === runId);
  const category = run && categoryOf(run);
  if (!category) return null;
  const files = await loadBaselines().catch(() => null);
  if (!files?.baselines || !files.budgets) return null;
  const ids = [...new Set([runId, ...withRunIds])].filter((id) => categoryOf(runs.find((entry) => entry.runId === id) ?? {}) === category);
  const report = buildReport({ runs, baselines: files.baselines, budgets: files.budgets, candidates: { [category]: ids } });
  const result = report.categories[0] ?? null;
  return result?.status === "no-baseline" ? null : result;
}

export async function reportFor(runId, withRunIds = []) {
  const { summary } = runDetails(runId);
  if (!summary) throw new Error("This run's summary can't be read, so there is nothing to report.");
  const [events, scenario, logText, category, live] = await Promise.all([
    runEvents(runId).then((value) => value.actions).catch(() => null),
    runScenario(runId),
    runLogText(runId),
    categoryFor(runId, withRunIds),
    summary.kind === "live-session" ? liveArtifacts(runId) : null,
  ]);
  const redact = makeRedactor({ root: context.getState().root || null });
  return diagnosticReport({ runId, summary, events, scenario, category, logText, live, redact });
}

function copyButton(label, makeText) {
  const status = h("span", { class: "muted small", role: "status" });
  const preview = h("details", { class: "report-preview", hidden: true });
  const button = h("button", {
    class: "primary",
    type: "button",
    onclick: async () => {
      button.disabled = true;
      status.textContent = "Writing the report…";
      try {
        const text = await makeText();
        const copied = await copyText(text);
        status.textContent = copied
          ? `Copied ${Math.round(text.length / 100) / 10} KB of Markdown. Paths and common secret formats are redacted; read it before sharing.`
          : "Couldn't reach the clipboard; the report is below to copy by hand.";
        preview.replaceChildren(h("summary", {}, "Show the report"), h("pre", { class: "log report" }, text));
        preview.hidden = false;
        if (!copied) preview.open = true;
      } catch (error) {
        status.textContent = error.message;
      } finally {
        button.disabled = false;
      }
    },
  }, label);
  return h("div", { class: "copy-ai" }, h("div", { class: "row" }, button, status), preview);
}

export { copyText };

export function copyForAiButton(runId, { withRunIds = [], label = "Copy for AI" } = {}) {
  return copyButton(label, async () => (await reportFor(runId, withRunIds)).markdown);
}

export function comparisonCopyButton(category) {
  const redact = makeRedactor({ root: context.getState().root || null });
  return copyButton("Copy for AI", async () => comparisonMarkdown(category, { createdAt: new Date().toISOString(), redact }));
}

// ---- Export evidence ------------------------------------------------------------

export function exportButton(runId) {
  if (!hasControlKey()) {
    return h("p", { class: "muted small" }, "Export evidence (a redacted .zip for an agent): ",
      h("code", { class: "cmd" }, `npm run observatory -- export ${runId}`));
  }
  const status = h("div", { class: "small", role: "status" });
  const raw = h("input", { type: "checkbox" });
  const button = h("button", {
    type: "button",
    onclick: async () => {
      if (raw.checked && !window.confirm("Include the full, unredacted log? It can contain file paths, file names and other private text from this machine.")) return;
      button.disabled = true;
      status.textContent = "Exporting…";
      const response = await control("observatory.reports.export", { runId, includeRawLogs: raw.checked });
      button.disabled = false;
      if (!response.ok) {
        status.replaceChildren(h("span", { class: "verdict-worse" }, response.error.message));
        return;
      }
      const { zip, manifest } = response.result;
      status.replaceChildren(
        h("span", {}, `Saved ${manifest.files.length + 1} files (${manifest.redaction.rawLogsIncluded ? "includes the unredacted log" : "redacted"}): `),
        h("code", { class: "cmd" }, zip), " ",
        h("button", { type: "button", onclick: async () => { await copyText(zip); } }, "Copy path"));
    },
  }, "Export evidence");
  return h("div", { class: "export" }, h("div", { class: "row" }, button,
    h("label", { class: "small" }, raw, " include the full log (not redacted)")), status);
}

// ---- Test Lab page --------------------------------------------------------------

function jobTitle(job, scenarios = []) {
  const ids = job.plan?.scenarios ?? [];
  const name = ids.length > 1 ? "All scenarios" : scenarios.find((entry) => entry.id === ids[0])?.name ?? ids[0] ?? "UI test";
  return `${name} × ${job.plan?.repeat ?? 1} round${job.plan?.repeat === 1 ? "" : "s"}`;
}

function progressBlock(job) {
  const current = job.progress?.current;
  const rows = [
    ["Status", statusPill(job.status)],
    ["Elapsed", job.elapsedMs == null ? "—" : ms(job.elapsedMs)],
    ["Runs", `${job.progress?.runsDone ?? 0} of ${job.progress?.runsPlanned ?? "?"}`],
  ];
  if (current) {
    rows.push(["Now", `${current.scenario ?? "build"}${current.round ? `, round ${current.round}` : ""}: ${current.phase ?? ""}`]);
    if (current.actionsDone != null) rows.push(["Actions", `${current.actionsDone} of ${current.actionsPlanned ?? "?"}`]);
    if (current.lastAction) {
      rows.push(["Last action", `#${current.lastAction.index} ${current.lastAction.label ?? current.lastAction.op} (${current.lastAction.phase}): ${current.lastAction.ok ? `settled ${ms(current.lastAction.settledMs)}` : `failed: ${current.lastAction.error ?? ""}`}`]);
    }
  }
  if (job.target?.binary) rows.push(["Binary", h("span", { class: "mono path" }, `${job.target.binary}${job.target.modifiedAt ? `, built ${when(job.target.modifiedAt)}` : ""}`)]);
  const parts = [kv(rows)];
  // A bar only when the runner reports both numbers; never a guess.
  if (current?.actionsDone != null && current?.actionsPlanned) {
    const bar = h("div", { class: "bar" }, h("span"));
    bar.firstChild.style.width = `${Math.min(100, Math.round((current.actionsDone / current.actionsPlanned) * 100))}%`;
    parts.push(bar);
  }
  if (job.error) parts.push(h("div", { class: "notice bad" }, job.error));
  if (job.cancelRequestedAt && ACTIVE.has(job.status)) parts.push(h("p", { class: "muted" }, "Stopping…"));
  return parts;
}

function resultsBlock(job) {
  if (!job.results?.length) return null;
  const ids = job.results.map((run) => run.runId);
  const parts = [h("h3", {}, "Results"), table(
    ["Run", "Scenario", "Status", { label: "Settled median", num: true }, { label: "p95", num: true }, { label: "Frame gap median", num: true }, { label: "Failed actions", num: true }],
    job.results.map((run) => ({
      cells: [h("a", { href: `#/run/${run.runId}`, class: "mono" }, run.runId), run.scenario ?? "—", statusPill(run.status), ms(run.settledMedianMs ?? null),
        run.settledP95Ms == null ? "n/a" : ms(run.settledP95Ms), ms(run.frameGapMedianMs ?? null), run.failedActions ?? "—"],
    })))];
  const latest = context.getState().runs.find((run) => run.runId === ids.at(-1));
  const steps = [...(latest?.ui?.steps ?? [])].filter((step) => step.settledMedianMs != null).sort((a, b) => b.settledMedianMs - a.settledMedianMs).slice(0, 5);
  if (steps.length) {
    parts.push(h("h3", {}, "Slowest steps in the last run"), table(["Step", { label: "Settled median", num: true }, { label: "Max", num: true }, { label: "Failed", num: true }],
      steps.map((step) => ({ cells: [`${step.step}: ${step.label}`, ms(step.settledMedianMs), ms(step.settledMaxMs), step.failed ?? 0] }))));
  }
  if (job.comparison) {
    const compared = job.comparison.categories.filter((category) => category.status !== "no-baseline");
    parts.push(h("h3", {}, "Against the baseline"));
    if (!compared.length) {
      parts.push(h("p", { class: "muted" }, "No baseline is set for these scenarios, so nothing was compared. ", h("a", { href: "#/baselines" }, "Baselines")));
    }
    for (const category of compared) {
      const notable = [...category.metrics, ...category.steps].filter((metric) => ["regression", "improvement"].includes(metric.verdict) || metric.overBudget);
      parts.push(h("div", { class: "notice" }, h("strong", {}, `${categoryLabel(category.category)}: ${VERDICT_WORDS[category.verdict] ?? category.verdict}`),
        category.reasons.length ? h("ul", {}, category.reasons.map((reason) => h("li", {}, reason))) : null,
        notable.length ? h("ul", {}, notable.map((metric) => h("li", {}, `${metric.label}: ${ms(metric.baseline.median)} → ${ms(metric.candidate.median)} (${metric.deltaPercent > 0 ? "+" : ""}${metric.deltaPercent}%), ${metric.verdict}${metric.overBudget && metric.verdict !== "regression" ? ", over budget but unconfirmed" : ""}`))) : null,
        h("p", {}, h("a", { href: "#/baselines" }, "Details on the Baselines page"))));
    }
  }
  const last = ids.at(-1);
  parts.push(h("h3", {}, "Hand it to an AI"), copyForAiButton(last, { withRunIds: ids.slice(0, -1), label: "Copy for AI (last run, compared with this job's other runs)" }), exportButton(last));
  return parts;
}

export async function labPage() {
  const parts = [h("div", { class: "page-head" }, h("h2", {}, "Test Lab"),
    h("span", { class: `pill ${hasControlKey() ? "good" : ""}` }, hasControlKey() ? "controls on" : "read-only"))];
  parts.push(h("p", { class: "muted" },
    "Runs the UI Lab scenarios against the Archivist app already built in build/frontend-debug, as a separate app with a throwaway profile. ",
    "Your open Archivist window is not touched or restarted. One test runs at a time."));

  if (!hasControlKey()) {
    parts.push(h("div", { class: "notice warn" }, h("strong", {}, "This dashboard is read-only."),
      h("p", {}, "To run tests from this page, start it with controls and open the link it prints: ", h("code", { class: "cmd" }, "npm run observatory:dashboard -- --control")),
      h("p", {}, "Inside Archivist, the Observatory panel has controls built in. An agent can do the same from a terminal: ", h("code", { class: "cmd" }, "npm run observatory -- tests run --scenario sidebar --wait --json"))));
    return parts;
  }

  const caps = await control("observatory.capabilities");
  if (!caps.ok) {
    parts.push(h("div", { class: "notice bad" }, `Controls aren't available: ${caps.error.message}`));
    return parts;
  }
  const target = caps.result.target;
  const source = target.source ?? {};
  parts.push(h("div", { class: "grid cards" },
    h("div", { class: "card" }, h("h3", {}, "What will be tested"), kv([
      ["Binary", target.exists ? h("span", { class: "mono path" }, target.binary) : h("span", { class: "verdict-worse" }, "not built yet")],
      ["Built", target.modifiedAt ? when(target.modifiedAt) : null],
      ["Source", `${source.branch ?? "detached"} @ ${(source.commit ?? "?").slice(0, 9)}${source.dirty ? ` · dirty (${source.changedFiles} files)` : ""}`],
    ]), source.dirty ? h("p", { class: "muted small" }, "The working tree has changes; the binary may not match the commit. Build first to test exactly this checkout.") : null)));

  const scenario = h("select", {}, h("option", { value: "all" }, "All scenarios"),
    caps.result.scenarios.map((entry) => h("option", { value: entry.id }, `${entry.name} (v${entry.version})`)));
  const repeat = h("input", { type: "number", min: 1, max: caps.result.limits.maxRepeat, value: 1, class: "narrow" });
  const build = h("input", { type: "checkbox" });
  const runButton = h("button", { class: "primary", type: "button" }, "Run test");
  const formStatus = h("span", { class: "small", role: "status" });
  const jobArea = h("div");
  const historyArea = h("div");
  parts.push(h("div", { class: "card lab-form" }, h("h3", {}, "Run"),
    h("div", { class: "filters" },
      h("label", {}, "Scenario", scenario),
      h("label", {}, "Rounds", repeat),
      h("label", {}, h("span", {}, "Build first"), h("span", { class: "row" }, build, h("span", { class: "muted small" }, "rebuilds build/frontend-debug; nothing is restarted"))),
      runButton),
    h("p", { class: "muted small" }, "Three or more rounds let the baseline comparison confirm a change."), formStatus));
  parts.push(jobArea, historyArea);

  let timer = null;
  let lastStatus = null;
  const mine = ++generation;
  const onLab = () => mine === generation && location.hash.startsWith("#/lab");

  const showHistory = async () => {
    const list = await control("observatory.tests.list", { limit: 8 });
    if (!list.ok || !list.result.jobs.length) return historyArea.replaceChildren();
    historyArea.replaceChildren(h("h3", {}, "Recent tests"), table(["Job", "Scenarios", { label: "Rounds", num: true }, "Status", "Started"],
      list.result.jobs.map((job) => ({ cells: [h("span", { class: "mono" }, job.jobId), (job.scenarios ?? []).join(", "), job.repeat ?? "—", statusPill(job.status), when(job.createdAt)] }))));
  };

  const showJob = async (jobId) => {
    const response = await control("observatory.tests.status", jobId ? { jobId } : {});
    if (!response.ok) {
      jobArea.replaceChildren(h("div", { class: "notice bad" }, response.error.message));
      return null;
    }
    const job = response.result.job;
    if (!job) {
      jobArea.replaceChildren(empty("No tests run from here yet."));
      return null;
    }
    const active = ACTIVE.has(job.status);
    if (!active && lastStatus && ACTIVE.has(lastStatus)) {
      // Finished: read the new runs; the page renders again with the results.
      await context.refresh();
      return null;
    }
    lastStatus = job.status;
    const cancel = active ? h("button", {
      type: "button",
      onclick: async () => {
        cancel.disabled = true;
        const result = await control("observatory.tests.cancel", { jobId: job.jobId });
        if (!result.ok) formStatus.textContent = result.error.message;
        await showJob(job.jobId);
      },
    }, "Cancel") : null;
    jobArea.replaceChildren(h("section", { class: "card" },
      h("div", { class: "page-head" }, h("h3", {}, active ? "Running: " : "Last test: ", jobTitle(job, caps.result.scenarios)), h("span", { class: "mono muted small" }, job.jobId)),
      progressBlock(job), cancel, active ? null : resultsBlock(job)));
    runButton.disabled = active;
    return job;
  };

  const poll = async (jobId) => {
    clearTimeout(timer);
    if (!onLab()) return;
    const job = await showJob(jobId);
    if (job && ACTIVE.has(job.status) && onLab()) timer = setTimeout(() => poll(job.jobId), 1000);
    else await showHistory();
  };

  runButton.addEventListener("click", async () => {
    if (build.checked && !window.confirm("Build first? This rebuilds build/frontend-debug, which can take minutes. A running Archivist is not restarted, but its binary on disk is replaced.")) return;
    runButton.disabled = true;
    formStatus.textContent = "Starting…";
    const response = await control("observatory.tests.run", { scenario: scenario.value, repeat: Number(repeat.value), build: build.checked });
    if (!response.ok) {
      formStatus.textContent = response.error.message;
      runButton.disabled = false;
      return;
    }
    formStatus.textContent = "";
    poll(response.result.job.jobId);
  });

  poll(null);
  return parts;
}
