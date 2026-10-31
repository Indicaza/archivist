// Small DOM and formatting helpers shared by the dashboard's pages. All text
// is set as text nodes; nothing is parsed as HTML.

export const OK = new Set(["success", "passed"]);

export function h(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [name, value] of Object.entries(props ?? {})) {
    if (value == null || value === false) continue;
    if (name === "class") node.className = value;
    else if (name.startsWith("on")) node.addEventListener(name.slice(2), value);
    // Through the CSSOM: the page's CSP blocks style attributes.
    else if (name === "style") node.style.cssText = value;
    else node.setAttribute(name, value === true ? "" : String(value));
  }
  for (const child of children.flat(Infinity)) {
    if (child == null || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

export function ms(value) {
  if (value == null) return "—";
  if (Math.abs(value) >= 1000) return `${(value / 1000).toFixed(Math.abs(value) >= 10000 ? 1 : 2)} s`;
  return `${value.toFixed(Math.abs(value) >= 100 ? 0 : 1)} ms`;
}

export function axisMs(value) {
  return value >= 1000 ? `${(value / 1000).toFixed(value >= 10000 ? 0 : 1)} s` : `${Math.round(value)} ms`;
}

export function when(iso) {
  if (!iso) return "—";
  const date = new Date(iso);
  return date.toLocaleString(undefined, { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
}

export function shortSha(sha) {
  return sha ? sha.slice(0, 9) : "—";
}

const BAD = new Set(["failure", "failed", "crashed", "error", "unreadable", "refused", "timeout", "timed-out", "interrupted"]);

export function statusPill(status) {
  const tone = OK.has(status) || status === "completed" ? "good" : BAD.has(status) ? "bad" : "warn";
  return h("span", { class: `pill ${tone}` }, status);
}

export function kindLabel(run) {
  if (run.kind === "build") return "Build";
  if (run.kind === "ui-scenario") return run.ui?.scenarioName ?? "UI scenario";
  if (run.kind === "runtime-log") return "Runtime log";
  return run.kind;
}

export function gitText(run) {
  const branch = run.source?.branch ?? "detached";
  return `${branch} @ ${shortSha(run.source?.commit)}${run.source?.dirty ? " · dirty" : ""}`;
}

export function runLink(run, text) {
  return h("a", { href: `#/run/${run.runId}` }, text ?? when(run.createdAt));
}

export function empty(message, hint) {
  return h("div", { class: "empty" }, h("p", {}, message), hint ? h("p", {}, hint) : null);
}

export function kv(pairs) {
  return h("dl", { class: "kv" }, pairs.filter(([, value]) => value != null && value !== "").map(([key, value]) => [h("dt", {}, key), h("dd", {}, value)]));
}

export function table(headers, rows, { onRow } = {}) {
  return h("div", { class: "table-wrap" }, h("table", {},
    h("thead", {}, h("tr", {}, headers.map((header) => h("th", { class: header.num ? "num" : null }, header.label ?? header)))),
    h("tbody", {}, rows.map((row) => h("tr", {
      class: onRow ? "clickable" : null,
      onclick: onRow ? (event) => { if (!event.target.closest("a, input, button")) onRow(row.source); } : null,
    }, row.cells.map((cell, index) => h("td", { class: headers[index]?.num ? "num" : headers[index]?.wrap ? "wrap" : null }, cell)))))));
}

export function legend(items) {
  return h("div", { class: "legend" }, items.map(([name, color]) => h("span", {}, h("i", { style: `background:${color}` }), name)));
}

