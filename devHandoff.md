# Archivist Development Seed

This is the primary handoff note for restarting development in a fresh coding chat. Keep it accurate, compact, and biased toward what someone needs to make the next correct change.

Treat the literal repository and a freshly generated context bundle as authoritative when this file becomes stale.

## Current Product Goal

Ship a fast, local-first desktop application where a user can:

```text
select a Collection
→ restore the complete task workspace
→ move among code, documentation, assets, and research Libraries
→ open files and Chats as durable tabs
→ talk to persistent Agents
→ retrieve or attach trusted project evidence
→ inspect the exact context used
→ return later without rebuilding the session
```

The daily-driver path works end to end.

Archivist now combines a durable context pipeline with persistent Collection workspaces. Collections preserve the tabs, layout, active Library, Library-tree viewport, Chats, and Agents needed to resume a task quickly.

The next priority is rich file rendering and a shared icon system. The first slice should make Markdown genuinely pleasant to read while establishing a renderer registry that can grow into images, structured data, PDFs, 3D assets, diffs, and other file types.

## Current Stack

- Qt 6.8+ and QML native desktop frontend
- C++ domain stores exposed to QML
- Express 5 and TypeScript backend
- SQLite WAL persistence and versioned migrations
- QSettings-backed local workspace and viewport state
- local filesystem as the authority for user file contents
- provider abstraction with OpenAI currently connected
- deterministic, versioned Context Compilers
- durable per-response context-run records
- deterministic Library text extraction and stable chunk persistence
- SQLite FTS5 lexical retrieval with file and line provenance
- automatic active-Library retrieval during Chat completion
- root npm scripts supervising the complete native development session

## Current Product State

Update this section after every meaningful coding session.

### Working now

- native Qt/QML Workbench, Explorer, Workspace, Chat dock, status bar, and right-side Context Inspector;
- reference-based Collections that group multiple Libraries, Chats, and Agent configuration into task-focused workspaces;
- Collection-scoped navigation with no global `All Work` Library view;
- persistent file and Chat tabs with polished drag-and-drop reordering;
- one embedded Qt WebEngine IDE host containing Monaco and xterm.js;
- Archivist-owned editor commands and context menu with Go to Definition plus standard clipboard actions;
- workspace-scoped language-server supervision with one reusable process per server and workspace;
- TypeScript/JavaScript language intelligence for `.ts`, `.js`, `.tsx`, and `.jsx`, including React and Next.js projects;
- QML, C/C++, Rust, Python, Go, YAML, Bash, and Markdown language-server adapters with graceful missing-tool fallback;
- Monaco-native HTML, CSS, SCSS, Less, and JSON services;
- SQL highlighting and generic completion isolated to SQL models so SQL never attaches to TypeScript files;
- pinned npm language tooling in `scripts/language-tools.json` with install, update, doctor, and version commands;
- structured language telemetry and one-command diagnostics for classifications, capabilities, markers, processes, and failures;
- `language-test-lab/` fixtures for cross-file navigation and intentional diagnostic smoke tests;
- per-Collection tab order, active tab, Explorer width and visibility, active activity view, Chat dock height, and dock attachment state;
- live Collection switching that waits for the new Collection scope before restoring active files and Chats;
- multi-Library Collections for separate code, documentation, assets, research, and design roots;
- per-Collection restoration of the last active Library;
- per-Library restoration of expanded folders, selected node, filter text, stable viewport anchor, and scroll position;
- Chat sidebar population when Collection scope finishes loading rather than only after Chat mutation;
- persistent Libraries and root-constrained Library scanning;
- safe read-only preview for supported UTF-8 text and source files;
- persistent Chats, selected state, and cursor-based message pagination;
- persistent Chat-to-Agent rosters and default Agent behavior;
- native Collection, Chat, and Agent management flows;
- persistent Library-file attachments scoped to an individual Chat;
- attached evidence passes through the selected Context Compiler;
- durable context-run snapshots linked to generated assistant messages;
- native Context Inspector showing compiler, provider, model, token accounting, warnings, retrieval mode, and source outcomes;
- supported Library text extracted into deterministic chunks with exact line provenance;
- incremental rescanning through document and chunk hashes;
- SQLite FTS5 search across chunk content, filenames, and relative paths;
- normal Chat requests automatically search the active Library;
- explicit attachments remain stronger than automatically retrieved evidence;
- `.obsidian`, `.git`, `node_modules`, build output, coverage, and other non-project directories are ignored during scans;
- root `npm run dev` owns one backend and one Qt application session;
- root `npm run build` builds the backend and native Qt client;
- development startup validates Node 24 and `better-sqlite3`;
- legacy Electron/React development remains available through explicit `:legacy` scripts.

### Workspace state contract

The local desktop state is keyed by durable IDs:

```text
Collection ID
├── editor tabs and active tab
├── Explorer shell state
├── Chat dock state
├── last active Library ID
└── Library ID
    ├── expanded folder IDs
    ├── selected node and path
    ├── filter text
    ├── stable visible-node viewport anchor
    └── raw scroll offset fallback
```

Collections may reference several Libraries, but the Explorer displays one active Library tree at a time.

Switching Collections must:

```text
save current Collection state
→ change Collection
→ wait for Collection scope
→ restore target Collection shell and tabs
→ select its last Library
→ wait for that Library catalog
→ restore the Library tree and active content
```

Do not restore a file, Chat, or tree against stale scope data.

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

Automatic retrieval searches the active Library inside the active Collection. It does not yet search every Library in the Collection or bind a Chat permanently to one or more Libraries.

Files are authoritative on disk. SQLite stores product data, catalogs, attachment relationships, document hashes, chunks, search indexes, and context records. QSettings stores local workspace presentation state.

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

Finish and merge the current language-support PR before adding more adapters.

```text
run the final checks
→ smoke-test the small language fixtures
→ inspect one diagnostic snapshot
→ stage only the intended implementation and documentation
→ commit, push, and open the PR
```

### Immediate goals

1. **Verify the branch**
   - run `nvm use`;
   - run `npm run check:language-support`;
   - run `npm run build`;
   - launch with `npm run dev`;
   - open representative valid and intentionally broken fixtures.

2. **Inspect language health**
   - run `npm run diagnose:language-support`;
   - confirm expected Monaco and LSP language IDs;
   - confirm valid TSX/JSX returns zero diagnostics;
   - confirm intentional errors produce owned Monaco markers;
   - treat missing optional Go support as non-blocking.

3. **Keep the PR narrow**
   - include the editor command boundary, language-server supervision, managed tools, telemetry, fixtures, and documentation;
   - exclude generated `target/`, build output, diagnostic dumps, context bundles, and numbered patches;
   - do not add framework-specific, Unreal, Godot, debugger, or Blueprint systems before merge.

### Known debt

- the editor menu currently exposes only Go to Definition and standard clipboard actions;
- Find References, Rename, Quick Fix, Format, multiple-definition selection, and peek views still need UI surfaces;
- JSX editing now parses and validates correctly, but completion and auto-closing deserve focused testing in real React projects;
- SQL completion is generic and isolated to SQL files; it is not connected to live database schemas;
- `clangd` quality depends on a correct compilation database or compile flags;
- Unreal projects need a future adapter for compilation commands, generated headers, reflection data, and assets;
- Godot GDScript requires a future TCP transport to Godot's project-scoped language server;
- Go remains optional until the local Go toolchain can install a compatible `gopls`;
- language-server sessions are bounded in memory and process count but do not yet have an idle-time eviction policy;
- client telemetry is local, in-memory, bounded, and diagnostic-only; it resets with the backend.

### After merge

1. **Capability-driven editor commands**
   - Find References;
   - Rename Symbol;
   - Quick Fix and code actions;
   - Format Document and Selection;
   - multiple-definition and reference result pickers.

2. **Targeted language quality**
   - test real React and Next.js repositories;
   - improve JSX completion only from observed failures;
   - add project-aware C++ and Unreal setup when needed;
   - add additional languages only when a real project requires them.

3. **Return to product work**
   - keep language plumbing stable;
   - continue the broader Archivist workspace, renderer, Git, Agent, and deployment roadmap on fresh branches.

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

sqlite3 backend/data/archivist.db   "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;"
```

Do not trust a hard-coded schema version in this handoff. Compare `PRAGMA user_version` with the current migration set before diagnosing persistence.

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
- Collection IDs define the current workspace boundary;
- Library IDs define independent Explorer-tree state inside a Collection;
- worktree identity must join file and tab identity before worktree views ship;
- UI restoration must wait for the relevant Collection or Library catalog rather than racing stale data;
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
./scripts/qt-context   <backend paths needed for the next slice>   <Qt paths needed for the next slice>
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

Generated files must be ignored through committed repository or fixture-level `.gitignore` rules. Do not use `.git/info/exclude`; hidden local exclusions make PR contents harder to audit and reproduce.

## Current PR Boundary

The current coherent milestone is **workspace-scoped language support inside the embedded Archivist IDE**:

```text
open a supported source file
→ classify its Monaco and LSP language identities
→ resolve the nearest project workspace
→ reuse or start the correct language service
→ expose completion, hover, navigation, diagnostics, and advertised capabilities
→ preserve normal editing when an optional tool is unavailable
→ capture enough local telemetry to diagnose failures
```

The PR should contain only the coherent language-support slice:

```text
README.md
devHandoff.md
package.json
package-lock.json
backend/src/api/languageSupport/**
frontend/qml/App/Workbench/IdeHost/Web/MonacoEditor/**
language-test-lab/**
scripts/diagnose-language-support.mjs
scripts/language-tools.json
scripts/language-tools.mjs
.gitignore changes directly related to generated language-test output
```

Do not include:

```text
numbered patch files
generated context bundles
language-support diagnostic dumps
language-test-lab compiler output
Rust target directories
screenshots or unrelated UI work
framework-specific adapters
Unreal, Godot, debugger, Blueprint, or deployment systems
```

Suggested PR title:

```text
feat: add workspace-scoped language support
```

Suggested commit shape:

```text
feat: add workspace-scoped language support
chore: add language tooling diagnostics and test fixtures
```

A single commit is acceptable if the branch is already one coherent uncommitted slice.

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
