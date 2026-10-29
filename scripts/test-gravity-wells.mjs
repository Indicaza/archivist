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

console.log("gravity wells geometry: ok");
