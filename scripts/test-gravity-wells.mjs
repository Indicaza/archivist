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

console.log("gravity wells geometry: ok");
