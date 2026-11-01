import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const source = (file) => readFile(path.join(root, "frontend", file), "utf8");

test("AI tool schemas use only the lightweight composition catalog", async () => {
  const tools = await source("src/Core/Domains/AI/ai_tools.cpp");
  const catalog = await source("src/Core/Domains/Workspace/workspace_compose_catalog.h");

  assert.match(tools, /#include "Core\/Domains\/Workspace\/workspace_compose_catalog\.h"/);
  assert.doesNotMatch(tools, /#include "Core\/Domains\/Workspace\/workspace_compose\.h"/);
  assert.doesNotMatch(catalog, /#include "Core\/Domains\/Workspace\/workspace_(?:model|types)\.h"/);
  assert.doesNotMatch(catalog, /#include "Core\/Domains\/Workspace\/workspace_compose\.h"/);

  for (const name of ["templates", "templateIds", "strategies", "holdWords"]) {
    assert.match(catalog, new RegExp(`\\b${name}\\(\\)`));
  }
  for (const [name, value] of [["kMaximumSlots", 6], ["kMaximumItemsPerSlot", 8], ["kMaximumItems", 16]]) {
    assert.match(catalog, new RegExp(`inline constexpr int ${name} = ${value};`));
  }
  assert.match(catalog, /struct Template\s*\{[\s\S]*?QJsonObject recipe;/);
});

test("existing compose header keeps the full planning API via catalog re-export", async () => {
  const composed = await source("src/Core/Domains/Workspace/workspace_compose.h");
  const model = await source("src/Core/Domains/Workspace/workspace_model.h");
  const impl = await source("src/Core/Domains/Workspace/workspace_compose.cpp");
  const cmake = await source("CMakeLists.txt");

  assert.match(composed, /#include "Core\/Domains\/Workspace\/workspace_compose_catalog\.h"/);
  assert.match(composed, /#include "Core\/Domains\/Workspace\/workspace_model\.h"/);
  assert.match(composed, /Plan plan\(const Spec &spec, const WorkspaceModel &model\)/);
  assert.match(model, /class WorkspaceModel/);
  assert.match(impl, /#include "Core\/Domains\/Workspace\/workspace_compose\.h"/);
  assert.match(cmake, /src\/Core\/Domains\/Workspace\/workspace_compose_catalog\.h/);
  assert.doesNotMatch(composed, /(?:inline constexpr int kMaximumSlots|struct Template|QStringList holdWords\(\))/);
});
