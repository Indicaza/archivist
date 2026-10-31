// Archivist Observatory. Three places: Record (make a recording or run a
// scripted test), History (everything recorded, with trends) and Compare
// (two runs side by side, and the regression check against baselines).
// Reads runs from the host and renders them; all text is set as text.
import { lineChart, pairedBars, stackedBars } from "./charts.js";
import { SAME_WITHIN_PCT } from "./compare.mjs";
import { artifactPreview, artifactUrl, compare, hasControlKey, initControl, liveArtifacts, loadBaselines, loadRuns, runDetails, runEvents } from "./data.js";
import { buildReport, categoryLabel, categoryOf, resolveBudgets } from "./regression.mjs";
import { filterRuns } from "./filter.mjs";
import { axisMs, empty, gitText, h, kindLabel, kv, legend, ms, OK, shortSha, statusPill, table, when } from "./dom.js";
import { comparisonCopyButton, configureLab, copyForAiButton, exportButton, testPanel } from "./lab.js";
import { configureLive, recorderHero, recordingView } from "./live-page.js";
import { auditComparison, auditHero, auditView, configureAudit } from "./audit-page.js";
import { bytes, compareLive, liveModel } from "./live.mjs";

const view = document.getElementById("view");
const state = { runs: [], options: {}, root: "", error: null, listing: null };

const PHASES = [
  ["ide-bundle", "IDE bundle", "var(--series-3)"],
  ["configure", "Configure", "var(--series-2)"],
  ["build-plan", "Build plan", "var(--series-5)"],
  ["native-build", "Native build", "var(--series-1)"],
];
const SCENARIO_COLORS = ["var(--series-1)", "var(--series-2)", "var(--series-3)", "var(--series-4)", "var(--series-5)"];

// What each kind of run is called and shows at a glance.
const KINDS = {
  audit: { name: "Audit", plural: "Audits", glyph: "A" },
  "live-session": { name: "Recording", plural: "Recordings", glyph: "●" },
  build: { name: "Build", plural: "Builds", glyph: "B" },
  "ui-scenario": { name: "UI test", plural: "UI tests", glyph: "T" },
  "runtime-log": { name: "Runtime log", plural: "Runtime logs", glyph: "L" },
};
const COMPARABLE = new Set(["audit", "live-session", "build", "ui-scenario"]);

function chronological(runs) {
  return [...runs].sort((a, b) => a.runId.localeCompare(b.runId));
}

function byKind(kind) {
  return state.runs.filter((run) => run.kind === kind && run.readable && run.supported);
}

// The latest earlier run that compares with `run` (same kind; same scenario and version for UI tests).
function previousComparable(run) {
  return state.runs.find((other) => other.runId < run.runId && other.kind === run.kind && other.readable && other.supported
    && (OK.has(other.status) || other.status === "completed")
    && (run.kind !== "ui-scenario" || (other.ui?.scenarioId === run.ui?.scenarioId && other.ui?.scenarioVersion === run.ui?.scenarioVersion))
    && (run.kind !== "audit" || other.audit?.preset === run.audit?.preset)) ?? null;
}

function title(run) {
  if (run.kind === "audit") return run.label ?? "Audit";
  if (run.kind === "live-session") return run.label ?? "Recording";
  if (run.kind === "ui-scenario") return run.ui?.scenarioName ?? "UI test";
  if (run.kind === "build") return run.label ?? `${run.build?.classification ?? ""} build`.trim();
  return run.label ?? kindLabel(run);
}

// The one number a run is known by.
function figure(run) {
  if (!run.readable) return { value: "—", sub: "can't be read" };
  if (!run.supported) return { value: "—", sub: "newer format" };
  if (run.audit) return { value: String(run.audit.counts.total), sub: run.audit.counts.high ? `${run.audit.counts.high} high` : run.audit.counts.total === 1 ? "finding" : "findings" };
  if (run.build) return { value: ms(run.build.totalMs), sub: "total" };
  if (run.ui) return { value: ms(run.ui.settled?.medianMs), sub: "median" };
  if (run.live) return { value: ms(run.live.worstMs ?? null), sub: "worst delay" };
  if (run.runtime) return { value: String(run.runtime.events ?? 0), sub: "events" };
  return { value: "—", sub: "" };
}

function runCard(run, { selectable = false, selected = null } = {}) {
  const kind = KINDS[run.kind] ?? { name: run.kind, glyph: "?" };
  const { value, sub } = figure(run);
  const card = h("a", { class: `run-card k-${run.kind}`, href: `#/run/${run.runId}` },
    h("span", { class: "run-glyph", "aria-hidden": "true" }, kind.glyph),
    h("span", { class: "run-body" },
      h("span", { class: "run-title" }, title(run)),
      h("span", { class: "run-meta" }, [kind.name, when(run.createdAt), run.source?.commit ? shortSha(run.source.commit) : null].filter(Boolean).join(" · "))),
    h("span", { class: "run-figure" }, h("strong", {}, value), h("span", {}, sub)),
    OK.has(run.status) || run.status === "completed" ? null : statusPill(run.status));
  if (!selectable) return card;
  const box = h("input", { type: "checkbox", "aria-label": `Select ${title(run)} to compare`, checked: selected?.has(run.runId) || null });
  box.addEventListener("change", () => selected.toggle(run.runId, box.checked));
  return h("div", { class: "run-row" }, h("label", { class: "pick" }, box), card);
}

function pageHead(heading, ...rest) {
  return h("div", { class: "page-head" }, h("h2", {}, heading), ...rest);
}

// ---- Record -------------------------------------------------------------------

async function recordPage(params) {
  const recent = state.runs.filter((run) => run.readable).slice(0, 6);
  return h("div", { class: "record-layout" },
    h("div", { class: "record-main" }, auditHero(), recorderHero(), await testPanel({ open: params.get("test") === "1" })),
    h("aside", { class: "record-side" },
      h("div", { class: "side-head" }, h("h3", {}, "Latest"), state.runs.length ? h("a", { href: "#/history" }, "All history") : null),
      recent.length ? h("div", { class: "run-list" }, recent.map((run) => runCard(run)))
        : h("p", { class: "muted small" }, "Nothing recorded yet. Your first recording shows up here.")));
}

// ---- History ------------------------------------------------------------------

const PAGE = 60;

function historyPage(params) {
  const type = params.get("type") ?? "all";
  const q = params.get("q") ?? "";
  const shownCount = Number(params.get("n")) || PAGE;
  const present = new Set(state.runs.map((run) => run.kind));
  const link = (changes) => {
    const next = new URLSearchParams(params);
    for (const [key, value] of Object.entries(changes)) if (value) next.set(key, value); else next.delete(key);
    return `#/history?${next}`;
  };
  const chips = h("div", { class: "chips", role: "tablist", "aria-label": "Show" },
    [["all", "All"], ...Object.entries(KINDS).filter(([kind]) => present.has(kind)).map(([kind, info]) => [kind, info.plural])]
      .map(([value, label]) => h("a", { class: `chip${type === value ? " on" : ""}`, href: link({ type: value === "all" ? null : value, n: null }), role: "tab", "aria-selected": type === value ? "true" : "false" }, label)));
  const search = h("input", { type: "search", value: q, placeholder: "Search name, branch, commit", "aria-label": "Search runs", class: "search" });
  search.addEventListener("change", () => go(link({ q: search.value.trim() || null, n: null })));

  const runs = filterRuns(state.runs, { kind: type === "all" ? "" : type, q });
  const picked = new Set();
  const compareBar = h("div", { class: "compare-bar", hidden: true });
  const selected = {
    has: (id) => picked.has(id),
    toggle: (id, on) => {
      if (on) picked.add(id); else picked.delete(id);
      const ids = [...picked].sort();
      const kinds = new Set(ids.map((id2) => state.runs.find((run) => run.runId === id2)?.kind));
      compareBar.hidden = ids.length === 0;
      compareBar.replaceChildren(
        h("span", {}, ids.length === 1 ? "Pick one more to compare" : ids.length === 2 ? (kinds.size === 1 ? "Ready to compare" : "Pick two of the same kind") : "Pick just two"),
        h("button", { class: "primary", type: "button", disabled: !(ids.length === 2 && kinds.size === 1) || null, onclick: () => go(`#/compare?a=${ids[0]}&b=${ids[1]}`) }, "Compare"),
        h("button", { type: "button", onclick: () => { picked.clear(); render(); } }, "Clear"));
    },
  };

  const parts = [pageHead("History", h("span", { class: "muted small" }, `${runs.length} of ${state.runs.length}`)),
    h("div", { class: "toolbar" }, chips, search)];
  if (state.listing?.omitted) {
    parts.push(h("p", { class: "muted small" }, `Showing ${(state.listing.total - state.listing.omitted).toLocaleString()} of ${state.listing.total.toLocaleString()} runs: the newest, and those baselines use. The CLI reads all of them.`));
  }
  const charts = trends(type);
  if (charts.length) parts.push(h("div", { class: "charts" }, charts));
  if (!state.runs.length) {
    parts.push(empty("Nothing recorded yet.", h("span", {}, "Make a recording on ", h("a", { href: "#/record" }, "Record"), ", or run ", h("code", {}, "npm run observatory:build"), ".")));
    return parts;
  }
  parts.push(h("h3", { class: "list-head" }, "Runs ", h("span", { class: "muted small" }, "tick two to compare")),
    runs.length ? h("div", { class: "run-list" }, runs.slice(0, shownCount).map((run) => runCard(run, { selectable: COMPARABLE.has(run.kind), selected }))) : empty("Nothing matches."),
    runs.length > shownCount ? h("p", {}, h("a", { class: "more", href: link({ n: String(shownCount + PAGE) }) }, `Show ${Math.min(PAGE, runs.length - shownCount)} more`)) : null,
    compareBar);
  return parts;
}

function chartCard(heading, body, note) {
  return h("section", { class: "card chart-card" }, h("h3", {}, heading), body, note ? h("p", { class: "muted small" }, note) : null);
}

function trends(type) {
  const cards = [];
  const builds = byKind("build");
  const uiRuns = byKind("ui-scenario");
  const recordings = byKind("live-session");
  if ((type === "all" || type === "live-session") && recordings.length >= 2) {
    cards.push(chartCard("Recordings: worst UI-thread delay", liveTrend(recordings.slice(0, 40)), "Lower is better. One recording each: what you did matters as much as the code."));
  }
  if ((type === "all" || type === "build") && builds.length) cards.push(chartCard("Build time", buildTrend(builds.slice(0, 30))));
  if (type === "build" && builds.length) {
    const recent = chronological(builds.slice(0, 30));
    cards.push(chartCard("Phases per build", [stackedBars({
      format: axisMs,
      bars: recent.map((run) => ({
        label: when(run.createdAt).split(",")[0],
        title: `${when(run.createdAt)} · ${run.status} · ${run.build?.classification ?? "?"}\n${gitText(run)}`,
        href: `#/run/${run.runId}`,
        failed: !OK.has(run.status),
        segments: PHASES.map(([key, name, color]) => ({ name, color, value: run.build?.phases?.[key] ?? null })),
      })),
    }), legend([...PHASES.map(([, name, color]) => [name, color]), ["Failed", "var(--bad)"]])]));
  }
  if ((type === "all" || type === "ui-scenario") && uiRuns.length) cards.push(chartCard("UI tests: median settled latency", uiTrend(uiRuns.slice(0, 60), "median")));
  if (type === "ui-scenario" && uiRuns.length) cards.push(chartCard("UI tests: p95 settled latency", uiTrend(uiRuns.slice(0, 60), "p95"), "p95 needs 20 or more measured actions."));
  return cards;
}

function liveTrend(recordings) {
  const runs = chronological(recordings);
  return [lineChart({
    count: runs.length,
    format: axisMs,
    series: [{
      name: "Worst delay",
      color: "var(--series-5)",
      points: runs.map((run, index) => ({ x: index, y: run.live?.worstMs ?? null, title: `${title(run)} · ${when(run.createdAt)}\nworst ${ms(run.live?.worstMs ?? null)} · ${run.live?.stalls ?? 0} stalls`, href: `#/run/${run.runId}` })),
    }],
  })];
}

// ---- Compare --------------------------------------------------------------------

// Which way is better for a live recording's headline figures.
const LIVE_LOWER_IS_BETTER = new Set(["busiestWindowP50Ms", "busiestWindowP95Ms", "frameGapsPerMinute", "stallsPerMinute", "maxEventLoopDelayMs", "cpuMeanPercent", "rssGrowthBytesPerMinute"]);
const LIVE_FORMAT = {
  durationMs: ms, busiestWindowP50Ms: ms, busiestWindowP95Ms: ms, maxEventLoopDelayMs: ms,
  frames: (value) => String(value), frameGapsPerMinute: (value) => `${value}/min`, stallsPerMinute: (value) => `${value}/min`,
  cpuMeanPercent: (value) => `${value}%`, rssGrowthBytesPerMinute: (value) => `${bytes(value)}/min`,
};

function changeLabel(delta, pct, format) {
  if (delta == null) return "";
  const sign = delta > 0 ? "+" : delta < 0 ? "−" : "±";
  return `${sign}${format(Math.abs(delta))}${pct == null ? "" : ` (${sign}${Math.abs(pct)}%)`}`;
}

function liveRows(comparison) {
  return comparison.metrics.filter((metric) => metric.base != null || metric.candidate != null).map((metric) => {
    const format = LIVE_FORMAT[metric.metric] ?? String;
    const pct = metric.delta != null && metric.base ? Math.round((metric.delta / metric.base) * 1000) / 10 : null;
    let verdict = null;
    if (LIVE_LOWER_IS_BETTER.has(metric.metric) && metric.delta != null) {
      verdict = pct != null && Math.abs(pct) < SAME_WITHIN_PCT ? "same" : metric.delta < 0 ? "better" : metric.delta > 0 ? "worse" : "same";
    }
    return { label: metric.label, a: metric.base, b: metric.candidate, format, verdict, change: changeLabel(metric.delta, pct, format) };
  });
}

function runRows(comparison) {
  return comparison.metrics.filter((metric) => metric.verdict !== "missing").map((metric) => {
    const format = metric.unit === "ms" ? ms : (value) => `${value} ${metric.unit}`;
    return {
      label: metric.group && !metric.label.startsWith(metric.group) && metric.group !== "Phases" ? `${metric.group}: ${metric.label}` : metric.label,
      a: metric.baseline, b: metric.candidate, format, verdict: metric.verdict, change: changeLabel(metric.delta, metric.deltaPct, format),
    };
  });
}

function verdictSummary(rows) {
  const count = (verdict) => rows.filter((row) => row.verdict === verdict).length;
  const better = count("better");
  const worse = count("worse");
  const same = count("same");
  return h("div", { class: "verdicts" },
    h("span", { class: "v better" }, h("strong", {}, String(better)), " better"),
    h("span", { class: "v worse" }, h("strong", {}, String(worse)), " worse"),
    h("span", { class: "v same" }, h("strong", {}, String(same)), " about the same"));
}

function runOption(run, value) {
  return h("option", { value: run.runId, selected: run.runId === value || null }, `${title(run)} · ${when(run.createdAt)}${run.source?.commit ? ` · ${shortSha(run.source.commit)}` : ""}`);
}

function picker(label, value, runs, onPick) {
  const groups = Object.entries(KINDS).filter(([kind]) => COMPARABLE.has(kind)).map(([kind, info]) => [info.plural, runs.filter((run) => run.kind === kind)]).filter(([, list]) => list.length);
  return h("label", { class: "pick-run" }, h("span", {}, label),
    h("select", { onchange: (event) => onPick(event.target.value) }, h("option", { value: "" }, "Choose…"),
      groups.map(([name, list]) => h("optgroup", { label: name }, list.slice(0, 300).map((run) => runOption(run, value))))));
}

async function comparePage(params) {
  const a = params.get("a") ?? "";
  const b = params.get("b") ?? "";
  const options = state.runs.filter((run) => run.readable && run.supported && COMPARABLE.has(run.kind));
  const runA = options.find((run) => run.runId === a);
  const set = (key, value) => {
    const next = new URLSearchParams(params);
    if (value) next.set(key, value); else next.delete(key);
    if (key === "a" && value && next.get("b")) {
      const kind = options.find((run) => run.runId === value)?.kind;
      if (options.find((run) => run.runId === next.get("b"))?.kind !== kind) next.delete("b");
    }
    go(`#/compare?${next}`);
  };
  const parts = [pageHead("Compare"),
    h("div", { class: "pickers card" },
      picker("Before", a, options, (value) => set("a", value)),
      h("button", { class: "icon-btn swap", type: "button", title: "Swap before and after", "aria-label": "Swap", disabled: !(a && b) || null, onclick: () => go(`#/compare?a=${b}&b=${a}`) }, "⇄"),
      picker("After", b, runA ? options.filter((run) => run.kind === runA.kind && run.runId !== a) : options, (value) => set("b", value)))];

  if (!options.length) {
    parts.push(empty("Nothing to compare yet.", "Make two recordings of the same thing, or record two builds or UI tests."));
  } else if (!a || !b) {
    const latest = options[0];
    const previous = latest && previousComparable(latest);
    parts.push(h("p", { class: "muted" }, "Pick a before and an after of the same kind: two recordings of the same interaction, two builds, or two rounds of the same UI test."),
      previous ? h("p", {}, h("a", { class: "button primary", href: `#/compare?a=${previous.runId}&b=${latest.runId}` }, `Compare the latest ${KINDS[latest.kind].name.toLowerCase()} with the one before`)) : null);
  } else {
    parts.push(await comparison(a, b));
  }

  const check = h("details", { class: "card fold", id: "regression-check", open: params.get("check") === "1" || null },
    h("summary", {}, h("span", { class: "fold-title" }, "Regression check"), h("span", { class: "muted small" }, "Builds and UI tests against their baselines, with budgets")));
  check.addEventListener("toggle", async () => {
    if (check.open && check.children.length === 1) check.append(...[await regressionCheck()].flat(Infinity).filter(Boolean));
  }, { once: false });
  if (check.open) check.append(...[await regressionCheck()].flat(Infinity).filter(Boolean));
  parts.push(check);
  return parts;
}

function sideBySide(base, candidate) {
  const side = (run, role) => h("a", { class: `side ${role}`, href: `#/run/${run.runId}` },
    h("span", { class: "side-role" }, role === "a" ? "Before" : "After"),
    h("strong", {}, title(run)),
    h("span", { class: "muted small" }, [when(run.createdAt), gitText(run)].join(" · ")));
  return h("div", { class: "sides" }, side(base, "a"), side(candidate, "b"));
}

async function comparison(a, b) {
  const base = state.runs.find((run) => run.runId === a);
  const candidate = state.runs.find((run) => run.runId === b);
  if (!base || !candidate) return empty("One of these runs isn't in the history any more.");
  const parts = [sideBySide(base, candidate)];
  if (base.kind !== candidate.kind) {
    parts.push(h("div", { class: "notice bad" }, `A ${KINDS[base.kind]?.name ?? base.kind} can't be compared with a ${KINDS[candidate.kind]?.name ?? candidate.kind}.`));
    return parts;
  }
  if (base.kind === "audit") {
    parts.push(...await auditComparison(base, candidate));
    return parts;
  }
  let rows;
  let blockers;
  let warnings;
  let notes;
  let full = null;
  if (base.kind === "live-session") {
    const [left, right] = await Promise.all([a, b].map(async (id) => liveModel({ runId: id, summary: runDetails(id).summary, ...(await liveArtifacts(id)) })));
    const result = compareLive(left, right);
    blockers = result.blockers;
    warnings = result.warnings;
    notes = result.notes;
    rows = liveRows(result);
  } else {
    const { comparison: result } = compare(a, b);
    blockers = result.blockers.map((entry) => entry.message);
    warnings = result.warnings.map((entry) => entry.message);
    notes = [result.note];
    rows = runRows(result);
    full = result;
  }
  if (blockers.length) {
    parts.push(h("div", { class: "notice bad" }, h("strong", {}, "These can't be compared"), h("ul", {}, blockers.map((line) => h("li", {}, line)))));
    return parts;
  }
  const headline = rows.slice(0, 10);
  parts.push(h("section", { class: "card" }, verdictSummary(rows), pairedBars({ rows: headline, names: ["Before", "After"] }),
    rows.length > headline.length ? h("details", { class: "fold plain" }, h("summary", {}, `${rows.length - headline.length} more measures`), pairedBars({ rows: rows.slice(headline.length), names: ["Before", "After"] })) : null));
  if (warnings.length) parts.push(h("div", { class: "notice warn" }, h("strong", {}, "Read with care"), h("ul", {}, warnings.map((line) => h("li", {}, line)))));
  parts.push(h("ul", { class: "notes muted small" }, notes.map((note) => h("li", {}, note)), full ? h("li", {}, `Within ${SAME_WITHIN_PCT}% counts as about the same.`) : null));
  return parts;
}

// ---- One run --------------------------------------------------------------------

async function runPage(runId) {
  const { run, summary } = runDetails(runId);
  const kind = KINDS[run.kind] ?? { name: run.kind };
  const previous = summary && COMPARABLE.has(run.kind) ? previousComparable(run) : null;
  const parts = [
    h("div", { class: "run-head" },
      h("a", { class: "back", href: run.kind === "live-session" ? "#/record" : "#/history" }, "‹ ", run.kind === "live-session" ? "Record" : "History"),
      h("div", { class: "run-title-row" }, h("h2", {}, title(run)), statusPill(run.status)),
      h("p", { class: "muted small" }, [kind.name, when(run.createdAt), gitText(run)].join(" · "))),
  ];
  if (summary && run.kind !== "audit") {
    parts.push(h("div", { class: "action-bar" },
      previous ? h("a", { class: "button", href: `#/compare?a=${previous.runId}&b=${run.runId}` }, "Compare with previous") : null,
      COMPARABLE.has(run.kind) ? h("a", { class: "button", href: `#/compare?a=${run.runId}` }, "Compare with…") : null,
      h("div", { class: "action-ai" }, copyForAiButton(run.runId), exportButton(run.runId))));
  }
  if (run.problems?.length) parts.push(h("div", { class: "notice warn" }, h("strong", {}, "Incomplete or unusual record"), h("ul", {}, run.problems.map((problem) => h("li", {}, problem)))));
  if (run.error) parts.push(h("div", { class: "notice bad" }, run.error));
  if (run.kind === "live-session") {
    parts.push(await recordingView(runId));
    return parts;
  }
  if (run.kind === "audit" && summary && run.audit) {
    parts.push(...await auditView(run, summary));
    return parts;
  }
  parts.push(...await runDetail(run, summary));
  return parts;
}

async function runDetail(run, summary) {
  const parts = [];
  const data = runDetails(run.runId);
  if (run.build) {
    parts.push(h("div", { class: "tiles" },
      tileOf("Total", ms(run.build.totalMs), run.build.classification ?? ""),
      ...PHASES.map(([key, name]) => tileOf(name, ms(run.build.phases?.[key] ?? null), "")),
      tileOf("Ninja steps", run.build.ninjaWorkSteps ?? "—", run.build.ideCacheReused == null ? "" : run.build.ideCacheReused ? "IDE cached" : "IDE rebuilt")));
  }
  if (run.ui) {
    parts.push(h("div", { class: "tiles" }, ["settled", "state", "dispatch", "frameGap"].map((field) => tileOf(
      { settled: "Settled", state: "UI state reached", dispatch: "Dispatch", frameGap: "Longest frame gap" }[field],
      ms(run.ui[field]?.medianMs), `median · p95 ${run.ui[field]?.p95Ms == null ? "n/a" : ms(run.ui[field].p95Ms)} · max ${ms(run.ui[field]?.maxMs)}`))));
  }
  if (run.build && summary) {
    parts.push(h("details", { class: "fold plain" }, h("summary", {}, "Phases in detail"),
      table(["Phase", "Status", { label: "Duration", num: true }, "Details", { label: "Error", wrap: true }], (summary.phases ?? []).map((phase) => ({
        cells: [phase.name, statusPill(phase.status), ms(phase.durationMs ?? null), phaseDetails(phase), phase.error ? [phase.error.message, h("div", { class: "mono small" }, [].concat(phase.error.excerpt ?? []).slice(0, 4).join("\n"))] : ""],
      })))));
    const log = (summary.phases ?? []).find((phase) => phase.name === "native-build")?.details?.ninjaLog;
    if (log?.slowestSteps?.length) {
      parts.push(h("details", { class: "fold plain" }, h("summary", {}, "Slowest Ninja steps"),
        table(["Output", { label: "Duration", num: true }], log.slowestSteps.map((step) => ({ cells: [h("span", { class: "mono" }, step.output), ms(step.durationMs)] })))));
    }
  }
  if (run.ui) {
    parts.push(await actionsSection(run));
    if (summary?.runtimeTiming?.recognizedEvents) {
      parts.push(h("details", { class: "fold plain" }, h("summary", {}, "archivist.wells.timing (same process)"),
        table(["Operation", "Field", { label: "Count", num: true }, { label: "Mean", num: true }, { label: "Max", num: true }],
          Object.entries(summary.runtimeTiming.summary ?? {}).map(([operation, stats]) => ({ cells: [operation, stats.field, stats.count, ms(stats.meanMs), ms(stats.maxMs)] })))));
    }
  }
  const category = categoryOf(run);
  if (category) {
    parts.push(h("p", { class: "muted small" }, `Use as the ${categoryLabel(category)} baseline (with other runs if you have them): `,
      h("code", { class: "cmd" }, `npm run observatory -- baseline set ${run.runId}`)));
  }
  parts.push(h("details", { class: "fold plain" }, h("summary", {}, "About this run"), kv([
    ["Started", when(run.createdAt)], ["Finished", when(run.finishedAt)], ["Branch", run.source?.branch ?? "detached"],
    ["Commit", run.source?.commit ? h("span", { class: "mono" }, run.source.commit) : null], ["Working tree", run.source?.dirty == null ? null : run.source.dirty ? "uncommitted changes" : "clean"],
    ["Build type", run.config?.buildType], ["Compiler", run.config?.compiler], ["Qt", run.config?.qtVersion],
    ["Machine", [run.machine?.cpuModel, run.machine?.logicalCpus && `${run.machine.logicalCpus} cores`, [run.machine?.platform, run.machine?.osVersion].filter(Boolean).join(" ")].filter(Boolean).join(" · ") || null],
    ["Run id", h("span", { class: "mono" }, run.runId)]])));
  if (data.artifacts.length) parts.push(artifactsSection(run.runId, data.artifacts));
  return parts;
}

function tileOf(label, value, sub) {
  return h("div", { class: "tile" }, h("span", { class: "tile-label" }, label), h("span", { class: "tile-value" }, value), sub ? h("span", { class: "tile-sub" }, sub) : null);
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
    return h("p", { class: "muted small" }, `No action records: ${error.message}`);
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
  return h("details", { class: "fold plain" }, h("summary", {}, `Every action (${events.actions.length})`),
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
  return h("details", { class: "fold plain" }, h("summary", {}, "Logs and files"), tabs, output);
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

async function regressionCheck() {
  const files = await loadBaselines();
  const parts = [];
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

// ---- data and routing ---------------------------------------------------------

function go(hash) {
  if (location.hash === hash) render(); else location.hash = hash;
}

// Old addresses still land somewhere sensible.
const LEGACY = { "": "record", lab: "record?test=1", live: "record", builds: "history?type=build", ui: "history?type=ui-scenario", runs: "history", baselines: "compare?check=1" };

let renderSeq = 0;

async function render() {
  // A control link can also arrive without a reload (the panel re-opens it).
  if (location.hash.startsWith("#control=")) {
    initControl();
    document.body.dataset.controls = hasControlKey() ? "on" : "off";
  }
  const mine = ++renderSeq;
  let [path, query = ""] = location.hash.replace(/^#/, "").split("?");
  let section = path.split("/")[1] ?? "";
  const id = path.split("/")[2] ?? "";
  if (section === "live" && id) {
    history.replaceState(null, "", `#/run/${id}`);
    return render();
  }
  if (Object.hasOwn(LEGACY, section)) {
    history.replaceState(null, "", `#/${LEGACY[section]}${query && !LEGACY[section].includes("?") ? `?${query}` : ""}`);
    [path, query = ""] = location.hash.replace(/^#/, "").split("?");
    section = path.split("/")[1] ?? "";
  }
  const params = new URLSearchParams(query);
  const tab = section === "run" ? (state.runs.find((run) => run.runId === id)?.kind === "live-session" ? "record" : "history") : section;
  for (const link of document.querySelectorAll("#nav a")) {
    const on = link.dataset.tab === tab;
    link.classList.toggle("active", on);
    if (on) link.setAttribute("aria-current", "page"); else link.removeAttribute("aria-current");
  }
  document.body.dataset.page = section;
  let content;
  try {
    if (state.error) content = empty(`Can't read Observatory runs: ${state.error}`);
    else if (section === "history") content = historyPage(params);
    else if (section === "compare") content = await comparePage(params);
    else if (section === "run") content = await runPage(id);
    else content = await recordPage(params);
  } catch (error) {
    content = empty(`Something went wrong: ${error.message}`);
  }
  // A slower, older render must not replace a newer page.
  if (mine !== renderSeq) return;
  view.replaceChildren(...[content].flat(Infinity).filter(Boolean));
  if (section !== "record") window.scrollTo(0, 0);
}

function applyLoaded(data) {
  state.runs = data.runs;
  state.options = data.options;
  state.root = data.root;
  state.listing = data.listing;
  document.getElementById("root-path").textContent = `Reading ${data.root}`;
}

// Read the runs again without re-rendering (the caller navigates next).
async function loadOnly() {
  try {
    applyLoaded(await loadRuns());
  } catch (error) {
    state.error = error.message;
  }
}

async function refresh() {
  state.error = null;
  await loadOnly();
  render();
}

// The top bar's Recording chip: shown on every page while a recording runs.
function showStatus(status) {
  const chip = document.getElementById("live-chip");
  const busy = status?.recording === true;
  chip.hidden = !busy;
  if (busy) chip.querySelector(".chip-text").textContent = `Recording ${Math.floor((status.elapsedMs ?? 0) / 1000)} s`;
}

function start() {
  initControl();
  document.body.dataset.controls = hasControlKey() ? "on" : "off";
  configureLab({ getState: () => state, refresh });
  configureLive({ getState: () => state, refresh: loadOnly, go, onStatus: showStatus });
  configureAudit({ getState: () => state, refresh: loadOnly, go });
  document.getElementById("refresh").addEventListener("click", refresh);
  window.addEventListener("hashchange", render);
  refresh();
}

start();
