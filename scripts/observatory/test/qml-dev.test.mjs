import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { isQmlRuntimeFile, qtBinaryCandidates, sourceQmlDirectory } from "../../qml-dev-utils.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");

test("QML developer watcher restarts for runtime assets, not C++ or build products", () => {
  for (const file of ["App.qml", "App/Icons/IconRegistry.js", "App/Theme/Palette.mjs", "App/Icons/example.svg", "App/Icons/Assets/fonts/seti.ttf", "App/Dock/shaders/genie.frag.qsb"]) {
    assert.equal(isQmlRuntimeFile(file), true, file);
  }
  for (const file of ["src/main.cpp", "frontend/CMakeLists.txt", "gen/cache.cpp.o", "README.md", "", undefined, null]) {
    assert.equal(isQmlRuntimeFile(file), false, String(file));
  }
});

test("QML developer path is the source tree; native binary comes from existing debug build", () => {
  assert.equal(sourceQmlDirectory(root), path.join(root, "frontend", "qml"));
  const bins = qtBinaryCandidates(root);
  assert.equal(bins.length, 2);
  assert.ok(bins.every((file) => file.startsWith(path.join(root, "build", "frontend-debug"))));
});

test("source QML loading is opt-in and production retains embedded module loading", async () => {
  const source = await readFile(path.join(root, "frontend", "src", "main.cpp"), "utf8");
  assert.match(source, /qEnvironmentVariable\("ARCHIVIST_QML_SOURCE_DIR"\)/);
  assert.match(source, /engine\.load\(QUrl::fromLocalFile\(source\)\)/);
  assert.match(source, /engine\.loadFromModule\("Archivist", "App"\)/);
});
