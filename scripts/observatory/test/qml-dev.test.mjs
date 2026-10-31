import assert from "node:assert/strict";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { isQmlRuntimeFile, qtBinaryCandidates, sourceQmlDirectory } from "../../qml-dev-utils.mjs";
import { refreshDevQmlTree, withDevModuleImport } from "../../qml-dev-stage.mjs";

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
  assert.match(source, /engine\.addUrlInterceptor\(sourceInterceptor\.get\(\)\)/);
  assert.match(source, /QQmlAbstractUrlInterceptor/);
  assert.match(source, /QmldirFile/);
  assert.match(source, /\/qt\/qml\/Archivist\/qml\//);
  assert.match(source, /engine\.loadFromModule\("Archivist", "App"\)/);
  assert.doesNotMatch(source, /engine\.load\(QUrl::fromLocalFile/);
});

test("QML staging preserves pragmas and ensures module types remain importable", () => {
  assert.equal(withDevModuleImport("import QtQuick\\nItem {}\\n", "App.qml"), "import QtQuick\\nItem {}\\n");
  assert.equal(withDevModuleImport("import QtQuick\\nItem {}\\n", "App/Topbar/CodeThemePicker.qml"), "import Archivist 1.0\\nimport QtQuick\\nItem {}\\n");
  assert.equal(withDevModuleImport("pragma Singleton\\n\\nimport QtQuick\\nItem {}\\n", "App/Icons/SetiFont.qml"), "pragma Singleton\\nimport Archivist 1.0\\n\\nimport QtQuick\\nItem {}\\n");
  const existing = "import Archivist 1.0\\nimport QtQuick\\nItem {}\\n";
  assert.equal(withDevModuleImport(existing, "App/Icon.qml"), existing);
  assert.equal(withDevModuleImport("function hi() {}", "App/Icons/IconRegistry.js"), "function hi() {}");
});

test("dev mirror carries file-system relative imports and icons without changing source", async () => {
  const temp = await mkdtemp(path.join(os.tmpdir(), "archivist-qml-stage-test-"));
  const source = path.join(temp, "src");
  const staged = path.join(temp, "mirror");
  try {
    await mkdir(path.join(source, "App", "Icons"), { recursive: true });
    await mkdir(path.join(source, "App", "Topbar"), { recursive: true });
    await writeFile(path.join(source, "App.qml"), 'import "App/Topbar"\\nItem {}\\n');
    await writeFile(path.join(source, "App", "Topbar", "CodeThemePicker.qml"), 'import QtQuick\\nItem { AppIcon {} }\\n');
    await writeFile(path.join(source, "App", "Icons", "AppIcon.qml"), 'import QtQuick\\nItem {}\\n');
    await writeFile(path.join(source, "App", "Icons", "icon.svg"), "<svg/>");
    await refreshDevQmlTree(source, staged);
    const picker = await readFile(path.join(staged, "App", "Topbar", "CodeThemePicker.qml"), "utf8");
    assert.match(picker, /^import Archivist 1.0\\n/);
    assert.match(picker, /AppIcon/);
    assert.equal(await readFile(path.join(staged, "App", "Icons", "icon.svg"), "utf8"), "<svg/>");
    assert.equal(await readFile(path.join(source, "App", "Topbar", "CodeThemePicker.qml"), "utf8"), 'import QtQuick\\nItem { AppIcon {} }\\n');
    await writeFile(path.join(source, "App", "Topbar", "CodeThemePicker.qml"), 'import QtQuick\\nItem { AppIcon { visible: false } }\\n');
    await refreshDevQmlTree(source, staged);
    assert.match(await readFile(path.join(staged, "App", "Topbar", "CodeThemePicker.qml"), "utf8"), /visible: false/);
  } finally {
    await rm(temp, { recursive: true, force: true });
  }
});
