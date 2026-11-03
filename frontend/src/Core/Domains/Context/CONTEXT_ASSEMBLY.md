# Context Assembly v3

Decides what a model sees for one turn: which instructions, conversation,
attached files, Library excerpts and recalled messages, within one budget for
the whole provider request. Agents opt in by choosing the
`context-assembly` v3 Context Compiler (Agent editor → Context). Saved Agents
keep their compiler; nothing is migrated.

## Modules

| File | Layer | Owns |
| --- | --- | --- |
| `context_contracts.{h,cpp}` | Contracts | `Kind`, `Trust`, `EvidenceRef`, `SearchCandidate`, `ContextItem`, `AccessScope`, `Policy`, `ContextRequest`, omission reasons |
| `token_estimator.h` | Contract | `TokenEstimator` interface, `CharacterEstimator` (chars ÷ 4, the estimate every compiler has used) |
| `budget_ledger.{h,cpp}` | Accounting | `BudgetLedger`: output reserve, named reserves, per-kind usage, caps, over-budget flag |
| `context_assembler.{h,cpp}` | Selection | `assemble()` → `CompiledContext`, `admitToolResult()`, `policyFromConfig()`, the manifest |
| `context_sources.{h,cpp}` | Database adapters | stored messages, attachments, Library FTS and Chat recall → items and candidates |

Dependency direction:

```
ai_runtime.cpp ──► context_sources ──► ChatRepository, LibraryFiles, LibraryRetrieval, chat-message FTS, Retrieval (hybrid)
       │                 │
       └──────────► context_assembler ──► budget_ledger, token_estimator, context_contracts
```

The assembler has no database, provider, UI or run state. It takes a
`ContextRequest` and returns provider-agnostic messages (`role`, `content`)
plus a full record of what was included and why the rest was not.

## Contracts

- **`ContextRequest`**: one turn: instructions (final system text), serialized
  tool definitions, the current request, pre-built items (history,
  attachments), `SearchCandidate`s from any retriever, items a source already
  dropped (`preOmitted`), the `AccessScope` and the `Policy`.
- **`ContextItem`**: one unit that may be sent. Stable `id`, `kind`, `trust`,
  `priority`, `order` within its kind, `dedupeKey`, `coveredByKey`, and an
  `EvidenceRef`.
- **`EvidenceRef`**: provenance. Source type and id, Library, file, path, line
  range, Chat, who produced it (`chat-history`, `library-file-fts`,
  `attachment`, `chat-message-fts`). `key()` is the identity used to
  de-duplicate; `label()` is the citation.
- **`SearchCandidate`**: what a retriever returns: kind, content, score,
  reason, `EvidenceRef`. Retrievers never budget.
- **`BudgetLedger`**: total = output reserve + named reserves (tool results)
  + input. Required content is charged unconditionally; optional content goes
  through `tryCharge`, which refuses on the kind's cap (`category-budget`) or
  the input limit (`total-budget`).
- **`CompiledContext`**: messages, every `Outcome` (included or omitted with a
  reason), the ledger, warnings. `manifest()` keeps every field the Context
  Inspector already reads and adds `assembly {version, estimator, ledger,
  items[]}`.

## Kinds and trust

| Kind | Trust | Sent as |
| --- | --- | --- |
| instructions, tool-definitions | authoritative | provider system text / tools field |
| current-request | user-intent | last `user` message |
| conversation-history | user-intent or model-output | `user` / `assistant` messages, oldest first |
| attachment, library-evidence, conversation-recall, wiki-knowledge | untrusted | one `user` message wrapped in `<archivist-reference-material>`, before the current request |
| memory | untrusted | reserved; same envelope once produced |
| tool-result | untrusted | the provider's tool-output channel |

Trust comes from the kind, never from the source: a source cannot mark its
own content authoritative, and a stored `system` message in Chat history is
sent as a quoted `user` note. Quoted content cannot close the envelope or open
a new `<source>`. Instructions tell the model the envelope is quoted data.

## Selection

1. Charge instructions, tool definitions and the current request. They are
   never dropped; if they alone exceed the input limit the ledger reports
   `overBudget` and a warning says so.
2. Hold back `toolResultTokens` when tools are offered.
3. Turn candidates into items, ranked within their kind by score, then by
   `EvidenceRef.key()` so equal scores order the same way every run.
4. Sort all optional items by priority, kind, order, id. The newest
   `historyFloorMessages` messages get `historyFloorPriority` (85).
5. For each item, in order: empty → `empty`; outside `AccessScope` →
   `out-of-scope`; same `dedupeKey` or same normalized text as something
   included → `duplicate`; an included attachment of the same file →
   `covered-by-attachment`; history older than a message that did not fit →
   `older-than-omitted`; kind item limit → `item-limit`; per-file excerpt
   limit → `per-file-limit`; then the ledger.

Default policy (all in the v3 config, all optional except the two budgets):

| Kind | Cap (tokens) | Priority |
| --- | --- | --- |
| attachment | 8000 (4000 per file) | 90 |
| library-evidence | 6000, 8 items, 3 per file | 70 |
| conversation-history | whatever is left | 60 (newest 2: 85) |
| conversation-recall | 1500, current Chat | 40 |
| wiki-knowledge | 2000 (`wikiTokens`), 6 items (`maxWikiItems`) | 50 (`wikiPriority`) |
| tool results | 6000 reserve | — |

## Runtime integration

`ChatRunJob::prepare` routes on the Agent's compiler: v1/v2 compilers go
through `prepareCompiled` (unchanged), `context-assembly:3` through
`prepareAssembly`. Tool selection (`planTools`) depends on how many Library
excerpts reach the model, and tool definitions are part of the budget, so
`prepareAssembly` plans, assembles, and re-plans until the count settles (at
most three passes). During the tool loop each result is passed through
`admitToolResult`, which draws from the tool-result reserve and replaces an
oversized result with a JSON notice plus as much of the output as fits. The
final ledger, including tool output, is stored with the context run.

Run events, Observatory steps, streaming, cancellation, tool permissions and
edit proposals are shared with the older path.

## Hybrid Library retrieval

When the Chat's Library opted in to semantic indexing and the Agent's config
does not set `semanticRetrieval: "off"`, the runtime embeds the user's
message before `prepareAssembly` and Library evidence comes from
`ContextSources::hybridLibraryEvidence` (FTS5 + vectors, reciprocal rank
fusion, exact matches first) instead of `libraryEvidence`. Candidates keep the
same kind, source type and chunk ids, so nothing below changes; `producer` is
`library-semantic` or `library-hybrid` for vector hits, and the manifest gains
`retrieval {mode, timings, counts, policy}`. Any embedding failure falls back
to keyword search with a context warning. See
`Core/Domains/Retrieval/RETRIEVAL.md`.

## Wiki knowledge

When the Chat has a Library, `wikiTokens > 0` and `wikiRetrieval` is not
`off`, `prepareAssembly` also asks `ContextSources::wikiKnowledge` for the
Library's Living Wiki (FTS5, plus vectors when the question was embedded).
Candidates are kind `wiki-knowledge`, source type `wiki-claim` or
`wiki-page`, and never carry a Library file id, so they cannot count as
evidence for a file or cover an attachment. Their `<source>` block says what
they are: `kind="wiki-knowledge"`, the virtual `wiki-page` path, the claim
type, verification status, confidence, freshness, and the `original-sources`
(Library paths and lines) the claim rests on. Library evidence keeps the
higher priority (70 vs 50), so when the budget is tight the originals win.
Retracted and stale claims and inactive pages are never sent. The manifest
gains `wikiRetrieval` diagnostics. See `Core/Domains/Knowledge/KNOWLEDGE.md`.

## Adding a retriever (semantic search, Wiki, memory)

1. Add a function to `context_sources` that returns `SearchCandidate`s with a
   full `EvidenceRef` (stable `sourceType` + `sourceId`, Library id when it has
   one, `producer`).
2. Use an existing kind or the reserved `WikiKnowledge` / `Memory` kinds, and
   give the kind a `KindPolicy` (cap, priority, item limit) in
   `policyFromConfig` and the v3 config schema.
3. Append the candidates to `ContextRequest::candidates` in
   `prepareAssembly`. Ranking against everything else, de-duplication across
   retrievers, budgeting, rendering and the manifest need no change.

## Future integration points

- **Tokenizers**: implement `TokenEstimator` for a model and pass it to
  `assemble()` and `admitToolResult()`.
- **Access control**: `AccessScope` filters by Library today (the Chat's
  Library plus its attachments' Libraries). The identity engine will fill it
  per user and Agent.
- **Multi-agent work**: a `ContextRequest` is plain data, so an orchestrator
  can build one per agent with its own policy and scope.

## Tests

`tests/core_context_assembly_test.cpp` covers budgeting and priorities,
determinism, history contiguity, de-duplication, limits, trust boundaries,
provenance, access scope, tool-result admission, a replaceable estimator,
compatibility with the saved compilers, and the database sources.
`tests/core_ai_runtime_test.cpp` covers the runtime path end to end: legacy
shape unchanged, v3 request shape and manifest, tool-result budgeting, and
streaming with cancellation.
