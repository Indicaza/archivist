# Archivist Development Seed

This is the primary handoff note for restarting development in a fresh coding chat. Keep it accurate, compact, and biased toward what someone needs to make the next correct change.

Treat the literal repository and a freshly generated context bundle as authoritative when this file becomes stale.

## Current Product Goal

Ship a fast, local-first desktop application where a user can:

```text
create or select a Library
→ inspect its files safely
→ maintain a durable Chat
→ attach trusted project evidence
→ talk to a persistent Agent
→ inspect the exact sources and context used
→ retrieve relevant project memory automatically
→ safely run useful tools
```

The daily-driver path works end to end.

Archivist now has a functional, persistent, inspectable, and automatically retrieved context pipeline. A normal Chat request can search the selected Library, pass bounded excerpts through the Context Compiler, answer from project evidence, and expose every source in the Context Inspector.

The next priority is retrieval demo hardening: latency visibility, automatic index freshness, evidence quality, and a repeatable showcase flow. Autonomous mutation remains out of scope.

## Current Stack

- Qt 6.8+ and QML native desktop frontend
- C++ domain stores exposed to QML
- Express 5 and TypeScript backend
- SQLite WAL persistence and versioned migrations
- local filesystem as the authority for user file contents
- provider abstraction with OpenAI currently connected
- deterministic, versioned Context Compilers
- durable per-response context-run records
- deterministic Library text extraction and stable chunk persistence
- SQLite FTS5 lexical retrieval with file and line provenance
- automatic selected-Library retrieval during Chat completion
- root npm scripts supervising the complete native development session

## Current Product State

Update this section after every meaningful coding session.

### Working now

- native Qt/QML Workbench, Explorer, Workspace, Chat dock, status bar, and right-side Context Inspector;
- persistent Libraries and selected-Library restoration;
- root-constrained Library scanning and expandable native file tree;
- safe read-only preview for supported UTF-8 text and source files;
- persistent Chats, selected state, and cursor-based message pagination;
- real provider responses through assigned persistent Agents;
- native Chat and Agent create, edit, archive, restore, duplicate, and delete flows;
- persistent Chat-to-Agent assignment;
- persistent Library-file attachments scoped to an individual Chat;
- attach and detach controls from the native file preview;
- visible source count, empty state, removable source chips, and attach/detach confirmation;
- attached evidence passes through the selected Context Compiler;
- durable context-run snapshots linked to generated assistant messages;
- native Context button on completed assistant responses;
- native Context Inspector showing compiler, provider, model, token accounting, warnings, retrieval mode, and source outcomes;
- source outcomes distinguish Included, Truncated, Omitted, Unavailable, and Failed states;
- attached and automatically retrieved sources can be reopened from the inspector;
- old responses without a stored context run fail honestly instead of inventing provenance;
- context persistence failures do not invalidate an otherwise successful assistant response;
- supported Library text is extracted into deterministic chunks with exact start and end lines;
- document and chunk hashes make rescans incremental and preserve stable identities for unchanged content;
- changed files replace stale chunks and missing files lose stale searchable content;
- SQLite FTS5 searches chunk content, filenames, and relative paths;
- the Library index smoke test scans twice, verifies reuse, searches a known term, and prints one copyable result block;
- normal Chat requests automatically search the currently selected Library;
- automatic retrieval excludes files already explicitly attached to the Chat;
- retrieved excerpts are bounded, labeled, and passed through the selected Context Compiler;
- explicit attachments remain closer to the user message and receive fallback protection when a compiler omits them;
- automatically retrieved evidence remains inspectable rather than silently entering the provider request;
- `.obsidian`, `.git`, `node_modules`, build output, coverage, and other non-project directories are ignored during scans;
- SQLite schema version 13 contains context-run, document, chunk, and FTS5 persistence;
- backend startup logs the active database path and index readiness;
- root `npm run dev` owns one backend and one Qt application session;
- root `npm run build` builds the backend and native Qt client;
- `npm run dev:stop` cleans up the previous managed backend process tree;
- development startup validates Node 24 and `better-sqlite3`;
- stale Archivist listeners on port `3333` are cleaned up safely;
- unrelated applications using port `3333` are reported rather than killed;
- legacy Electron/React development remains available through explicit `:legacy` scripts.

### File-aware Chat and retrieval limits

Current deliberate limits:

```text
maximum attached files per Chat: 8
maximum attachment evidence budget per turn: about 8,000 tokens
maximum contribution per attached file: about 4,000 tokens
maximum automatic search candidates: 16
maximum automatically selected excerpts: 8
maximum automatically selected excerpts per file: 3
maximum automatic retrieval evidence budget: about 6,000 tokens
```

Automatic retrieval uses the globally selected Library from persisted app state. It does not currently bind a Chat permanently to a Library.

Files are authoritative on disk. SQLite stores catalog state, attachment relationships, document hashes, chunks, search indexes, context records, and structured application state.

Explicit attachments are excluded from automatic retrieval and remain the stronger evidence channel.

### Context Inspector contract

A stored context run belongs to one generated assistant message.

It records:

```text
Chat and assistant-message identity
provider, model, and Agent
Context Compiler ID and version
input budget and response reserve
estimated tokens used
included and omitted message counts
compiler duration
compiler warnings
source identity and path
source outcome
estimated and included token contribution
reason for truncation, omission, unavailability, or failure
```

Current source outcomes:

```text
Included
Truncated
Omitted
Unavailable
Failed
```

Context records are immutable inspection snapshots.

They describe what happened during a response rather than recalculating history from current files that may have changed.

## Next Milestone

The next branch should be:

```text
feature/retrieval-demo-hardening
```

The next coherent milestone is to make the working automatic-retrieval loop fast, fresh, obvious, and dependable enough to demonstrate repeatedly:

```text
ask a normal project question
→ know whether the selected Library index is ready
→ retrieve a small high-quality evidence set
→ expose retrieval and provider timing
→ answer without a long unexplained pause
→ inspect and reopen the evidence
```

### Immediate goals

1. **Measure the actual slow path**
   - log or persist indexing, FTS search, evidence assembly, Context Compiler, provider request, response persistence, and native rendering duration separately;
   - do not optimize SQLite retrieval merely because the complete turn feels slow;
   - make one copyable diagnostic block for tired testing.

2. **Make index freshness automatic and obvious**
   - stop requiring the user to remember a manual rescan before a demo;
   - expose whether the selected Library is unindexed, indexing, fresh, stale, partial, or failed;
   - choose a small safe policy such as lazy first-use indexing or background indexing after Library selection;
   - never block every Chat turn on a full filesystem crawl.

3. **Tighten evidence selection**
   - reduce redundant chunks from the same file;
   - prefer the strongest direct source before related mentions;
   - tune total excerpt count and token budget using observed answers rather than arbitrary expansion;
   - preserve explicit attachments as the strongest evidence channel;
   - keep every accepted and rejected candidate inspectable.

4. **Improve visible retrieval state**
   - show a lightweight state such as searching, compiling context, waiting for provider, and complete;
   - make it obvious that Archivist is searching the Library rather than appearing frozen;
   - do not turn the Chat surface into a debug dashboard.

5. **Create a repeatable demo set**
   - use a few known Shard questions with expected primary files;
   - test direct lookup, multi-file synthesis, no-match behavior, explicit attachment priority, and source reopening;
   - keep the test flow one command or a tiny checklist.

### Known debt

- the complete turn currently feels slow, but the measured bottleneck has not yet been isolated;
- automatic retrieval depends on the selected Library having already been scanned;
- search is lexical SQLite FTS5, not semantic embedding retrieval;
- broad prompts may select too many related excerpts;
- Chat-to-Library ownership is implicit through global selected state rather than a durable Chat relationship;
- automatic retrieval has no user-facing enable/disable or profile controls yet;
- retrieval runs synchronously inside Chat completion;
- source cards can become visually dense when many candidates are recorded.

### After demo hardening

1. **Manual native Library search**
   - expose the existing search API in the Workbench;
   - inspect ranked matches without sending an AI request;
   - open the exact file and line range.

2. **Observable read-only Agent tools**
   - `search_library`;
   - `read_file`;
   - `list_directory`;
   - `search_chat_history`;
   - visible request, result, failure, timing, and source trace.

3. **Retrieval quality expansion only when earned**
   - phrase and field weighting improvements;
   - hybrid lexical and embedding retrieval if lexical search demonstrates a concrete miss;
   - per-Library retrieval profiles;
   - durable Chat-to-Library relationships when the product workflow requires them.

4. **Only after inspection and read-only tools are dependable**
   - durable artifacts;
   - proposed file changes;
   - reviewable diffs;
   - operation ledger;
   - revert.

### Explicitly not now

- autonomous filesystem mutation;
- embeddings added merely because they are fashionable;
- generic multi-Agent playgrounds;
- large plugin marketplaces;
- broad social or business integrations;
- replacing mature creative applications.

## Root Development Workflow

The normal development interface lives in the repository root.

### Build everything

```bash
npm run build
```

This performs:

```text
Node 24 validation
→ backend TypeScript build
→ Qt configuration when needed
→ native Qt build
```

### Run everything

```bash
npm run dev
```

This performs:

```text
validate Node 24
→ validate better-sqlite3
→ stop the previous managed backend process tree
→ clean up stale Archivist listeners on port 3333
→ start one backend
→ wait for the health endpoint
→ build Qt
→ launch Archivist
→ clean up the backend when Qt exits
```

The full application runs in one terminal.

Closing Archivist or pressing `Ctrl+C` should also stop the managed backend.

### Stop a broken or abandoned session

```bash
npm run dev:stop
```

The managed PID file lives at:

```text
backend/data/runtime/qt-dev-backend.pid
```

The cleanup script stops the complete npm, Node, and `tsx` backend process tree instead of killing only one wrapper process.

It may stop a stale process listening on port `3333` only when that process belongs to this Archivist repository. It must not kill an unrelated application.

### Focused native commands

```bash
npm run dev:qt
npm run build:qt
npm run qt:configure
npm run qt:run
```

Use these only when debugging a specific Qt build or launch stage.

### Legacy reference client

```bash
npm run dev:legacy
npm run build:legacy
```

Electron/React is no longer the default application workflow. It remains a behavioral and visual reference client.

## Important Runtime Lessons

### Use the repository Node version

The project targets Node 24 LTS.

```bash
nvm use
node -v
```

`better-sqlite3` is a native module compiled for a specific Node ABI.

After changing Node versions:

```bash
npm rebuild better-sqlite3
```

A common failure looks like:

```text
ERR_DLOPEN_FAILED
better-sqlite3 was compiled for a different Node version
```

Do not debug application code until the native module loads successfully.

### Prefer the managed workflow

Do not routinely start the backend manually in another terminal.

Use:

```bash
npm run dev
```

This prevents an old backend from serving stale code or preventing a migration from executing.

When the session appears confused:

```bash
npm run dev:stop
npm run dev
```

### Verify process ownership

```bash
lsof -nP -iTCP:3333 -sTCP:LISTEN
cat backend/data/runtime/qt-dev-backend.pid 2>/dev/null || true
curl http://127.0.0.1:3333/api/health
```

After Archivist exits normally, this should return no listener:

```bash
lsof -nP -iTCP:3333 -sTCP:LISTEN
```

### Verify SQLite state

Do not delete `backend/data/archivist.db` to fix a migration problem.

Inspect and repair migrations instead:

```bash
sqlite3 backend/data/archivist.db "PRAGMA user_version;"

sqlite3 backend/data/archivist.db \
  "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;"
```

The automatic retrieval milestone expects schema version 13 with context-run, Library document, chunk, and FTS5 persistence available.

## Architecture Rules

Archivist follows a fractal, domain-first architecture:

```text
domain
├── UI or presentation
├── application behavior
├── data and persistence
├── contracts and validation
└── tests
```

The exact names may change by language or layer, but ownership should remain obvious.

### Required preferences

- organize by domain or feature before technical file type;
- meaningful visual QML features own their nested children;
- Workbench components own layout, not backend behavior;
- C++ domain stores own HTTP requests, client state, and QML-facing models;
- backend domains own validation, persistence, invariants, and orchestration;
- do not duplicate backend rules in QML;
- co-locate tightly related QML, TypeScript, schemas, types, tests, and helpers;
- avoid giant global `components`, `utils`, `services`, or `types` junk drawers;
- shared code must earn its shared status through real reuse;
- provider-specific behavior remains behind provider interfaces;
- context evidence must never masquerade as current user intent;
- inspection records describe historical decisions and should not be recomputed from mutable files;
- retrieval must preserve file and line provenance;
- automatically retrieved evidence must remain distinct from explicit attachments;
- explicit attachments must remain the strongest user-controlled evidence channel;
- retrieval failures or no-match results must not prevent ordinary Chat completion;
- index freshness must be observable rather than silently assumed;
- tool execution, approvals, provenance, and cost belong to Core;
- plugins may contribute workflows but may not bypass Core governance.

### Change test

Before adding a file, ask:

1. Which domain owns it?
2. Is it private to that domain or intentionally shared?
3. Does the folder tree still explain the product?
4. Can the complete feature slice be located without searching the whole repository?
5. Is the behavior enforced in the correct layer?

If the answer is unclear, the boundary is probably wrong.

## Coding Assistance Contract

The user steers architecture and product decisions.

The coding assistant should optimize for safe, low-cognitive-load execution.

### Preferred working rhythm

```text
finish one coherent vertical slice
→ run npm run build
→ verify through npm run dev
→ commit
→ push and merge a narrow PR
→ update main
→ delete the merged branch
→ create the next branch
→ generate a fresh focused context bundle
→ return one numbered patch
```

Do not continue unrelated feature work on a branch whose PR boundary is already coherent.

### Generate context

From the Archivist repository root:

```bash
./scripts/qt-context \
  <backend paths needed for the next slice> \
  <Qt paths needed for the next slice>
```

The generated bundle includes:

- current branch and Git status;
- recent commits;
- repository tree;
- uncommitted diff;
- root `package.json`;
- Qt build, run, stop, and context scripts;
- all Qt source;
- explicitly requested backend and reference paths.

Always generate a fresh bundle after files change.

Never create a patch against an old context.

### Deliver changes as patch files

Preferred coding-chat response:

1. a downloadable, numbered root-level `.patch`;
2. exact apply commands;
3. exact build and verification commands;
4. concise behavior and test flow;
5. commit command and next context command when appropriate.

Apply:

```bash
git apply --check 001-feature-name.patch
git apply 001-feature-name.patch

git status --short
git diff --stat
```

Do not suggest `git apply --3way` for generated patches unless the patch is known to contain valid repository ancestor blobs.

### Verification rhythm

```bash
nvm use
npm run build
npm run dev
```

Never claim a build or test passed unless it was actually run.

### Patch expectations

- preserve the fractal/domain architecture;
- make the smallest coherent vertical slice;
- use literal current files and paths from the bundle;
- include new files, deletions, renames, and mode changes;
- avoid unrelated formatting churn;
- validate patch application against an exact clean reconstruction when possible;
- state assumptions that could not be verified;
- provide rollback guidance when a change is risky;
- keep temporary context and patch artifacts out of Git.

Local-only ignores:

```bash
cat >> .git/info/exclude <<'EOF_IGNORE'
qt-context-*.txt
[0-9][0-9][0-9]-*.patch
EOF_IGNORE
```

## Current PR Boundary

The current coherent milestone is **deterministic Library indexing plus automatic, inspectable Chat retrieval**:

```text
safe Library catalog
→ deterministic text extraction
→ stable chunks with line provenance
→ incremental SQLite FTS5 index
→ smoke-tested lexical search
→ automatic selected-Library retrieval
→ bounded evidence through Context Compiler
→ explicit attachment priority
→ durable source outcomes
→ native Context Inspector
→ reopen authoritative evidence
```

The branch also ignores `.obsidian` and other non-project directories so plugin bundles and generated files do not pollute the knowledge index.

Do not add retrieval performance work, automatic index freshness, manual native search, read-only Agent tools, embeddings, or filesystem mutation before merging this PR.

Suggested PR title:

```text
feat: add automatic Library retrieval
```

Suggested next branch after merge:

```text
feature/retrieval-demo-hardening
```

## Keeping This Seed Useful

After meaningful work, update only what changed:

- Working now
- current limits
- Next
- root development workflow
- important architectural decisions
- runtime and migration lessons
- verification commands
- known breakage or debt
- current PR boundary and recommended next branch

Do not turn this into a source-code mirror.

The context script supplies the live tree and selected files.
