import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");

test("Qt WebEngine startup retains its early initialization without parsing WebEngine in main.cpp", async () => {
  const main = await readFile(path.join(root, "frontend", "src", "main.cpp"), "utf8");
  const helperHeader = await readFile(path.join(root, "frontend", "src", "App", "Services", "web_engine_bootstrap.h"), "utf8");
  const helperSource = await readFile(path.join(root, "frontend", "src", "App", "Services", "web_engine_bootstrap.cpp"), "utf8");
  const cmake = await readFile(path.join(root, "frontend", "CMakeLists.txt"), "utf8");

  assert.doesNotMatch(main, /#include\s*[<"]QtWebEngineQuick[>"]/);
  assert.match(main, /#include "App\/Services\/web_engine_bootstrap\.h"/);
  assert.match(helperHeader, /void initializeWebEngine\(\);/);
  assert.match(helperSource, /#include <QtWebEngineQuick>/);
  assert.match(helperSource, /void initializeWebEngine\(\)\s*\{\s*QtWebEngineQuick::initialize\(\);\s*\}/);

  const init = main.indexOf("    initializeWebEngine();");
  const app = main.indexOf("    QGuiApplication app(argc, argv);");
  assert.ok(init > 0 && init < app, "WebEngine must initialize before QGuiApplication");
  assert.equal((main.match(/initializeWebEngine\(\);/g) ?? []).length, 1);
  assert.match(cmake, /qt_add_executable\(archivist-qt\s+src\/main\.cpp\s+src\/App\/Services\/web_engine_bootstrap\.cpp/);
  assert.match(cmake, /Qt6::WebEngineQuick/);
});
