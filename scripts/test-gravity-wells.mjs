// Gravity Well geometry (frontend/qml/App/Workbench/Dock/WellGeometry.js):
// where icons sit, which one is under the pointer, and how a saved position
// maps to the window and back.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const geometryPath = path.join(root, "frontend/qml/App/Workbench/Dock/WellGeometry.js");

function loadGeometry() {
  const context = vm.createContext({ Math, Number });
  const source = fs.readFileSync(geometryPath, "utf8").replace(/^\s*\.pragma library\s*$/m, "");
  vm.runInContext(source, context, { filename: geometryPath });
  return context;
}

// Results come from another realm: compare plain copies.
const plain = (value) => JSON.parse(JSON.stringify(value));
const loaded = loadGeometry();
const G = {
  layout: (...args) => plain(loaded.layout(...args)),
  slotAt: (slots, x, y) => loaded.slotAt(slots, x, y),
  place: (...args) => plain(loaded.place(...args)),
  position: (...args) => plain(loaded.position(...args)),
  magnify: (...args) => plain(loaded.magnify(...args)),
  distanceToRect: (...args) => loaded.distanceToRect(...args),
  proximity: (...args) => plain(loaded.proximity(...args)),
};
const options = { slot: 40, gap: 4, padding: 6, lead: 14, dividerSize: 13 };

// Slots in a row, after the grip, with the Well wrapped around them.
{
  const result = G.layout("linear", "horizontal", 3, { ...options, dividerAt: -1 });
  assert.deepEqual(result.slots.map((slot) => slot.x), [20, 64, 108]);
  assert.ok(result.slots.every((slot) => slot.y === 6 && slot.size === 40));
  assert.equal(result.width, 6 + 14 + 40 * 3 + 4 * 2 + 6);
  assert.equal(result.height, 52);
  assert.equal(result.divider, null);
}

// The divider between pinned and minimized takes its own room.
{
  const result = G.layout("linear", "horizontal", 3, { ...options, dividerAt: 2 });
  assert.deepEqual(result.slots.map((slot) => slot.x), [20, 64, 121]);
  assert.ok(result.divider.x > 64 + 40 && result.divider.x < 121);
  assert.equal(result.divider.y, 26);
  // No divider at either end.
  assert.equal(G.layout("linear", "horizontal", 3, { ...options, dividerAt: 0 }).divider, null);
  assert.equal(G.layout("linear", "horizontal", 3, { ...options, dividerAt: 3 }).divider, null);
}

// Vertical is the same row turned on its side.
{
  const across = G.layout("linear", "horizontal", 4, { ...options, dividerAt: 1 });
  const down = G.layout("linear", "vertical", 4, { ...options, dividerAt: 1 });
  assert.equal(down.height, across.width);
  assert.equal(down.width, across.height);
  assert.deepEqual(down.slots.map((slot) => slot.y), across.slots.map((slot) => slot.x));
}

// Hit testing uses the fixed slots: gaps and the grip hit nothing.
{
  const { slots } = G.layout("linear", "horizontal", 3, { ...options, dividerAt: -1 });
  assert.equal(G.slotAt(slots, 21, 10), 0);
  assert.equal(G.slotAt(slots, 62, 20), -1);
  assert.equal(G.slotAt(slots, 64, 45), 1);
  assert.equal(G.slotAt(slots, 10, 20), -1);
  assert.equal(G.slotAt(slots, 147, 20), 2);
  assert.equal(G.slotAt(slots, 148, 20), -1);
}

// Bottom centre by default, fully inside the window at any size.
{
  const placed = G.place({ x: 0.5, y: 1 }, 200, 52, 1000, 600, 12, 0);
  assert.equal(placed.x, 400);
  assert.equal(placed.y, 600 - 52 - 12);
  const lifted = G.place({ x: 0.5, y: 1 }, 200, 52, 1000, 600, 12, 100);
  assert.equal(lifted.y, 600 - 52 - 12 - 100);
  const squeezed = G.place({ x: 1, y: 1 }, 400, 52, 300, 40, 12, 0);
  assert.equal(squeezed.x, 12);
  assert.equal(squeezed.y, 12);
  const wild = G.place({ x: 7, y: -3 }, 200, 52, 1000, 600, 12, 0);
  assert.deepEqual([wild.x, wild.y], [1000 - 200 - 12, 12]);
}

// A drop maps back to the same position, and snaps to the centre and edges.
{
  const there = G.position(150, 200, 200, 52, 1000, 600, 12, 0, 18);
  const back = G.place(there, 200, 52, 1000, 600, 12, 0);
  assert.ok(Math.abs(back.x - 150) < 1e-9 && Math.abs(back.y - 200) < 1e-9);
  assert.equal(G.position(410, 200, 200, 52, 1000, 600, 12, 0, 18).x, 0.5);
  assert.equal(G.position(150, 600 - 52 - 12 - 10, 200, 52, 1000, 600, 12, 0, 18).y, 1);
  assert.equal(G.position(150, 20, 200, 52, 1000, 600, 12, 0, 18).y, 0);
  const off = G.position(-500, 9000, 200, 52, 1000, 600, 12, 0, 18);
  assert.deepEqual([off.x, off.y], [0, 1]);
}

// The Dock wave: the orb under the pointer swells most, its neighbours
// about half as much, the rest not at all.
const wave = { maxScale: 1.62, reach: 1.9, pitch: 46 };
const centreOf = (slot) => slot.x + slot.size / 2;
{
  const { slots } = G.layout("linear", "horizontal", 7, { slot: 40, gap: 6, padding: 10, lead: 0, dividerAt: -1 });
  const result = G.magnify(slots, centreOf(slots[3]), wave, null);
  assert.ok(Math.abs(result.scales[3] - 1.62) < 1e-9);
  assert.ok(result.scales[2] >= 1.2 && result.scales[2] <= 1.32, `neighbour ${result.scales[2]}`);
  assert.ok(Math.abs(result.scales[2] - result.scales[4]) < 1e-9);
  assert.equal(result.scales[0], 1);
  assert.equal(result.scales[6], 1);
  // The one under the pointer stays put; the others step aside, outward.
  assert.ok(Math.abs(result.shifts[3]) < 1e-9);
  assert.ok(result.shifts[2] < 0 && result.shifts[4] > 0);
  assert.ok(result.shifts[1] < result.shifts[2] && result.shifts[5] > result.shifts[4]);
  // They never close up: every drawn gap is at least the gap at rest.
  for (let index = 1; index < slots.length; index += 1) {
    const left = slots[index - 1];
    const right = slots[index];
    const leftEdge = centreOf(left) + result.shifts[index - 1] + (left.size * result.scales[index - 1]) / 2;
    const rightEdge = centreOf(right) + result.shifts[index] - (right.size * result.scales[index]) / 2;
    assert.ok(rightEdge - leftEdge >= 6 - 1e-9, `gap ${index} ${rightEdge - leftEdge}`);
  }
  // The drawn row widens both ways.
  assert.ok(result.start < slots[0].x && result.end > slots[6].x + 40);
}

// It follows the pointer continuously: halfway between two orbs, both are
// the same size, between the peak and the neighbour size.
{
  const { slots } = G.layout("linear", "horizontal", 5, { slot: 40, gap: 6, padding: 10, lead: 0, dividerAt: -1 });
  const between = (centreOf(slots[1]) + centreOf(slots[2])) / 2;
  const result = G.magnify(slots, between, wave, null);
  assert.ok(Math.abs(result.scales[1] - result.scales[2]) < 1e-9);
  assert.ok(result.scales[1] > 1.3 && result.scales[1] < 1.62);
  // A small move makes a small change: no steps.
  const nudged = G.magnify(slots, between + 1, wave, null);
  for (let index = 0; index < slots.length; index += 1) {
    assert.ok(Math.abs(nudged.scales[index] - result.scales[index]) < 0.02);
    assert.ok(Math.abs(nudged.shifts[index] - result.shifts[index]) < 1.5);
  }
}

// No pointer, or no magnification (reduced motion): nothing moves.
{
  const { slots } = G.layout("linear", "horizontal", 4, { slot: 40, gap: 6, padding: 10, lead: 0, dividerAt: 2 });
  const still = G.magnify(slots, null, wave, { index: 2 });
  assert.ok(still.scales.every((scale) => scale === 1));
  assert.ok(still.shifts.every((shift) => shift === 0));
  assert.equal(still.dividerShift, 0);
  const flat = G.magnify(slots, centreOf(slots[1]), { ...wave, maxScale: 1 }, null);
  assert.ok(flat.scales.every((scale) => scale === 1));
  // The divider rides the wave, halfway between its neighbours.
  const moved = G.magnify(slots, centreOf(slots[1]), wave, { index: 2 });
  assert.ok(moved.dividerShift > 0);
}

// Proximity: wakes as the pointer comes close, sleeps again only further
// out, and engages over the Well itself.
{
  const options = { revealIn: 96, revealOut: 118, engageIn: 0, engageOut: 22 };
  assert.equal(G.distanceToRect(10, 10, 100, 40), 0);
  assert.equal(G.distanceToRect(-30, 10, 100, 40), 30);
  assert.equal(G.distanceToRect(103, 44, 100, 40), 5);

  let state = { near: false, engaged: false, level: 0 };
  state = G.proximity(state, 150, options);
  assert.deepEqual([state.near, state.engaged, state.level], [false, false, 0]);
  state = G.proximity(state, 100, options);
  assert.equal(state.near, false);
  state = G.proximity(state, 90, options);
  assert.equal(state.near, true);
  assert.ok(state.level > 0 && state.level < 0.5);
  const far = state.level;
  state = G.proximity(state, 40, options);
  assert.ok(state.level > far && !state.engaged);
  // Back out a little past where it woke: still awake (no flicker).
  state = G.proximity(state, 110, options);
  assert.equal(state.near, true);
  state = G.proximity(state, 0, options);
  assert.deepEqual([state.near, state.engaged, state.level], [true, true, 1]);
  // Just off the edge: still engaged.
  state = G.proximity(state, 15, options);
  assert.equal(state.engaged, true);
  state = G.proximity(state, 30, options);
  assert.equal(state.engaged, false);
  // Back to 15 isn't enough to engage again: it must come over the Well.
  state = G.proximity(state, 15, options);
  assert.equal(state.engaged, false);
  state = G.proximity(state, 130, options);
  assert.deepEqual([state.near, state.engaged, state.level], [false, false, 0]);
  assert.deepEqual(G.proximity({ near: true, engaged: true }, null, options), { near: false, engaged: false, level: 0 });
}

// The tab strip changes only what changed (TabStrip.js): a tab put in the
// Well or brought back is one change, and tabs that stay are never remade.
{
  const stripPath = path.join(root, "frontend/qml/App/Workbench/Dock/TabStrip.js");
  const context = vm.createContext({});
  vm.runInContext(fs.readFileSync(stripPath, "utf8").replace(/^\s*\.pragma library\s*$/m, ""), context, { filename: stripPath });
  const changes = (current, wanted) => plain(context.changes(current, wanted));
  const apply = (current, ops) => {
    const strip = current.slice();
    for (const op of ops) {
      if (op.op === "remove") strip.splice(op.at, 1);
      else if (op.op === "insert") strip.splice(op.at, 0, op.id);
      else strip.splice(op.to, 0, strip.splice(op.from, 1)[0]);
    }
    return strip;
  };
  assert.deepEqual(changes(["a", "b", "c"], ["a", "b", "c"]), []);
  assert.deepEqual(changes(["a", "b", "c"], ["a", "c"]), [{ op: "remove", at: 1 }]);
  assert.deepEqual(changes(["a", "c"], ["a", "b", "c"]), [{ op: "insert", at: 1, id: "b" }]);
  assert.deepEqual(changes([], ["a", "b"]), [{ op: "insert", at: 0, id: "a" }, { op: "insert", at: 1, id: "b" }]);
  assert.deepEqual(changes(["a", "b", "c"], ["c", "a", "b"]).length, 1);
  // Any reordering, removal or addition comes out right, and a tab that
  // stays is never removed.
  let seed = 7;
  const random = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
  for (let round = 0; round < 500; round += 1) {
    const pool = ["a", "b", "c", "d", "e", "f", "g"];
    const current = pool.filter(() => random() < 0.6);
    const wanted = pool.filter(() => random() < 0.6).sort(() => random() - 0.5);
    const ops = changes(current, wanted);
    assert.deepEqual(apply(current, ops), wanted);
    let strip = current.slice();
    for (const op of ops) {
      if (op.op === "remove") assert.ok(!wanted.includes(strip[op.at]), "a staying tab was removed");
      strip = apply(strip, [op]);
    }
  }
}


// The hover card (frontend/qml/App/Workbench/Dock/WellTip.js): what a sphere
// holds, where the card sits, and when it shows.
{
  const tipPath = path.join(root, "frontend/qml/App/Workbench/Dock/WellTip.js");
  const context = vm.createContext({ Math, Number, String });
  vm.runInContext(fs.readFileSync(tipPath, "utf8").replace(/^\s*\.pragma library\s*$/m, ""), context, { filename: tipPath });
  const T = {
    describe: (...args) => plain(context.describe(...args)),
    fit: (...args) => plain(context.fit(...args)),
    place: (...args) => plain(context.place(...args)),
    hover: (...args) => plain(context.hover(...args)),
  };
  // Lookups as the workspace gives them: only what's recorded.
  const dirtyIds = new Set(["doc:store"]);
  const look = {
    title: (info) => info.title,
    icon: (info) => (info.typeId === "archivist.chat" ? "chat" : info.typeId === "archivist.terminal" ? "terminal" : "file-text"),
    kind: (info) => {
      const name = String((info.resource.details || {}).name || "");
      if (name.endsWith(".cpp")) return "C++";
      if (name.endsWith(".qml")) return "Qt QML";
      return { "archivist.chat": "Chat", "archivist.terminal": "Terminal" }[info.typeId] || "";
    },
    path: (info) => String((info.resource.details || {}).path || ""),
    dirty: (info) => info.resource.kind === "document" && dirtyIds.has(info.resource.id),
  };
  const file = (viewId, name, dir, id) => ({
    viewId, typeId: "archivist.editor", title: name,
    resource: { kind: "document", id: id || "doc:" + viewId, details: { name, path: dir + "/" + name } },
  });

  // One Magnet: its own name, type and path; nothing made up.
  {
    const card = T.describe({ entryId: "we1", state: "minimized", panel: false, views: [file("v1", "main.cpp", "src/app")] }, look);
    assert.equal(card.anchor, false);
    assert.equal(card.title, "main.cpp");
    assert.equal(card.kind, "C++");
    assert.equal(card.icon, "file-text");
    assert.equal(card.path, "src/app/main.cpp");
    assert.equal(card.dirty, false);
    assert.equal(card.note, "");
    assert.deepEqual(card.rows, []);
    // Unsaved changes show.
    assert.equal(T.describe({ state: "minimized", views: [file("v2", "store.cpp", "src", "doc:store")] }, look).dirty, true);
    // A chat has no path, and the card doesn't invent one.
    const chat = T.describe({ state: "minimized", views: [{ viewId: "c", typeId: "archivist.chat", title: "Architecture Chat", resource: { kind: "chat", id: "c1" } }] }, look);
    assert.equal(chat.kind, "Chat");
    assert.equal(chat.path, "");
    // Pinned and not open: what the entry remembers.
    const pinned = T.describe({ entryId: "we3", state: "pinnedClosed", pinned: true, typeId: "archivist.editor", title: "App.qml",
      resource: { kind: "document", id: "doc:app", details: { name: "App.qml", path: "qml/App.qml" } }, views: [] }, look);
    assert.equal(pinned.title, "App.qml");
    assert.equal(pinned.kind, "Qt QML");
    assert.equal(pinned.note, "Pinned · not open");
    // An unknown type says nothing rather than guessing.
    assert.equal(T.describe({ state: "minimized", views: [{ viewId: "z", typeId: "x.unknown", title: "Thing", resource: {} }] }, look).kind, "");
  }

  // An Anchor: its name, its Magnets in tab order, the active one marked.
  {
    const views = [
      file("v1", "App.qml", "qml"),
      file("v2", "DockWorkspace.qml", "qml/Dock"),
      file("v3", "workspace_store.cpp", "src", "doc:store"),
      { viewId: "v4", typeId: "archivist.chat", title: "Architecture Chat", resource: { kind: "chat", id: "c1" } },
      { viewId: "v5", typeId: "archivist.terminal", title: "Terminal", resource: { kind: "terminal", id: "t1" } },
    ];
    const card = T.describe({ panel: true, title: "Panel", anchorName: "Development", activeViewId: "v1", views }, look);
    assert.equal(card.anchor, true);
    assert.equal(card.title, "Development");
    assert.equal(card.note, "5 Magnets");
    assert.equal(card.icon, "grid");
    assert.deepEqual(card.rows.map((row) => row.title), ["App.qml", "DockWorkspace.qml", "workspace_store.cpp", "Architecture Chat", "Terminal"]);
    assert.deepEqual(card.rows.map((row) => row.active), [true, false, false, false, false]);
    assert.deepEqual(card.rows.map((row) => row.dirty), [false, false, true, false, false]);
    assert.deepEqual(card.rows.map((row) => row.icon), ["file-text", "file-text", "file-text", "chat", "terminal"]);
    // Reordered or renamed, it reads what the workspace says now.
    const moved = T.describe({ panel: true, anchorName: "Dev", activeViewId: "v5", views: [views[4], views[0]] }, look);
    assert.deepEqual(moved.rows.map((row) => row.title), ["Terminal", "App.qml"]);
    assert.equal(moved.rows[0].active, true);
    assert.equal(moved.title, "Dev");
    // No name of its own: the name it showed when it was put away.
    assert.equal(T.describe({ panel: true, anchorName: "", title: "Editor", views }, look).title, "Editor");
    assert.equal(T.describe({ panel: true, views: [views[0]] }, look).note, "1 Magnet");
    // A tab that's gone shows as unavailable, with no details.
    const stale = T.describe({ panel: true, anchorName: "A", activeViewId: "gone", views: [views[0], { viewId: "gone", missing: true }] }, look);
    assert.deepEqual(stale.rows[1], { viewId: "gone", title: "Unavailable", icon: "file", kind: "", active: false, dirty: false, missing: true });
    assert.equal(stale.rows.some((row) => row.active), false);
    // Nothing at all: an empty card, no crash.
    assert.equal(T.describe(null, look).title, "Untitled");
  }

  // Never taller than the room it has; the rest are counted.
  {
    assert.deepEqual(T.fit(5, 600, 60, 22, 8), { shown: 5, more: 0 });
    assert.deepEqual(T.fit(8, 600, 60, 22, 8), { shown: 8, more: 0 });
    assert.deepEqual(T.fit(40, 600, 60, 22, 8), { shown: 7, more: 33 });
    // Little room: fewer rows, and the count row still fits.
    const tight = T.fit(40, 60 + 22 * 3, 60, 22, 8);
    assert.deepEqual(tight, { shown: 2, more: 38 });
    assert.ok(60 + (tight.shown + 1) * 22 <= 60 + 22 * 3);
    // Hardly any: still one row and the count.
    assert.deepEqual(T.fit(3, 10, 60, 22, 8), { shown: 1, more: 2 });
  }

  // Placed above its sphere, inside the window, flipping when it must.
  {
    const bounds = { x: 0, y: 0, width: 1000, height: 700 };
    const sphere = { x: 480, y: 620, width: 40, height: 40 };
    let at = T.place(sphere, 200, 120, bounds, "above", 8, 8);
    assert.equal(at.side, "above");
    assert.equal(at.x, 400);
    assert.equal(at.y, 620 - 8 - 120);
    assert.equal(at.pointer, 100);
    // At the left edge it slides in, and its pointer still finds the sphere.
    at = T.place({ x: 2, y: 620, width: 40, height: 40 }, 200, 120, bounds, "above", 8, 8);
    assert.equal(at.x, 8);
    assert.equal(at.pointer, 22 - 8);
    // At the right edge too.
    at = T.place({ x: 960, y: 620, width: 40, height: 40 }, 200, 120, bounds, "above", 8, 8);
    assert.equal(at.x, 1000 - 8 - 200);
    assert.ok(at.pointer <= 200 - 12);
    // Near the top with no room above: below instead.
    at = T.place({ x: 480, y: 30, width: 40, height: 40 }, 200, 120, bounds, "above", 8, 8);
    assert.equal(at.side, "below");
    assert.equal(at.y, 78);
    // An upright Well on the right: the card goes left, inside the window.
    at = T.place({ x: 950, y: 10, width: 40, height: 40 }, 200, 120, bounds, "left", 8, 8);
    assert.equal(at.side, "left");
    assert.equal(at.x, 950 - 8 - 200);
    assert.equal(at.y, 8);
    // Whatever the sphere does, the card stays inside.
    for (let x = -50; x <= 1050; x += 37) {
      for (let y = -50; y <= 750; y += 41) {
        const box = T.place({ x, y, width: 64, height: 64 }, 240, 180, bounds, "above", 8, 8);
        assert.ok(box.x >= 8 && box.x + 240 <= 992, "stays inside across");
      }
    }
    // As the sphere swells (bigger, taller) the card rises with it and
    // stays centred on it.
    const rest = T.place({ x: 480, y: 620, width: 34, height: 34 }, 200, 120, bounds, "above", 8, 8);
    const swollen = T.place({ x: 469, y: 600, width: 55, height: 55 }, 200, 120, bounds, "above", 8, 8);
    assert.equal(rest.x + 100, 497);
    assert.ok(Math.abs(swollen.x + 100 - (469 + 27.5)) < 0.01);
    assert.ok(swollen.y + 120 + 8 <= 600, "never over the swollen sphere");
  }

  // When it shows: a rest first; then sphere to sphere without closing.
  {
    const run = (events) => {
      let state = { phase: "idle", entryId: "" };
      const seen = [];
      for (const event of events) {
        state = T.hover(state, event);
        seen.push({ phase: state.phase, entryId: state.entryId, visible: state.visible, timer: state.timer });
      }
      return seen;
    };
    // Hovered: waits, then shows.
    let seen = run([{ type: "enter", entryId: "a" }, { type: "timer" }]);
    assert.deepEqual(seen.map((s) => s.visible), [false, true]);
    assert.equal(seen[0].timer, "open");
    // Passing over on the way somewhere else: never shows.
    seen = run([{ type: "enter", entryId: "a" }, { type: "leave" }]);
    assert.deepEqual(seen.map((s) => s.visible), [false, false]);
    assert.equal(seen[1].phase, "idle");
    // Across the row while it shows: through the gaps between spheres too,
    // it stays up and only changes what it says.
    seen = run([
      { type: "enter", entryId: "a" }, { type: "timer" },
      { type: "leave" }, { type: "enter", entryId: "b" },
      { type: "leave" }, { type: "enter", entryId: "c" },
    ]);
    assert.deepEqual(seen.slice(1).map((s) => s.visible), [true, true, true, true, true]);
    assert.deepEqual(seen.slice(1).map((s) => s.entryId), ["a", "a", "b", "b", "c"]);
    // Left for good: gone after the grace; back soon after, at once.
    seen = run([
      { type: "enter", entryId: "a" }, { type: "timer" }, { type: "leave" }, { type: "timer" },
      { type: "enter", entryId: "b" },
    ]);
    assert.deepEqual(seen.map((s) => s.visible), [false, true, true, false, true]);
    assert.equal(seen[3].timer, "warm");
    // ...but after a while, it waits again.
    seen = run([
      { type: "enter", entryId: "a" }, { type: "timer" }, { type: "leave" }, { type: "timer" }, { type: "timer" },
      { type: "enter", entryId: "b" },
    ]);
    assert.equal(seen[4].phase, "idle");
    assert.equal(seen[5].visible, false);
    // A press, a drag, a move or a menu: away at once.
    seen = run([{ type: "enter", entryId: "a" }, { type: "timer" }, { type: "cancel" }]);
    assert.deepEqual(seen[2], { phase: "idle", entryId: "", visible: false, timer: "stop" });
  }

  // Hover stays put while the spheres swell: what's under the pointer is
  // decided by the fixed slots, never by the wave.
  {
    const layout = G.layout("linear", "horizontal", 6, { ...options, dividerAt: -1 });
    for (let x = layout.slots[0].x; x < layout.slots[5].x + 40; x += 1) {
      const wave = G.magnify(layout.slots, x, { maxScale: 1.62, reach: 1.9, pitch: 46, vertical: false }, { index: -1 });
      const under = G.slotAt(layout.slots, x, 26);
      const again = G.slotAt(layout.slots, x, 26);
      assert.equal(under, again);
      if (under >= 0) {
        assert.ok(x >= layout.slots[under].x && x < layout.slots[under].x + 40);
        assert.ok(wave.scales[under] >= 1);
      }
    }
  }
}

console.log("gravity wells geometry: ok");
