# AI tools

The tools an agent can call during a run: Library reads, Library edit
proposals and `workspace_*` arrangement tools. The rest of the app uses only
`ai_tools.h`; everything else here is internal to the AI domain.

## Modules

| File | Layer | Owns |
| --- | --- | --- |
| `ai_tools.h` | Contract (public) | `ToolError`, `Context`, `Availability`, `providerTools`, `execute`, `executionsByRun` |
| `ai_tools_definition.h` | Contract (internal) | `Definition`, permission and execution-mode names, family entry points |
| `ai_tools_input.{h,cpp}` | Validation | strict-object parsing, field checks, `InvalidInput`, JSON schema builders |
| `ai_tools_catalog.cpp` | Catalog | combines the families, sorts by id, provider discovery rules |
| `ai_tools_read.{h,cpp}` | Library adapter (read) | six read-only tools: definitions and handlers |
| `ai_tools_mutation.cpp` | Library adapter (proposals) | five proposal tools: definitions and handlers; never writes files |
| `ai_tools_workspace.cpp` | Workspace adapter | seven `workspace_*` tools; the only tools file that includes Workspace headers |
| `ai_tools.cpp` | Execution | parse → permission → run → cancellation → `ai_tool_executions` row and `tool.*` events |

Dependency direction (no cycles):

```
ai_runtime.cpp ──► ai_tools.h
                      ▲
ai_tools.cpp (executor) ──► ai_tools_definition.h ◄── ai_tools_catalog.cpp
                                   ▲
            ai_tools_read.cpp, ai_tools_mutation.cpp, ai_tools_workspace.cpp
                 │                    │                     │
                 ▼                    ▼                     ▼
          Libraries, retrieval   Libraries, edit_proposals  Workspace gateway/catalog
```

Each family depends on `ai_tools_input.h` for parsing and schemas. The
executor and the catalog depend on no family's domain.

## Adding a tool

1. Pick the family it belongs to (or add one: declare `QList<Definition>
   yourFamilyTools()` in `ai_tools_definition.h`, define it in a new
   `ai_tools_<family>.cpp`, append it in `ai_tools_catalog.cpp` and list the
   file in `frontend/CMakeLists.txt`).
2. Write the handler beside the family's other handlers:
   `QVariantMap handler(const QVariantMap &parsedInput, Context &context)`.
   Call `ReadTools::throwIfCancelled(context)` around slow steps; throw
   `ToolError` for expected failures.
3. Append a `Definition`: id, name, description, permission, execution mode,
   JSON schema (`objectSchema`, `stringSchema`, ...), a parser built from
   `strictObject`/`stringField`/`intField`/... that matches the schema, and
   input and output summaries for the `tool.started`/`tool.completed` events.
   Parsers, handlers and summaries are plain functions or captureless lambdas.
4. If the tool should only be offered in some runs, extend
   `providerTools` in `ai_tools_catalog.cpp` and `Availability`.
5. Record the contract: run `core-ai-tool-contracts-test` with
   `ARCHIVIST_UPDATE_TOOL_CONTRACTS=1`, review the
   `tests/fixtures/ai_tool_contracts.json` diff, and add behavior tests to
   `tests/core_ai_test.cpp`.

The executor, permission rules and persistence need no change for a new tool
that uses an existing permission level and execution mode.

## Contract snapshot

`tests/core_ai_tool_contracts_test.cpp` pins every tool's id, name,
description, schema, discovery for all 32 `Availability` combinations,
ordering, permission, execution mode, invalid-input errors, lifecycle event
payloads with summaries, outputs, stored execution records, and the request
each workspace tool sends to the gateway. A refactor must leave the fixture
unchanged; a deliberate contract change re-records it and shows up in review.
