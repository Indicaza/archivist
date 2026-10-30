// Archivist Observatory dashboard. Reads runs from the local server and
// renders them; never writes anything. All text is set with textContent.
import { lineChart, stackedBars } from "./charts.js";
import { compareRuns, SAME_WITHIN_PCT } from "./compare.mjs";
import { artifactPreview, artifactUrl, compare, initControl, loadBaselines, loadRuns, runDetails, runEvents } from "./data.js";
import { buildReport, categoryLabel, categoryOf, resolveBudgets } from "./regression.mjs";
import { filterRuns } from "./filter.mjs";
import { axisMs, empty, gitText, h, kindLabel, kv, legend, ms, OK, runLink, shortSha, statusPill, table, when } from "./dom.js";
import { comparisonCopyButton, configureLab, copyForAiButton, exportButton, labPage } from "./lab.js";

const view = document.getElementById("view");
const state = { runs: [], options: {}, root: "", error: null };

const PHASES = [
  ["ide-bundle", "IDE bundle", "var(--series-3)"],
  ["configure", "Configure", "var(--series-2)"],
  ["build-plan", "Build plan", "var(--series-5)"],
  ["native-build", "Native build", "var(--series-1)"],
];
const SCENARIO_COLORS = ["var(--series-1)", "var(--series-2)", "var(--series-3)", "var(--series-4)", "var(--series-5)"];

function chronological(runs) {
  return [...runs].sort((a, b) => a.runId.localeCompare(b.runId));
}

function byKind(kind) {
  return state.runs.filter((run) => run.kind === kind && run.readable && run.supported);
}

// The latest earlier run that the comparison logic accepts against `run`.
function previousComparable(run) {
  const earlier = state.runs.filter((other) => other.runId < run.runId && other.kind === run.kind && OK.has(other.status)
    && (run.kind !== "ui-scenario" || (other.ui?.scenarioId === run.ui?.scenarioId && other.ui?.scenarioVersion === run.ui?.scenarioVersion)));
  return earlier[0] ?? null;
}

// ---- pages ------------------------------------------------------------------

function overview() {
  if (!state.runs.length) {
    return [h("div", { class: "page-head" }, h("h2", {}, "Overview")),
      empty("No Observatory runs yet.", h("span", {}, "Record one with ", h("code", {}, "npm run observatory:build"), " or ", h("code", {}, "npm run observatory:ui"), "."))];
  }
  const builds = byKind("build");
  const uiRuns = byKind("ui-scenario");
  const latestBuild = builds[0];
  const scenarios = [...new Set(uiRuns.map((run) => run.ui?.scenarioId).filter(Boolean))];
  const failures = state.runs.filter((run) => !OK.has(run.status)).slice(0, 6);

  const cards = [];
  if (latestBuild) {
    cards.push(h("div", { class: "card" },
      h("h3", {}, "Latest build ", statusPill(latestBuild.status)),
      h("div", { class: "big" }, ms(latestBuild.build?.totalMs)),
      h("div", { class: "row muted small" }, h("span", {}, latestBuild.build?.classification ?? "kind unknown"), h("span", {}, latestBuild.build?.ideCacheReused == null ? "" : latestBuild.build.ideCacheReused ? "IDE cached" : "IDE rebuilt")),
      h("div", { class: "row small" }, h("span", { class: "mono" }, gitText(latestBuild)), runLink(latestBuild))));
  }
  for (const id of scenarios) {
    const latest = uiRuns.find((run) => run.ui?.scenarioId === id);
    cards.push(h("div", { class: "card" },
      h("h3", {}, latest.ui.scenarioName ?? id, " ", statusPill(latest.status)),
      h("div", { class: "big" }, ms(latest.ui.settled?.medianMs), h("span", { class: "muted small" }, " median settled")),
      h("div", { class: "row muted small" }, h("span", {}, `p95 ${ms(latest.ui.settled?.p95Ms)} · max ${ms(latest.ui.settled?.maxMs)}`), h("span", {}, `${latest.ui.failedActions ?? "?"} failed`)),
      h("div", { class: "row small" }, h("span", { class: "mono" }, gitText(latest)), runLink(latest))));
  }
  const machine = state.runs.find((run) => run.machine?.cpuModel)?.machine;
  if (machine) {
    cards.push(h("div", { class: "card" }, h("h3", {}, "Machine (latest run)"),
      kv([["CPU", machine.cpuModel], ["Cores", machine.logicalCpus], ["Memory", machine.totalMemoryBytes ? `${Math.round(machine.totalMemoryBytes / 2 ** 30)} GB` : null], ["OS", [machine.platform, machine.osVersion, machine.architecture].filter(Boolean).join(" ")]])));
  }

  // What changed since the previous comparable run, per build / scenario.
  const latestPerSeries = [latestBuild, ...scenarios.map((id) => uiRuns.find((run) => run.ui?.scenarioId === id))].filter((run) => run && OK.has(run.status));
  const changes = latestPerSeries.map((run) => {
    const previous = previousComparable(run);
    if (!previous) return null;
    const result = compareRuns(previous, run);
    const headline = result.metrics.find((metric) => metric.key === "totalMs" || metric.key === "settled.median");
    return { run, previous, result, headline };
  }).filter(Boolean);

  return [
    h("div", { class: "page-head" }, h("h2", {}, "Overview"), h("span", { class: "muted small" }, `${state.runs.length} runs`)),
    h("div", { class: "grid cards" }, cards),
    changes.length ? h("section", {}, h("h3", {}, "Since the previous comparable run"),
      table(["", "Headline", "Before", "Now", "Change", "Better / worse / same", "Warnings", ""], changes.map(({ run, previous, result, headline }) => ({
        cells: [kindLabel(run), headline?.label ?? "—", ms(headline?.baseline), ms(headline?.candidate), verdictCell(headline),
          `${result.totals.better} / ${result.totals.worse} / ${result.totals.same}`,
          result.warnings.length ? h("span", { class: "pill warn" }, `${result.warnings.length}`) : "none",
          h("a", { href: `#/compare?a=${previous.runId}&b=${run.runId}` }, "Compare")],
      })))) : null,
    h("div", { class: "grid two" },
      h("section", { class: "card" }, h("h3", {}, "Build time"), buildTrend(builds.slice(0, 30))),
      h("section", { class: "card" }, h("h3", {}, "UI latency (median settled)"), uiTrend(uiRuns.slice(0, 60), "median"))),
    h("section", {}, h("h3", {}, "Recent failures"),
      failures.length ? table(["When", "Run", "Status", "What happened"].map((label, index) => (index === 3 ? { label, wrap: true } : label)), failures.map((run) => ({
        source: run,
        cells: [runLink(run), kindLabel(run), statusPill(run.status), run.error ?? run.problems?.join("; ") ?? (run.build?.failedPhase ? `failed in ${run.build.failedPhase}` : "—")],
      })), { onRow: (run) => go(`#/run/${run.runId}`) }) : empty("No failed runs.")),
  ];
}

function verdictCell(metric) {
  if (!metric || metric.verdict === "missing") return h("span", { class: "muted" }, "not comparable");
  const sign = metric.delta > 0 ? "+" : "";
  const text = metric.unit === "ms" ? `${sign}${ms(metric.delta)}` : `${sign}${metric.delta}`;
  return h("span", { class: `verdict-${metric.verdict}` }, `${text}${metric.deltaPct == null ? "" : ` (${sign}${metric.deltaPct}%)`}`, metric.verdict === "same" ? " · about the same" : "");
}

function buildTrend(builds) {
  const runs = chronological(builds);
  if (!runs.length) return empty("No builds recorded.");
  const chart = lineChart({
    count: runs.length,
    format: axisMs,
    series: [{
      name: "Total",
      color: "var(--series-1)",
      points: runs.map((run, index) => ({
        x: index,
        y: OK.has(run.status) ? run.build?.totalMs ?? null : null,
        failed: !OK.has(run.status),
        title: `${when(run.createdAt)} · ${run.status}\n${ms(run.build?.totalMs)} total · ${run.build?.classification ?? "?"}\n${gitText(run)}`,
        href: `#/run/${run.runId}`,
      })),
    }],
  });
  return [chart, legend([["Total (successful builds)", "var(--series-1)"], ["Failed build (not plotted)", "var(--bad)"]])];
}

// One line per scenario version: a changed scenario is a different workload.
function uiTrend(uiRuns, which) {
  const runs = chronological(uiRuns);
  const key = (run) => `${run.ui?.scenarioId} v${run.ui?.scenarioVersion}`;
  const scenarios = [...new Set(runs.filter((run) => run.ui?.scenarioId).map(key))];
  if (!runs.length) return empty("No UI runs recorded.", h("span", {}, "Run ", h("code", {}, "npm run observatory:ui"), "."));
  const field = which === "p95" ? "p95Ms" : "medianMs";
  const series = scenarios.map((id, index) => ({
    name: id,
    color: SCENARIO_COLORS[index % SCENARIO_COLORS.length],
    points: runs.map((run, x) => (key(run) === id ? {
      x,
      y: OK.has(run.status) ? run.ui.settled?.[field] ?? null : null,
      failed: !OK.has(run.status),
      title: `${run.ui.scenarioName} v${run.ui.scenarioVersion} · ${run.status}\nmedian ${ms(run.ui.settled?.medianMs)} · p95 ${ms(run.ui.settled?.p95Ms)}\n${gitText(run)}`,
      href: `#/run/${run.runId}`,
    } : null)).filter(Boolean),
  }));
  return [lineChart({ count: runs.length, format: axisMs, series }),
    legend([...series.map((line) => [line.name, line.color]), ["Failed run (not plotted)", "var(--bad)"]])];
}

function buildsPage() {
  const builds = byKind("build");
  if (!builds.length) return [h("h2", {}, "Builds"), empty("No builds recorded.", h("span", {}, "Run ", h("code", {}, "npm run observatory:build"), "."))];
  const recent = chronological(builds.slice(0, 30));
  return [
    h("div", { class: "page-head" }, h("h2", {}, "Build performance"), h("span", { class: "muted small" }, `${builds.length} builds`)),
    h("section", { class: "card" }, h("h3", {}, "Phases per build (last 30)"),
      stackedBars({
        format: axisMs,
        bars: recent.map((run) => ({
          label: when(run.createdAt).split(",")[0],
          title: `${when(run.createdAt)} · ${run.status} · ${run.build?.classification ?? "?"}\n${gitText(run)}`,
          href: `#/run/${run.runId}`,
          failed: !OK.has(run.status),
          segments: PHASES.map(([key, name, color]) => ({ name, color, value: run.build?.phases?.[key] ?? null })),
        })),
      }),
      legend([...PHASES.map(([, name, color]) => [name, color]), ["Failed", "var(--bad)"]]),
      h("p", { class: "muted small" }, "Durations are measured per phase with a monotonic clock. Build kind (clean / incremental / no-change) is inferred from Ninja's log.")),
    h("section", {}, h("h3", {}, "All builds"),
      table(["When", "Status", "Kind", "IDE bundle", { label: "Configure", num: true }, { label: "Native", num: true }, { label: "Total", num: true }, { label: "Steps", num: true }, "Git", "Label"],
        builds.map((run) => ({
          source: run,
          cells: [runLink(run), statusPill(run.status), run.build?.classification ?? "—",
            run.build?.ideCacheReused == null ? "—" : run.build.ideCacheReused ? "cached" : "rebuilt",
            ms(run.build?.phases?.configure), ms(run.build?.phases?.["native-build"]), ms(run.build?.totalMs), run.build?.ninjaWorkSteps ?? "—",
            h("span", { class: "mono" }, gitText(run)), run.label ?? ""],
        })), { onRow: (run) => go(`#/run/${run.runId}`) })),
  ];
}

function uiPage() {
  const uiRuns = byKind("ui-scenario");
  if (!uiRuns.length) return [h("h2", {}, "UI performance"), empty("No UI runs recorded.", h("span", {}, "Run ", h("code", {}, "npm run observatory:ui"), "."))];
  const scenarios = [...new Set(uiRuns.map((run) => run.ui?.scenarioId).filter(Boolean))];
  return [
    h("div", { class: "page-head" }, h("h2", {}, "UI performance"), h("span", { class: "muted small" }, `${uiRuns.length} runs`)),
    h("p", { class: "muted" }, "Action latency, not frame time: \"settled\" is the last frame the window drew after an action before rendering went quiet; \"state\" is when the expected UI state appeared. Only measured iterations count (warm-up excluded)."),
    h("div", { class: "grid two" },
      h("section", { class: "card" }, h("h3", {}, "Median settled latency"), uiTrend(uiRuns.slice(0, 60), "median")),
      h("section", { class: "card" }, h("h3", {}, "p95 settled latency"), uiTrend(uiRuns.slice(0, 60), "p95"),
        h("p", { class: "muted small" }, "p95 is only recorded with 20 or more measured actions."))),
    scenarios.map((id) => {
      const runs = uiRuns.filter((run) => run.ui?.scenarioId === id);
      const latest = runs[0];
      const slowest = [...(latest.ui.steps ?? [])].filter((step) => step.settledMaxMs != null).sort((a, b) => b.settledMaxMs - a.settledMaxMs).slice(0, 8);
      return h("section", {},
        h("h3", {}, `${latest.ui.scenarioName ?? id} · latest run `, statusPill(latest.status), " ", runLink(latest)),
        slowest.length ? table(["Step", "Method", { label: "Settled median", num: true }, { label: "Settled max", num: true }, { label: "State median", num: true }, { label: "Failed", num: true }],
          slowest.map((step) => ({ cells: [step.label, step.method ?? "—", ms(step.settledMedianMs), ms(step.settledMaxMs), ms(step.stateMedianMs), step.failed ?? "—"] })))
          : empty("This run has no measured actions."),
        h("div", { class: "small muted" }, `Runs: ${runs.length} · failed runs: ${runs.filter((run) => !OK.has(run.status)).length}`));
    }),
  ];
}

function runsPage(params) {
  const filters = Object.fromEntries(["kind", "status", "scenario", "branch", "commit", "from", "to", "q"].map((key) => [key, params.get(key) ?? ""]));
  const shown = filterRuns(state.runs, filters);
  const selected = new Set();
  const compareButton = h("button", { class: "primary", disabled: true, onclick: () => {
    const [a, b] = [...selected].sort();
    go(`#/compare?a=${a}&b=${b}`);
  } }, "Compare selected");
  const update = (key, value) => {
    const next = new URLSearchParams(params);
    if (value) next.set(key, value); else next.delete(key);
    go(`#/runs?${next}`);
  };
  const select = (key, label, values) => h("label", {}, label, h("select", { onchange: (event) => update(key, event.target.value) },
    h("option", { value: "" }, "Any"), values.map((value) => h("option", { value, selected: value === filters[key] }, value))));
  const text = (key, label, type = "text") => h("label", {}, label, h("input", { type, value: filters[key], placeholder: type === "text" ? "any" : null, onchange: (event) => update(key, event.target.value.trim()) }));
  return [
    h("div", { class: "page-head" }, h("h2", {}, "Runs"), h("span", { class: "muted small" }, `${shown.length} of ${state.runs.length}`)),
    h("div", { class: "filters" },
      select("kind", "Type", state.options.kinds ?? []),
      select("status", "Result", state.options.statuses ?? []),
      select("scenario", "Scenario", state.options.scenarios ?? []),
      text("branch", "Branch"), text("commit", "Commit"), text("from", "From", "date"), text("to", "To", "date"), text("q", "Search"),
      h("button", { onclick: () => go("#/runs") }, "Clear"), compareButton),
    shown.length ? table(["", "When", "Type", "Status", "Headline", "Git", "Label"], shown.map((run) => ({
      source: run,
      cells: [
        h("input", { type: "checkbox", title: "Select two runs to compare", onchange: (event) => {
          if (event.target.checked) selected.add(run.runId); else selected.delete(run.runId);
          compareButton.disabled = selected.size !== 2;
        } }),
        runLink(run), kindLabel(run), statusPill(run.status),
        !run.readable ? "summary can't be read" : !run.supported ? `newer format (schema ${run.schemaVersion ?? "?"}), not read`
          : run.build ? ms(run.build.totalMs) : run.ui ? `${ms(run.ui.settled?.medianMs)} median` : run.runtime ? `${run.runtime.events ?? 0} events` : "—",
        h("span", { class: "mono" }, gitText(run)), run.label ?? "",
      ],
    })), { onRow: (run) => go(`#/run/${run.runId}`) }) : empty("No runs match these filters."),
  ];
}

async function runPage(runId) {
  const data = runDetails(runId);
  const { run, summary } = data;
  const parts = [
    h("div", { class: "page-head" }, h("h2", {}, kindLabel(run), " ", statusPill(run.status)), h("span", { class: "mono muted" }, run.runId)),
  ];
  if (run.problems?.length) {
    parts.push(h("div", { class: "notice warn" }, h("strong", {}, "Incomplete or unusual record"), h("ul", {}, run.problems.map((problem) => h("li", {}, problem)))));
  }
  if (run.error) {
    parts.push(h("div", { class: "notice bad" }, run.error));
  }
  if (summary) parts.push(h("div", { class: "card actions-card" }, copyForAiButton(run.runId), exportButton(run.runId)));
  parts.push(h("div", { class: "grid cards" },
    h("div", { class: "card" }, h("h3", {}, "Run"), kv([["Started", when(run.createdAt)], ["Finished", when(run.finishedAt)], ["Label", run.label], ["Schema", run.schemaVersion]])),
    h("div", { class: "card" }, h("h3", {}, "Source"), kv([["Branch", run.source?.branch ?? "detached"], ["Commit", h("span", { class: "mono" }, run.source?.commit ?? "—")], ["Working tree", run.source?.dirty == null ? "unknown" : run.source.dirty ? "dirty" : "clean"]])),
    h("div", { class: "card" }, h("h3", {}, "Build configuration"), kv([["Build type", run.config?.buildType], ["Compiler", run.config?.compiler], ["Qt", run.config?.qtVersion], ["CMake", run.config?.cmakeVersion], ["Binary", run.config?.binarySha256 ? h("span", { class: "mono" }, run.config.binarySha256.slice(0, 16)) : null]])),
    h("div", { class: "card" }, h("h3", {}, "Machine"), kv([["CPU", run.machine?.cpuModel], ["Cores", run.machine?.logicalCpus], ["Memory", run.machine?.totalMemoryBytes ? `${Math.round(run.machine.totalMemoryBytes / 2 ** 30)} GB` : null], ["OS", [run.machine?.platform, run.machine?.osVersion, run.machine?.architecture].filter(Boolean).join(" ")]]))));

  if (run.build && summary) {
    parts.push(h("section", {}, h("h3", {}, "Phases"),
      table(["Phase", "Status", { label: "Duration", num: true }, "Details", { label: "Error", wrap: true }], (summary.phases ?? []).map((phase) => ({
        cells: [phase.name, statusPill(phase.status), ms(phase.durationMs ?? null), phaseDetails(phase), phase.error ? [phase.error.message, h("div", { class: "mono small" }, [].concat(phase.error.excerpt ?? []).slice(0, 4).join("\n"))] : ""],
      })))));
    const log = (summary.phases ?? []).find((phase) => phase.name === "native-build")?.details?.ninjaLog;
    if (log?.slowestSteps?.length) {
      parts.push(h("section", {}, h("h3", {}, "Slowest Ninja steps (measured by Ninja)"),
        table(["Output", { label: "Duration", num: true }], log.slowestSteps.map((step) => ({ cells: [h("span", { class: "mono" }, step.output), ms(step.durationMs)] })))));
    }
  }
  if (run.ui) {
    parts.push(h("section", {}, h("h3", {}, `Scenario ${run.ui.scenarioName ?? run.ui.scenarioId} v${run.ui.scenarioVersion ?? "?"}`),
      h("div", { class: "grid cards" }, ["settled", "state", "dispatch", "frameGap"].map((field) => h("div", { class: "card" },
        h("h3", {}, { settled: "Settled", state: "UI state reached", dispatch: "Dispatch", frameGap: "Longest frame gap" }[field]),
        kv([["Median", ms(run.ui[field]?.medianMs)], ["p95", run.ui[field]?.p95Ms == null ? "not enough samples" : ms(run.ui[field].p95Ms)], ["Max", ms(run.ui[field]?.maxMs)], ["Samples", run.ui[field]?.n]]))))));
    parts.push(await actionsSection(run));
    if (summary?.runtimeTiming?.recognizedEvents) {
      parts.push(h("section", {}, h("h3", {}, "archivist.wells.timing (same process)"),
        table(["Operation", "Field", { label: "Count", num: true }, { label: "Mean", num: true }, { label: "Max", num: true }],
          Object.entries(summary.runtimeTiming.summary ?? {}).map(([operation, stats]) => ({ cells: [operation, stats.field, stats.count, ms(stats.meanMs), ms(stats.maxMs)] })))));
    }
  }
  const category = categoryOf(run);
  if (category) {
    parts.push(h("p", { class: "muted small" }, `Use as the ${categoryLabel(category)} baseline (with other runs if you have them): `,
      h("code", { class: "cmd" }, `npm run observatory -- baseline set ${run.runId}`)));
  }
  if (data.artifacts.length) {
    parts.push(artifactsSection(run.runId, data.artifacts));
  }
  return parts;
}

function phaseDetails(phase) {
  const details = phase.details ?? {};
  const parts = [];
  if (typeof details.cacheReused === "boolean") parts.push(details.cacheReused ? "cache reused" : "rebuilt");
  if (details.plannedWorkSteps != null) parts.push(`${details.plannedWorkSteps} planned steps`);
  if (details.ninjaLog?.basis === "measured-by-ninja") parts.push(`${details.ninjaLog.workSteps} steps run`);
  return parts.join(" · ") || "";
}

async function actionsSection(run) {
  let events;
  try {
    events = await runEvents(run.runId);
  } catch (error) {
    return h("section", {}, h("h3", {}, "Actions"), empty(`No action records: ${error.message}`));
  }
  const body = h("div");
  let phase = "measured";
  let failedOnly = false;
  const render = () => {
    const shown = events.actions.filter((action) => (phase === "all" || action.phase === phase) && (!failedOnly || !action.ok));
    body.replaceChildren(shown.length ? table(["#", "Phase", "Iter.", "Step", "Method", "Result", { label: "Dispatch", num: true }, { label: "State", num: true }, { label: "Settled", num: true }, { label: "Frames", num: true }, { label: "Longest gap", num: true }],
      shown.slice(0, 500).map((action) => ({
        cells: [action.index, action.phase, action.iteration, action.label ?? action.op, action.method,
          action.ok ? h("span", { class: "pill good" }, "ok") : h("span", { class: "pill bad", title: action.error ?? "" }, action.timedOut ? "timeout" : "failed"),
          ms(action.dispatchMs), ms(action.stateMs), ms(action.settledMs), action.frames?.count ?? "—", ms(action.frames?.maxIntervalMs ?? null)],
      }))) : empty("No actions match."));
  };
  const phaseSelect = h("select", { onchange: (event) => { phase = event.target.value; render(); } },
    ["measured", "warmup", "setup", "teardown", "all"].map((value) => h("option", { value }, value)));
  const failedBox = h("input", { type: "checkbox", onchange: (event) => { failedOnly = event.target.checked; render(); } });
  render();
  return h("section", {}, h("h3", {}, `Actions (${events.actions.length})`),
    h("div", { class: "filters" }, h("label", {}, "Phase", phaseSelect), h("label", {}, "Failed only", failedBox)),
    events.error ? h("div", { class: "notice bad" }, events.error) : null, body);
}

function artifactsSection(runId, names) {
  const output = h("div");
  const tabs = h("div", { class: "tabs" });
  let current = names.includes("log") ? "log" : names[0];
  let query = "";
  let offset = 0;
  const load = async () => {
    for (const button of tabs.children) button.classList.toggle("active", button.dataset.name === current);
    try {
      const preview = await artifactPreview(runId, current, { offset, limit: 300, q: query });
      const lines = h("div", { class: "log" }, preview.lines.map((line) => h("div", { class: /error|FAILED|CMake Error/i.test(line.text) ? "err" : null }, h("span", { class: "n" }, line.n), line.text)));
      output.replaceChildren(
        h("div", { class: "filters" },
          h("label", {}, "Search", h("input", { type: "search", value: query, placeholder: "find lines", onchange: (event) => { query = event.target.value; offset = 0; load(); } })),
          h("button", { disabled: offset === 0, onclick: () => { offset = Math.max(0, offset - 300); load(); } }, "Previous"),
          h("button", { disabled: !preview.truncated, onclick: () => { offset += 300; load(); } }, "Next"),
          h("a", { href: artifactUrl(runId, current), target: "_blank", rel: "noopener" }, `Open full ${preview.file}`)),
        h("p", { class: "muted small" }, `${preview.file} · ${(preview.sizeBytes / 1024).toFixed(1)} KB · ${query ? `${preview.matchingLines} matching of ${preview.totalLines} lines` : `${preview.totalLines} lines`} · showing ${preview.lines.length}`),
        preview.lines.length ? lines : empty("Nothing to show."));
    } catch (error) {
      output.replaceChildren(empty(error.message));
    }
  };
  names.forEach((name) => tabs.append(h("button", { "data-name": name, onclick: () => { current = name; offset = 0; query = ""; load(); } }, name)));
  load();
  return h("section", {}, h("h3", {}, "Artifacts"), tabs, output);
}

async function comparePage(params) {
  const options = state.runs.filter((run) => run.readable && run.supported && ["build", "ui-scenario"].includes(run.kind));
  const a = params.get("a") ?? "";
  const b = params.get("b") ?? "";
  const picker = (key, label, value) => h("label", {}, label, h("select", { onchange: (event) => {
    const next = new URLSearchParams(params);
    next.set(key, event.target.value);
    go(`#/compare?${next}`);
  } }, h("option", { value: "" }, "Choose a run"), options.map((run) => h("option", { value: run.runId, selected: run.runId === value },
    `${when(run.createdAt)} · ${kindLabel(run)} · ${run.status} · ${gitText(run)}${run.label ? ` · ${run.label}` : ""}`))));
  const parts = [
    h("div", { class: "page-head" }, h("h2", {}, "Compare two runs")),
    h("div", { class: "picker card" }, picker("a", "Baseline (before)", a), picker("b", "Candidate (after)", b),
      h("button", { disabled: !a || !b, onclick: () => go(`#/compare?a=${b}&b=${a}`) }, "Swap")),
  ];
  if (!options.length) {
    parts.push(empty("No build or UI runs to compare yet."));
    return parts;
  }
  if (!a || !b) {
    parts.push(h("p", { class: "muted" }, "Pick a baseline and a candidate. Builds compare with builds; UI runs compare only with the same scenario and version."));
    return parts;
  }
  const { runs, comparison } = compare(a, b);
  const [baseline, candidate] = runs;
  parts.push(h("div", { class: "versus" }, [[baseline, "Baseline"], [candidate, "Candidate"]].map(([run, role]) => h("div", { class: "card" },
    h("h3", {}, `${role} `, statusPill(run.status ?? "unknown")),
    kv([["Run", run.readable ? runLink(run, run.runId) : run.runId], ["Type", kindLabel(run)], ["Scenario", run.ui ? `${run.ui.scenarioId} v${run.ui.scenarioVersion}` : null],
      ["Git", run.source ? h("span", { class: "mono" }, gitText(run)) : null], ["Build type", run.config?.buildType], ["Compiler", run.config?.compiler], ["Qt", run.config?.qtVersion],
      ["Build kind", run.build?.classification], ["Machine", run.machine?.cpuModel ? `${run.machine.cpuModel} · ${run.machine.logicalCpus} cores` : null], ["When", when(run.createdAt)]])))));
  if (comparison.blockers.length) {
    parts.push(h("div", { class: "notice bad" }, h("strong", {}, "These runs can't be compared"), h("ul", {}, comparison.blockers.map((blocker) => h("li", {}, blocker.message)))));
    return parts;
  }
  if (comparison.warnings.length) {
    parts.push(h("div", { class: "notice warn" }, h("strong", {}, "Different conditions: read the numbers with care"), h("ul", {}, comparison.warnings.map((warning) => h("li", {}, warning.message)))));
  }
  const { better, worse, same, missing } = comparison.totals;
  parts.push(h("div", { class: "notice" }, `${better} better · ${worse} worse · ${same} about the same (within ${SAME_WITHIN_PCT}%) · ${missing} not measured in both. `, h("span", { class: "muted" }, comparison.note)));
  parts.push(table(["Metric", { label: "Baseline", num: true }, { label: "Candidate", num: true }, { label: "Change", num: true }, "Verdict"],
    comparison.metrics.map((metric) => ({
      cells: [metric.group && !metric.label.startsWith(metric.group) ? `${metric.group}: ${metric.label}` : metric.label,
        metric.unit === "ms" ? ms(metric.baseline) : metric.baseline ?? "—",
        metric.unit === "ms" ? ms(metric.candidate) : metric.candidate ?? "—",
        verdictCell(metric),
        metric.verdict === "missing" ? h("span", { class: "pill" }, `missing in ${metric.missingIn.join(" and ")}`)
          : h("span", { class: `pill ${metric.verdict === "better" ? "good" : metric.verdict === "worse" ? "bad" : ""}` }, metric.verdict === "same" ? "same" : metric.verdict)],
    }))));
  return parts;
}

// ---- baselines and regressions --------------------------------------------------

const VERDICT_TEXT = {
  regression: ["Regression", "bad"],
  improvement: ["Improvement", "good"],
  "no-meaningful-change": ["No meaningful change", ""],
  "insufficient-evidence": ["Needs more runs", "warn"],
  incompatible: ["Incompatible", "bad"],
  "not-measured": ["Not measured", ""],
};

function verdictPill(verdict, overBudget = false) {
  const [text, tone] = VERDICT_TEXT[verdict] ?? [verdict, ""];
  return h("span", { class: `pill ${tone}` }, overBudget && verdict !== "regression" ? `${text} · over budget` : text);
}

function groupText(group, unit) {
  if (!group?.n) return "—";
  const fmt = (value) => (unit === "ms" ? ms(value) : String(value));
  return h("span", {}, fmt(group.median), h("br"), h("span", { class: "muted small" }, group.n === 1 ? "1 run" : `${group.n} runs, ${fmt(group.min)}–${fmt(group.max)}`));
}

function changeText(metric) {
  if (metric.delta == null) return h("span", { class: "muted" }, "—");
  const sign = metric.delta > 0 ? "+" : "";
  const worse = metric.direction === "lower" ? metric.delta > 0 : metric.delta < 0;
  const value = metric.unit === "ms" ? `${sign}${ms(metric.delta)}` : `${sign}${metric.delta}`;
  return h("span", { class: metric.delta === 0 ? "" : worse ? "verdict-worse" : "verdict-better" },
    `${value}${metric.deltaPercent == null ? "" : ` (${sign}${metric.deltaPercent}%)`}`);
}

function metricRows(metrics) {
  return metrics.filter((metric) => metric.verdict !== "not-measured").map((metric) => ({
    cells: [metric.label, groupText(metric.baseline, metric.unit), groupText(metric.candidate, metric.unit), changeText(metric),
      `${metric.direction === "lower" ? "lower" : "higher"} is better · ${metric.budget.regressionPercent}%${metric.budget.minAbsolute ? ` and ${metric.budget.minAbsolute} ms` : ""} · ${metric.budget.minSamples}+ runs`,
      verdictPill(metric.verdict, metric.overBudget), h("span", { class: "small" }, metric.reasons.join("; "))],
  }));
}

const METRIC_HEADERS = ["Metric", { label: "Baseline", num: true }, { label: "Latest", num: true }, { label: "Change", num: true }, { label: "Budget", wrap: true }, "Verdict", { label: "Why", wrap: true }];

function idLinks(ids) {
  return ids.map((id, index) => [index ? ", " : "", h("a", { href: `#/run/${id}`, class: "mono" }, id)]);
}

// The headline metric of a category over its recent runs, baseline runs
// drawn as their own line.
function categoryTrend(category, entry) {
  const headline = category.startsWith("build:") ? (run) => run.build?.totalMs : (run) => run.ui?.settled?.medianMs;
  const runs = chronological(state.runs.filter((run) => categoryOf(run) === category)).slice(-30);
  if (runs.length < 2) return null;
  const inBaseline = new Set(entry?.runIds ?? []);
  const point = (run, x) => ({ x, y: OK.has(run.status) ? headline(run) ?? null : null, failed: !OK.has(run.status), href: `#/run/${run.runId}`,
    title: `${when(run.createdAt)} · ${run.status}${inBaseline.has(run.runId) ? " · baseline" : ""}\n${ms(headline(run) ?? null)}\n${gitText(run)}` });
  const series = [
    { name: "Baseline runs", color: "var(--series-2)", points: runs.map(point).filter((_, x) => inBaseline.has(runs[x].runId)) },
    { name: "Other runs", color: "var(--series-1)", points: runs.map(point).filter((_, x) => !inBaseline.has(runs[x].runId)) },
  ];
  return [lineChart({ count: runs.length, format: axisMs, series }),
    legend([["Baseline runs", "var(--series-2)"], ["Other runs", "var(--series-1)"], ["Failed run (not plotted)", "var(--bad)"]]),
    h("p", { class: "muted small" }, category.startsWith("build:") ? "Total build time" : "Median settled action latency", ", oldest to newest.")];
}

async function baselinesPage() {
  const files = await loadBaselines();
  const parts = [h("div", { class: "page-head" }, h("h2", {}, "Baselines and regressions"))];
  parts.push(h("p", { class: "muted" },
    "Each workload is compared with its own baseline: the newest compatible successful runs since the baseline, against the baseline's runs. ",
    "A change counts as a regression or improvement only with enough runs on each side, past its budget, and with no overlap between the two groups' runs. ",
    "That is a rule of thumb, not a significance test. Failed runs are listed apart, as functional failures."));
  if (files.baselinesError) parts.push(h("div", { class: "notice bad" }, `Baselines: ${files.baselinesError}`));
  if (files.budgetsError) parts.push(h("div", { class: "notice bad" }, `Budgets: ${files.budgetsError}`));
  const budgets = resolveBudgets(files.budgets);
  if (budgets.problems.length) {
    parts.push(h("div", { class: "notice warn" }, h("strong", {}, "Budget problems (those entries are ignored)"), h("ul", {}, budgets.problems.map((problem) => h("li", {}, problem)))));
  }
  const report = buildReport({ runs: state.runs, baselines: files.baselines ?? { baselines: {} }, budgets, source: { budgets: files.budgetsSource } });
  const s = report.summary;
  parts.push(h("div", { class: "grid cards" },
    h("div", { class: "card" }, h("h3", {}, "Confirmed regressions"), h("div", { class: "big" }, String(s.regressions))),
    h("div", { class: "card" }, h("h3", {}, "Confirmed improvements"), h("div", { class: "big" }, String(s.improvements))),
    h("div", { class: "card" }, h("h3", {}, "Past budget, unconfirmed"), h("div", { class: "big" }, String(s.unconfirmedOverBudget))),
    h("div", { class: "card" }, h("h3", {}, "Functional failures"), h("div", { class: "big" }, String(s.functionalFailures)))));
  if (files.budgetsSource) parts.push(h("p", { class: "muted small" }, "Budgets from ", h("span", { class: "mono path" }, files.budgetsSource)));

  const entries = files.baselines?.baselines ?? {};
  for (const category of report.categories) {
    const entry = entries[category.category];
    const section = [h("h3", {}, category.label, " ", category.status === "no-baseline" ? h("span", { class: "pill" }, "No baseline") : verdictPill(category.verdict))];
    if (!entry) {
      section.push(empty(`No ${category.label} baseline.`, h("span", {}, "Pick successful runs and set them: ",
        h("code", { class: "cmd" }, "npm run observatory -- baseline set <run-id> [<run-id> ...]"))));
      parts.push(h("section", {}, section));
      continue;
    }
    const ref = entry.reference ?? {};
    section.push(h("div", { class: "grid cards" },
      h("div", { class: "card" }, h("h3", {}, "Baseline"), kv([["Runs", idLinks(entry.runIds)], ["Source", `${ref.branch ?? "detached"} @ ${shortSha(ref.commit)}${ref.dirty ? " · dirty" : ""}`],
        ["Machine", ref.machine?.cpuModel], ["Build type", ref.config?.buildType], ["Scenario", ref.scenario ? `${ref.scenario.id} v${ref.scenario.version}` : null],
        ["Set", when(entry.setAt)], ["Note", entry.note]])),
      h("div", { class: "card" }, h("h3", {}, "Compared with"), category.candidate.runIds.length
        ? kv([["Runs", idLinks(category.candidate.runIds)], ["Source", `${category.candidate.branches.join(", ") || "?"} @ ${category.candidate.commits.map(shortSha).join(", ") || "?"}`]])
        : h("p", { class: "muted" }, "No runs recorded since the baseline."))));
    if (category.reasons.length) {
      section.push(h("div", { class: `notice ${category.verdict === "incompatible" ? "bad" : "warn"}` }, h("ul", {}, category.reasons.map((reason) => h("li", {}, reason)))));
    }
    if (category.functional.failedRuns.length) {
      section.push(h("div", { class: "notice bad" }, h("strong", {}, "Functional failures (not counted as performance)"),
        h("ul", {}, category.functional.failedRuns.map((failure) => h("li", {}, h("a", { href: `#/run/${failure.runId}` }, failure.runId), ` ${failure.status}${failure.error ? `: ${failure.error}` : ""}`)))));
    }
    if (category.skipped.length) {
      section.push(h("div", { class: "notice" }, h("strong", {}, "Left out"),
        h("ul", {}, category.skipped.map((skip) => h("li", {}, h("a", { href: `#/run/${skip.runId}` }, skip.runId), ` ${skip.reason}`)))));
    }
    if (category.metrics.length) section.push(table(METRIC_HEADERS, metricRows(category.metrics)));
    if (category.steps.length) {
      section.push(h("h3", {}, "Steps, slowest first (settled median)"), table(METRIC_HEADERS, metricRows(category.steps.slice(0, 8))));
    }
    section.push(comparisonCopyButton(category));
    const trend = categoryTrend(category.category, entry);
    if (trend) section.push(h("div", { class: "card" }, trend));
    parts.push(h("section", {}, section));
  }
  return parts;
}

// ---- data and routing ---------------------------------------------------------

function go(hash) {
  if (location.hash === hash) render(); else location.hash = hash;
}

async function render() {
  const [path, query = ""] = location.hash.replace(/^#/, "").split("?");
  const params = new URLSearchParams(query);
  const section = path.split("/")[1] ?? "";
  for (const link of document.querySelectorAll("#nav a")) {
    const target = link.getAttribute("href").replace(/^#\//, "");
    link.classList.toggle("active", target === section || (section === "run" && target === "runs"));
  }
  let content;
  try {
    if (state.error) content = empty(`Can't read Observatory runs: ${state.error}`);
    else if (section === "builds") content = buildsPage();
    else if (section === "ui") content = uiPage();
    else if (section === "runs") content = runsPage(params);
    else if (section === "run") content = await runPage(path.split("/")[2] ?? "");
    else if (section === "compare") content = await comparePage(params);
    else if (section === "baselines") content = await baselinesPage();
    else if (section === "lab") content = await labPage();
    else content = overview();
  } catch (error) {
    content = empty(`Something went wrong: ${error.message}`);
  }
  view.replaceChildren(...[content].flat(Infinity).filter(Boolean));
}

async function refresh() {
  state.error = null;
  try {
    const data = await loadRuns();
    state.runs = data.runs;
    state.options = data.options;
    state.root = data.root;
    document.getElementById("root-path").textContent = `Reading ${data.root}`;
  } catch (error) {
    state.error = error.message;
  }
  render();
}

function start() {
  initControl();
  configureLab({ getState: () => state, refresh });
  document.getElementById("refresh").addEventListener("click", refresh);
  window.addEventListener("hashchange", render);
  refresh();
}

start();
