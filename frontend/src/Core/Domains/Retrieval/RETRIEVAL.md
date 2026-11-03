# Semantic retrieval

Embeddings, background indexing and hybrid search for Library text. This is
shared infrastructure: Library chunks were the first source; Living Wiki
claims (`wiki-claim`) and pages (`wiki-page`) now ride the same tables,
indexer and vector store, and memories will too.

Semantic indexing is **opt-in per Library** because Library text is sent to
OpenAI. Keyword search (SQLite FTS5) never depends on it: without consent, an
API key or a network connection, retrieval is exactly what it was.

## Modules

| File | Layer | Owns |
| --- | --- | --- |
| `embedding_config.h` | Contract | `EmbeddingConfig` (model, dimensions, input version), source types and scopes |
| `vector_codec.{h,cpp}` | Storage format | Float32 little-endian BLOBs, normalization, dot product |
| `embedding_client.{h,cpp}` | Provider | `EmbeddingClient` interface; `OpenAIEmbeddingClient` (POST `/v1/embeddings` on the existing OpenAI connection) |
| `embedding_index.{h,cpp}` | Repository | Every SQL statement: vectors, sources, consent, jobs, usage, status |
| `vector_store.{h,cpp}` | Search backend | `VectorStore` interface; `ExactVectorStore` (bounded exact scan) |
| `embedding_indexer.{h,cpp}` | Scheduling | `EmbeddingIndexer`: the persistent job queue, batching, retries, budgets |
| `hybrid_retrieval.{h,cpp}` | Ranking | `HybridPolicy`, exact-term detection, reciprocal rank fusion, `searchLibrary()` |
| `semantic_index_service.{h,cpp}` | App boundary | The "ArchivistEmbeddings" thread and the `SemanticIndex` QML singleton |

Dependency direction:

```
QML SemanticIndexStrip ──► SemanticIndexService ──► EmbeddingIndexer ──► EmbeddingClient (OpenAI)
                                                          │
ai_runtime ──► ContextSources::hybridLibraryEvidence ──► hybrid_retrieval ──► LibraryRetrieval (FTS5)
       │                                                  │
       └──► OpenAIEmbeddingClient (query)                 └──► VectorStore ──► embedding_index ──► SQLite
```

Nothing in this folder depends on the UI, the AI runtime or Context Assembly.
Context Assembly depends on it through one adapter in `context_sources`.

## Storage (migration 22)

```
embedding_vectors           content_hash + model + dimensions + input_version → vector BLOB
embedding_sources           (source_type, source_id) → scope, content_hash, citation metadata
semantic_library_settings   library_id → enabled, consented_at, paused
embedding_jobs              one row per indexing run; at most one active per scope
embedding_usage             tokens and requests per UTC day
```

- **Content-addressed cache.** A vector belongs to a content hash and an
  `EmbeddingConfig`, never to a chunk. Library chunks already carry the
  SHA-256 of their text, so a moved, renamed or re-chunked chunk with the same
  text finds its vector and costs nothing. Identical text in two files is
  embedded once.
- **Sources** point a source (`library-chunk` + chunk id) at a hash and keep
  what a citation needs: scope (Library id), parent (file id), locator
  (relative path) and line range. Search reads the live chunk for the current
  path and text; the stored locator is for sources without a table of their
  own (future Wiki pages, memories).
- **Recalculation.** Changing `model`, `dimensions` or `inputVersion` (bump it
  when the text sent for a hash changes) makes every source pending again and
  garbage collection drops the old vectors.
- **Cleanup.** A trigger deletes a chunk's source row with the chunk. The
  indexer also removes sources whose file became unavailable or whose
  document is no longer indexed, then deletes vectors nothing points at.
  Disabling a Library deletes all its sources, then the unreferenced vectors.
- Vectors are stored at unit length, so cosine similarity is a dot product.
- Semantic vectors carry no confidence, feedback or authority. Ranking
  systems that decide what is trustworthy belong next to retrieval, not in
  these tables.

## Indexing

`EmbeddingIndexer` runs on its own thread with its own connection and network
manager. Requests are asynchronous, so the thread's event loop stays free and
neither the UI nor chat streams wait on it.

The queue is the database. A job row says what is happening; the work left is
always "indexed chunks of available files without a current source row and
vector", recomputed from the tables. That makes every control simple:

| Event | Effect |
| --- | --- |
| Enable | Records consent, queues a job |
| Scan finished, file saved, every 90 s | `sync()`: drop stale sources, GC, queue a job if anything is pending |
| Pause | Drops the request in flight; job `paused` (reason `user`) |
| Resume | Clears any pause or failure; job `queued` |
| Cancel | Drops the request; job `cancelled`; Library paused until Resume |
| Disable | Cancels, forgets sources, GC |
| Restart | `running` jobs go back to `queued` and continue where the tables say |

Each step reuses cached vectors for a page of pending chunks without any
request, then sends one request with up to `batchSize` distinct texts and
`maxBatchTokens` estimated tokens. When it returns, vectors and source rows are
written in one short transaction, and only for chunks whose hash is still the
one that was sent.

Safeguards (`IndexerPolicy`):

| Setting | Default | Effect |
| --- | --- | --- |
| `batchSize` | 64 | inputs per request |
| `maxBatchTokens` | 60,000 | estimated tokens per request |
| `maxConcurrentRequests` | 2 | across Libraries, at most one per Library |
| `minRequestIntervalMs` | 250 | between request starts |
| `maxAttempts` | 6 | consecutive retryable failures before `failed` |
| backoff | 2 s doubling to 5 min | honors `retry-after` |
| `maxJobTokens` | 5,000,000 | job pauses (reason `budget`) before sending past it |
| `dailyTokenBudget` | 20,000,000 | all Libraries per UTC day; pauses before sending past it |

At $0.02 per million tokens for `text-embedding-3-small`, the defaults cap a
job at about $0.10 and a day at about $0.40. Errors are classified: offline,
429 and 5xx retry with backoff; 401/403 pause (`unauthorized`); a missing key
pauses (`not-configured`); other 4xx fail the job with the message.

## Hybrid search

`searchLibrary(database, libraryId, query, queryVector, policy, limit)`:

1. Keyword candidates from the existing FTS5 search (`LibraryRetrieval`,
   unchanged, including its subject-aware reranking).
2. Semantic candidates from the `VectorStore`, scoped to this Library only,
   above `minSimilarity`.
3. Exact terms from the query (file names, identifiers with inner capitals or
   underscores, `A::b` names, backticked text), matched case-sensitively.
4. Weighted reciprocal rank fusion:
   `score = lexicalWeight/(k + lexicalRank) + semanticWeight/(k + semanticRank)`,
   plus `exactMatchBoost` for an exact file name (half for an identifier).
   The boost is far larger than any RRF score, so exact matches lead.
5. Ties break on keyword rank, then chunk id: same inputs, same order.

Without a query vector (semantic off, not indexed, offline, embedding failed)
the result is keyword search with the same shape.

### Policy

`HybridPolicy::fromConfig` reads the Agent's Context Assembly v3 config. All
optional; no settings UI yet.

| Config key | Range | Default | Meaning |
| --- | --- | --- | --- |
| `semanticRetrieval` | `auto` \| `off` | `auto` | `off` never embeds the query |
| `lexicalWeight` | 0–100 (%) | 100 | keyword signal weight |
| `semanticWeight` | 0–100 (%) | 100 | semantic signal weight; 0 turns semantic off |
| `semanticMinSimilarity` | 0–100 (%) | 20 | cosine floor for semantic candidates |

`rrfK` (60), candidate limits (24 each), `exactMatchBoost` (1.0) and
`maxScannedVectors` (200,000) are fields of `HybridPolicy` with defaults.

### Exact search first, measured

`ExactVectorStore` streams the Library's vectors from SQLite and keeps the best
`limit` in a bounded heap: memory is one row plus the results, time grows with
the Library. `maxScannedVectors` bounds the worst case and the result reports
truncation. Measured in `core-retrieval-test` (`exactScanLatency`, Release,
1536 dimensions, warm page cache, Linux container):

| Vectors | Data read | Best of 3 | Peak RSS of the test |
| --- | --- | --- | --- |
| 5,000 | 29 MB | 25 ms | ~23 MB |
| 20,000 | 117 MB | 96 ms | ~23 MB |
| 50,000 | 293 MB | 230 ms | ~23 MB |

Roughly 5 ms per thousand chunks. A Library of a few thousand chunks searches
well inside a chat turn's budget; past ~50k chunks, or when Libraries are
searched together, an ANN index or a SQLite vector extension behind
`VectorStore` is the next step. `ARCHIVIST_VECTOR_BENCH_COUNT` sets the size
for a local measurement. Every search also reports `vectorsScanned`,
`bytesScanned`, `lexicalMs`, `vectorMs` and `totalMs`, which the runtime
stores in the Context Inspector manifest (`manifest.retrieval`) and the
Observatory span `retrieval.hybrid-search`.

## Context Assembly v3

- `ChatRunJob::embedQueryThenPrepare` runs before `prepare()`. Only for the
  `context-assembly:3` compiler, a Chat with a Library, `semanticRetrieval`
  not `off`, and a Library that opted in and has vectors. It embeds the user's
  message with an 8 s timeout. Failure of any kind leaves a context warning
  ("Semantic Library search was unavailable (…), so keyword search was used.")
  and the turn continues with keyword search.
- `prepareAssembly` calls `ContextSources::hybridLibraryEvidence` when a query
  vector exists, else `libraryEvidence` as before. Both produce
  `SearchCandidate`s of kind `LibraryEvidence` with source type
  `library-chunk` and the chunk id, so de-duplication, attachment coverage,
  `AccessScope`, budgeting, trust (always untrusted, quoted reference
  material) and citations are unchanged. `producer` says who found it:
  `library-file-fts`, `library-semantic` or `library-hybrid`; `reason` names
  the signals and ranks.
- Legacy compilers (v1/v2) never embed and never see hybrid results.

## Wiki sources

A Library that opted in to semantic indexing also indexes its Living Wiki:
every active page (title and body) and every claim that still stands
(not retracted, not stale), scoped to the Library. `pendingLibrarySources`
returns chunks first, then Wiki sources; their content hash is the SHA-256 of
the embedded text, so an unchanged claim is never re-embedded, and
`removeStaleLibrarySources` drops sources whose claim or page stopped being
searchable or changed. Deleting a page or claim deletes its source row by
trigger. `KnowledgeRetrieval::search` fuses FTS5 and these vectors the same
way as Library search. See `Core/Domains/Knowledge/KNOWLEDGE.md`.

## Adding a source type (memories)

1. Pick a `SourceType` and a scope (`library` with the Library id, or a new
   scope such as a user id for personal memories).
2. Write `EmbeddingIndex::SourceText` rows for the source with its content
   hash; vectors are shared with every other source of the same text.
3. Add a pending query like `pendingLibraryChunks` (or generalize it) so the
   indexer picks the source up, and a cleanup rule for deleted sources.
4. Search with `defaultVectorStore().search()` using that source type and
   scope, and return `SearchCandidate`s of the reserved `WikiKnowledge` or
   `Memory` kinds from a `context_sources` function.

No new embedding subsystem, table or thread is needed.

## Tests

`tests/core_retrieval_test.cpp` (mock embeddings, no network): codec, exact
terms, fusion and policy, the OpenAI client against a local HTTP stand-in
(request shape, out-of-order answers, 401, 429 with retry-after, wrong
dimensions, offline, no key), opt-in and persistence, cache reuse across
identical content, moves and FTS rebuilds, incremental re-indexing,
deletion and garbage collection, configuration changes, Library isolation,
hybrid ranking with exact-match preservation, cancellation, pause, restart
recovery, retries, failures, spending limits, the Context Assembly v3 fit, the
service thread, and the scan-latency measurement.

`tests/core_ai_runtime_test.cpp` covers the runtime: v3 embeds the question
and sends a chunk found only semantically, an embeddings outage falls back to
keyword search with a warning, and saved compilers and `semanticRetrieval:
off` never embed.
