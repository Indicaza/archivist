import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const source = (file) => readFile(path.join(root, "frontend", file), "utf8");

test("read-only Library tools are compiled independently with identical registry entry points", async () => {
  const dispatcher = await source("src/Core/Domains/AI/ai_tools.cpp");
  const read = await source("src/Core/Domains/AI/ai_tools_read.cpp");
  const header = await source("src/Core/Domains/AI/ai_tools_read.h");
  const cmake = await source("CMakeLists.txt");

  for (const name of ["listDirectory", "searchFilenames", "searchLibrary", "readFile", "readFileRange", "readFileRanges"]) {
    assert.match(header, new RegExp("QVariantMap " + name + "\\("));
    assert.match(read, new RegExp("^QVariantMap " + name + "\\(", "m"));
    assert.doesNotMatch(dispatcher, new RegExp("^QVariantMap " + name + "\\(", "m"));
    assert.ok(dispatcher.includes("using AITools::ReadTools::" + name + ";"));
  }
  assert.match(cmake, /src\/Core\/Domains\/AI\/ai_tools_read\.cpp/);
  assert.match(cmake, /src\/Core\/Domains\/AI\/ai_tools_read\.h/);
  assert.match(read, /#include <QCollator>/);
  assert.doesNotMatch(dispatcher, /#include <QCollator>/);
  assert.match(read, /LibraryRetrieval::search\(/);
  assert.match(read, /LibraryFiles::readText\(/);
});

test("moved read handlers retain the same cancellation exception caught by executor", async () => {
  const dispatcher = await source("src/Core/Domains/AI/ai_tools.cpp");
  const read = await source("src/Core/Domains/AI/ai_tools_read.cpp");
  const header = await source("src/Core/Domains/AI/ai_tools_read.h");

  assert.match(header, /namespace AITools::ReadTools[\s\S]*struct ToolCancelled\s*\{\s*\};/);
  assert.match(dispatcher, /using AITools::ReadTools::ToolCancelled;/);
  assert.match(dispatcher, /catch \(const ToolCancelled &\)/);
  assert.match(read, /throw ToolCancelled\{\};/);
  assert.match(read, /void throwIfCancelled\(const Context &context\)/);
  assert.doesNotMatch(dispatcher, /struct ToolCancelled\s*\{/);
});

test("read tool behavioral contracts retain result fields and guard checks", async () => {
  const read = await source("src/Core/Domains/AI/ai_tools_read.cpp");
  for (const field of ["totalEntries", "truncated", "matches", "warnings", "startLine", "endLine", "totalCharacterCount", "returnedCharacterCount"]) {
    assert.ok(read.includes('QStringLiteral("' + field + '")'), "Missing read result field: " + field);
  }
  assert.match(read, /readFileRange\(range\.toMap\(\), context\)/);
  assert.match(read, /throwIfCancelled\(context\)/);
  assert.match(read, /resolveLibraryFileId\(\*context\.database, libraryId/);
  assert.match(read, /std::stable_sort\(entries\.begin\(\), entries\.end\(\)/);
});
