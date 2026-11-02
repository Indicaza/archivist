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
    // Registered beside the handlers (libraryReadTools()), not in the executor.
    assert.ok(read.includes("ReadTools::" + name + ","), "Missing registration for " + name);
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

test("AI tool families stay behind the internal definition contract", async () => {
  const files = {
    executor: await source("src/Core/Domains/AI/ai_tools.cpp"),
    catalog: await source("src/Core/Domains/AI/ai_tools_catalog.cpp"),
    definition: await source("src/Core/Domains/AI/ai_tools_definition.h"),
    input: await source("src/Core/Domains/AI/ai_tools_input.cpp"),
    read: await source("src/Core/Domains/AI/ai_tools_read.cpp"),
    mutation: await source("src/Core/Domains/AI/ai_tools_mutation.cpp"),
    workspace: await source("src/Core/Domains/AI/ai_tools_workspace.cpp"),
  };
  const cmake = await source("CMakeLists.txt");
  for (const file of ["ai_tools_catalog.cpp", "ai_tools_definition.h", "ai_tools_input.cpp", "ai_tools_input.h", "ai_tools_mutation.cpp", "ai_tools_workspace.cpp"]) {
    assert.match(cmake, new RegExp("src/Core/Domains/AI/" + file.replace(".", "\\.")));
  }
  assert.match(cmake, /tests\/core_ai_tool_contracts_test\.cpp/);

  // Only the workspace family reaches the Workspace domain.
  for (const [name, text] of Object.entries(files)) {
    if (name !== "workspace") assert.doesNotMatch(text, /Domains\/Workspace\//, name + " must not include Workspace headers");
  }
  // The executor and catalog know no family's domain.
  for (const text of [files.executor, files.catalog]) {
    assert.doesNotMatch(text, /Domains\/Libraries\/|edit_proposals\.h|library_retrieval\.h/);
  }
  // Each family registers through its own list; the catalog combines them.
  assert.match(files.read, /QList<Definition> libraryReadTools\(\)/);
  assert.match(files.mutation, /QList<Definition> libraryMutationTools\(\)/);
  assert.match(files.workspace, /QList<Definition> workspaceTools\(\)/);
  for (const family of ["libraryReadTools", "libraryMutationTools", "workspaceTools"]) {
    assert.ok(files.catalog.includes("tools.append(" + family + "());"), family);
  }
  // Callbacks stay plain function pointers (no per-lambda std::function).
  assert.doesNotMatch(files.definition, /std::function</);
  // The executor alone records executions and lifecycle events.
  for (const event of ["tool.requested", "tool.started", "tool.completed", "tool.failed", "tool.cancelled"]) {
    assert.ok(files.executor.includes('"' + event + '"'), event);
  }
  for (const [name, text] of Object.entries(files)) {
    if (name !== "executor") assert.doesNotMatch(text, /(INSERT INTO|UPDATE|FROM) ai_tool_executions/, name);
  }
});
