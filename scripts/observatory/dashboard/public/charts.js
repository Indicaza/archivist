// Small SVG charts. Text goes in through textContent only; values that are
// null are left out (never drawn as zero).
const NS = "http://www.w3.org/2000/svg";

function el(tag, attrs = {}, text) {
  const node = document.createElementNS(NS, tag);
  for (const [name, value] of Object.entries(attrs)) {
    node.setAttribute(name, String(value));
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function niceMax(value) {
  if (!(value > 0)) return 1;
  const power = 10 ** Math.floor(Math.log10(value));
  for (const step of [1, 2, 2.5, 5, 10]) {
    if (value <= step * power) return step * power;
  }
  return 10 * power;
}

function frame(width, height, max, format) {
  const pad = { left: 56, right: 12, top: 12, bottom: 26 };
  const svg = el("svg", { viewBox: `0 0 ${width} ${height}`, class: "chart", role: "img" });
  const plotH = height - pad.top - pad.bottom;
  for (let i = 0; i <= 4; i += 1) {
    const y = pad.top + plotH - (plotH * i) / 4;
    svg.append(el("line", { x1: pad.left, x2: width - pad.right, y1: y, y2: y, class: i === 0 ? "axis" : "gridline" }));
    svg.append(el("text", { x: pad.left - 6, y: y + 4, "text-anchor": "end" }, format((max * i) / 4)));
  }
  const y = (value) => pad.top + plotH - (plotH * value) / max;
  return { svg, pad, y, plotW: width - pad.left - pad.right };
}

function link(node, href) {
  if (!href) return node;
  const anchor = el("a", { href });
  anchor.append(node);
  return anchor;
}

// series: [{ name, color, points: [{ x (0..n-1), y|null, title, href, failed }] }]
export function lineChart({ series, count, format, width = 640, height = 220 }) {
  const values = series.flatMap((line) => line.points.map((point) => point.y)).filter((value) => value != null);
  const max = niceMax(Math.max(0, ...values));
  const { svg, pad, y, plotW } = frame(width, height, max, format);
  const x = (index) => pad.left + (count <= 1 ? plotW / 2 : (plotW * index) / (count - 1));
  for (const line of series) {
    const drawn = line.points.filter((point) => point.y != null);
    if (drawn.length > 1) {
      svg.append(el("polyline", {
        points: drawn.map((point) => `${x(point.x)},${y(point.y)}`).join(" "),
        fill: "none", stroke: line.color, "stroke-width": 2, "stroke-linejoin": "round", "stroke-dasharray": line.dashed ? "5 4" : "none",
      }));
    }
    for (const point of drawn) {
      const dot = el("circle", { cx: x(point.x), cy: y(point.y), r: 4, fill: line.color, stroke: "var(--surface)", "stroke-width": 1.5 });
      dot.append(el("title", {}, point.title));
      svg.append(link(dot, point.href));
    }
    // Runs with no value (failed, not measured) get a marker on the axis.
    for (const point of line.points.filter((entry) => entry.y == null && entry.failed)) {
      const mark = el("path", { d: `M${x(point.x) - 4},${y(0) - 4} l8,8 m0,-8 l-8,8`, stroke: "var(--bad)", "stroke-width": 2 });
      mark.append(el("title", {}, point.title));
      svg.append(link(mark, point.href));
    }
  }
  return svg;
}

// bars: [{ label, title, href, failed, segments: [{ name, value|null, color }] }]
export function stackedBars({ bars, format, width = 640, height = 240 }) {
  const totals = bars.map((bar) => bar.segments.reduce((sum, segment) => sum + (segment.value ?? 0), 0));
  const max = niceMax(Math.max(0, ...totals));
  const { svg, pad, y, plotW } = frame(width, height, max, format);
  const slot = plotW / Math.max(1, bars.length);
  const barW = Math.max(4, Math.min(36, slot * 0.6));
  bars.forEach((bar, index) => {
    const left = pad.left + slot * index + (slot - barW) / 2;
    const group = el("g");
    let base = 0;
    for (const segment of bar.segments) {
      if (segment.value == null || segment.value <= 0) continue;
      const top = y(base + segment.value);
      const rect = el("rect", { x: left, y: top, width: barW, height: Math.max(1, y(base) - top), fill: segment.color, rx: 2 });
      rect.append(el("title", {}, `${bar.title}\n${segment.name}: ${format(segment.value)}`));
      group.append(rect);
      base += segment.value;
    }
    if (bar.failed) {
      const mark = el("path", { d: `M${left + barW / 2 - 4},${y(base) - 12} l8,8 m0,-8 l-8,8`, stroke: "var(--bad)", "stroke-width": 2 });
      mark.append(el("title", {}, `${bar.title}\nfailed`));
      group.append(mark);
    }
    svg.append(link(group, bar.href));
    if (bars.length <= 16) {
      svg.append(el("text", { x: left + barW / 2, y: height - 8, "text-anchor": "middle" }, bar.label));
    }
  });
  return svg;
}
