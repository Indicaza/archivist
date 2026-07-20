# Archivist

> **Status:** A native Qt 6/QML Workbench is connected to the existing Express, SQLite, filesystem, Context Compiler, Agent, and AI-provider backend. Native Library browsing, persistent Chat, paginated history, and AI responses work. Next: native Agent integration, then safe Library file reading and file-aware Chat.

Archivist is a local-first AI workspace for durable project memory, inspectable context, and safe computer work.

```text
Choose a Library
→ catalog its files
→ talk to an AI that remembers
→ retrieve only relevant evidence
→ inspect sources and work performed
→ approve, reject, or undo consequential actions
```

```text
Your files stay yours.
Chat history is durable.
Provider context is temporary and curated.
Memory, sources, tools, and consequential actions remain inspectable.
```

---

# Product Direction

Archivist should not compete with Codex or Claude Code on raw model intelligence, terminal execution, worktrees, or generic coding-agent behavior.

> Archivist is a durable, local project-memory and work-orchestration layer that can use OpenAI, Codex, Claude, and other agents as workers.

Providers supply intelligence and execution. Archivist owns:

```text
Libraries and project knowledge
Chats and decisions
explicit Context Compiler behavior
sources and provenance
Agent identities and assignments
artifacts and task outcomes
permissions and approvals
operation history
provider-independent continuity
```

A Library should become the durable world for a project, not merely a temporary folder attachment:

```text
Library
├── source files and indexed knowledge
├── Chats and decisions
├── Agents and task runs
├── pinned evidence and artifacts
└── approvals and operation history
```

The long-term opportunity is a human-readable workspace coordinating trusted AI workers without hiding context, evidence, or consequences.

---

# Runtime Direction

Electron and React established the product, backend contracts, and visual language, but large variable-height Chat histories exposed a practical rendering ceiling on weaker hardware.

The migration preserves the backend and replaces the desktop presentation layer:

```text
Qt 6 / QML
  native Workbench, Explorer, Chat, animation, desktop lifecycle
        |
C++ domain stores
  HTTP requests, client state, QML-facing models
        |
Express API
  validation, orchestration, product behavior
        |
SQLite + filesystem + AI providers
  durable state, catalog, retrieval, generation
```

The React frontend remains a behavioral and visual reference. New desktop work targets Qt unless a task explicitly concerns the legacy client.

The native client already provides fast startup, smooth variable-height Chat rendering, responsive paginated history, and substantially more animation and layout headroom.

This is a client lift-and-shift, not a backend rewrite.

---

# Development Handoff Rules

This README is the durable project seed. Literal current source always wins.

## Coding-help preferences

Optimize for low-cognitive-load, copy-paste development while Zach steers product and architecture.

```text
new paths
→ exact shell commands first
→ assume repository root unless stated otherwise

files around 500 lines or less
→ prefer complete replacements
→ label exact path and create/replace

larger or partial edits
→ unique search anchor
→ exact old block
→ complete replacement block
→ explicit end boundary
```

Prefer complete vertical slices, one build after the chunk, clear commit boundaries, minimal unrelated refactors, direct pushback on risky choices, reversible changes, and fixes grounded in current files.

Avoid vague placement, intentional red builds, scattered micro-edits, premature abstractions, long implementation lectures, and committed handoff artifacts.

## Fractal Qt architecture

```text
Feature/
├── Feature.qml
└── ChildFeature/
    ├── ChildFeature.qml
    └── NestedFeature/
        └── NestedFeature.qml
```

Rules:

- meaningful visual features own their children;
- do not flatten unrelated QML into generic folders;
- Workbench components own layout, not domain behavior;
- networking and state stay in domain stores;
- theme tokens stay centralized;
- `App.qml` stays focused on orchestration;
- unrelated work should not casually modify the Topbar.

C++ stores mirror product domains:

```text
qt/src/App/Domains/<Domain>/
├── <domain>_store.h
└── <domain>_store.cpp
```

---

# Context + Patch Workflow

```text
local repository
→ generated text context
→ coding chat
→ numbered patch
→ check, apply, build, test, commit
→ fresh context
```

Temporary root files are never committed:

```text
qt-context-012.txt
012-qt-agent-integration.patch
```

Add local ignores once:

```bash
cat >> .git/info/exclude <<'EOF_IGNORE'
qt-context-*.txt
[0-9][0-9][0-9]-*.patch
EOF_IGNORE
```

Generate context:

```bash
./scripts/qt-context
```

Include exact backend paths for the next slice:

```bash
./scripts/qt-context \
  backend/src/api/agents \
  backend/src/api/chats
```

The bundle includes branch, status, recent commits, a filtered tree, uncommitted diff, all Qt source, and requested reference paths.

Apply a returned patch:

```bash
git apply --check 012-feature-name.patch
git apply 012-feature-name.patch

git status --short
git diff --stat
```

After testing:

```bash
git add <completed-paths>
git commit -m "describe the completed vertical slice"
git status
```

Never generate a patch against an old bundle after files change.

---

# Current Product Snapshot

## Native Qt client

### Workbench

- [x] Qt 6/QML application and CMake build.
- [x] Native Topbar, Activity Rail, Explorer, Workspace, Chat dock, Artifact Drawer shell, and status bar.
- [x] Centered Workspace collision behavior around the Explorer.
- [x] Flat neutral Chat presentation.
- [x] Native hover, press, layout, and animation foundations.
- [x] One-command backend and Qt startup through `./scripts/qt-dev`.

### Libraries

- [x] Load active Libraries through the existing API.
- [x] Restore and persist selected Library.
- [x] Load cataloged files and trigger rescans.
- [x] Native expandable file tree from flat catalog records.
- [x] Filtering preserves visible ancestors.
- [x] File selection and Explorer status.

### Chats

- [x] Load persistent Chats and selected state.
- [x] Load real messages and send through `/respond`.
- [x] Optimistic user messages and temporary thinking state.
- [x] Provider and model response metadata.
- [x] Variable-height native message delegates.
- [x] Cursor-based upward history pagination.
- [x] Large initial load and aggressive history prefetch.
- [x] Visible-position preservation when older pages prepend.
- [x] Chats open at the newest message.
- [x] Reliable jump-to-latest with native `ListView` positioning.
- [x] Enter sends; Shift+Enter inserts a newline.
- [x] Misleading paginated scrollbar removed.

## Durable backend

- [x] Express 5, TypeScript, Node 24, SQLite WAL, and versioned migrations.
- [x] Persistent Libraries, scans, file catalog, Chats, messages, Agents, and selected state.
- [x] Root-constrained read-only Library scanning.
- [x] Chat CRUD, archive, restore, response, and paginated-message routes.
- [x] Persistent Agents and built-in Archivist Agent.
- [x] Agent identity, doctrine, output, generation, and Context Compiler configuration.
- [x] OpenAI behind an `AIProvider` boundary.
- [x] Provider health and dynamic model catalog.
- [x] Versioned deterministic Context Compilers.
- [x] SQLite FTS5 Chat retrieval.
- [x] Auto Balanced v2 separates evidence, conversation, and current intent.

## Legacy reference client

- [x] Electron and React remain as behavioral and visual reference.
- [x] Existing management flows remain usable during native parity work.
- [x] Unpaginated message requests remain compatible.
- [~] New desktop work should not target React by default.

---

# Current Limitations

## Native parity

- [~] Qt Library create, rename, archive, restore, and delete are incomplete.
- [~] Qt Chat create, rename, archive, restore, and delete are incomplete.
- [~] Real Agent loading, selection, and management are not connected in Qt.
- [~] Some Electron-era message actions remain presentation-only.
- [~] Artifact Drawer remains a shell.
- [~] Packaging and non-macOS verification are unfinished.

## Product work not built

- [ ] Safe Library file-content reading and Workspace preview.
- [ ] Chat attachments and pinned sources.
- [ ] Library-content chunking, FTS, and Context Compiler candidates.
- [ ] Visible source and Context Compiler manifest inspection.
- [ ] Read-only Agent tool execution trace.
- [ ] Streaming responses.
- [ ] Embeddings and semantic retrieval.
- [ ] User-approved durable memory and artifacts.
- [ ] File-writing tools, reviewable diffs, operation ledger, and revert.
- [ ] External-worker delegation and multi-Agent task orchestration.
- [ ] OS keychain credential storage.

---

# Immediate Roadmap

## 1. Native Agent integration

```text
load real Agents
→ resolve selected Chat's assigned Agent
→ show Agent name and configuration
→ change Chat-to-Agent assignment
→ create and edit Agents
→ duplicate, archive, restore, delete
```

Reuse the existing backend APIs. Do not create Qt-only Agent behavior.

## 2. Read-only Library file preview

```text
select cataloged file
→ validate Library ownership
→ resolve inside Library root
→ enforce type and size limits
→ read supported text
→ show it in the Workspace
```

First formats:

```text
.md .txt .json .ts .tsx .js .jsx .css .html .qml .cpp .h
```

Controllers must never accept arbitrary filesystem paths. Keep v1 strictly read-only.

## 3. File-aware Chat

```text
preview file
→ attach or pin approved source
→ represent excerpts as evidence
→ pass evidence through Context Compiler
→ keep current user intent last
→ show sources used
```

This is the first major slice that makes Archivist more than a fast Chat client.

## 4. Library indexing and retrieval

```text
cataloged file
→ deterministic extraction and chunks
→ SQLite metadata and FTS5
→ ranked Library evidence
→ Context Compiler candidates
```

Preserve Library ID, file ID, relative path, line or chunk range, excerpt, ranking score, and token estimate.

## 5. Visible read-only Agent tools

Start with:

```text
search_library
read_file
list_directory
search_chat_history
inspect_source
```

Execution must remain observable:

```text
Agent requests tool
→ Archivist displays request
→ tool runs within explicit scope
→ result or failure is stored
→ Agent continues
→ user can inspect the trace
```

## 6. Small swarm proof

Do not begin with a generic multi-Agent playground.

```text
one coordinator
→ two specialist Agents
→ read-only tools
→ explicit assignments
→ independent findings
→ disagreement and confidence notes
→ sourced final synthesis
```

A useful non-coding test is a modernization proposal built from architecture docs, meeting notes, Jira exports, customer feedback, and repository files. Archivist owns context, evidence, task graph, and final artifact; Codex or another provider may remain the implementation worker.

## 7. Streaming and safe mutation

Only after retrieval and tool execution are inspectable:

```text
streaming
→ real retry/edit/resend
→ durable artifacts
→ proposed file changes
→ diff and approval
→ operation ledger
→ revert
```

Do not skip from file reading to autonomous mutation.

---

# Core Architecture

Archivist is a modular monolith.

```text
Qt/QML desktop
  presentation and native interaction
        |
C++ domain stores
  HTTP requests, client state, QML models
        |
Express API
  routes, schemas, controllers
        |
Domain models and services
  persistence, invariants, orchestration
        |
SQLite + filesystem + provider adapters
```

Business behavior belongs behind the local HTTP API. QML must not duplicate backend invariants.

## Workbench ownership

```text
WorkbenchShell
├── ActivityRail
├── ExplorerDock
├── Workspace
├── ChatDock
├── ArtifactDrawer
└── StatusBar
```

The shell owns layout. Libraries, Chats, Agents, files, and artifacts supply domain content.

## Context ownership

```text
search tools → ranked ContextCandidates
selectors    → candidate sets
compiler     → merge, deduplicate, budget, order, provider messages
```

```text
Intent       = current user message
Conversation = recent real Chat history
Evidence     = retrieved reference material
```

Retrieved evidence must never masquerade as current intent.

## Domain boundaries

```text
Agent      reusable AI behavior
Chat       durable conversation assigned to an Agent
Library    user-owned filesystem root and observed project knowledge
AIProvider vendor-specific generation adapter
Artifact   durable inspectable output
ToolRun    observable scoped execution
TaskGraph  coordinated work and dependencies
```

The filesystem is authoritative for file contents. SQLite stores structured state, metadata, indexes, history, and execution records.

---

# Repository Direction

```text
Archivist/
├── backend/src/
│   ├── api/{agents,ai,appState,chats,cognition,libraries}/
│   ├── core/
│   ├── database/
│   └── middleware/
├── frontend/                  legacy React reference client
├── qt/
│   ├── CMakeLists.txt
│   ├── qml/App/{Theme,Topbar,Workbench}/
│   └── src/App/Domains/
├── scripts/
│   ├── qt-build
│   ├── qt-configure
│   ├── qt-context
│   ├── qt-dev
│   └── qt-run
└── README.md
```

Backend placement:

```text
routes       URL mapping and precedence
controllers  HTTP translation
schemas      runtime validation
models       persistence and invariants
services     multi-step coordination
core         vendor-independent contracts
providers    vendor-specific adapters
```

---

# API Direction

Base URL:

```text
http://127.0.0.1:3333/api
```

Qt currently connects to:

```text
GET    /api/health
GET    /api/app-state
PATCH  /api/app-state/selected-library
GET    /api/libraries
GET    /api/libraries/:libraryId/files
POST   /api/libraries/:libraryId/scan
GET    /api/chats
PATCH  /api/chats/selected
GET    /api/chats/:chatId/messages
POST   /api/chats/:chatId/respond
```

Existing backend domains also expose full Library, Chat, Agent, model, provider, Context Compiler, and message-search management APIs. Inspect the current routes before adding duplicates.

Paginated history:

```text
GET /api/chats/:chatId/messages?limit=160
GET /api/chats/:chatId/messages?limit=120&before=<oldest-message-id>
```

The unpaginated request remains for legacy compatibility.

---

# Development Commands

```bash
nvm use
npm install
./scripts/qt-dev
```

Qt only:

```bash
./scripts/qt-configure
./scripts/qt-build
./scripts/qt-run
```

Checks:

```bash
npm run build -w backend
npm run build
npm run lint
curl http://127.0.0.1:3333/api/health
sqlite3 backend/data/archivist.db ".tables"
```

---

# Git and PR Safety

Before a PR:

```bash
git status
git log --oneline --decorate -10
git push -u origin "$(git branch --show-current)"
```

Before deleting a branch, verify the merge reached `main`:

```bash
git fetch origin
git switch main
git pull --ff-only origin main

git log --oneline --decorate -10
git branch -r --contains <feature-tip-sha>
```

Only then:

```bash
git branch -d <merged-branch>
```

Delete the remote branch only after the PR visibly reports **Merged** and updated `main` contains the final feature tip.

---

# Feature Rhythm

```text
smallest complete behavior
→ narrow branch
→ fresh context bundle
→ one focused patch
→ UI and API verification
→ one build
→ one commit boundary
→ push
→ verify merge before cleanup
```

Good PR boundaries:

```text
native Agent integration
native Chat management
native Library management
Library file reader and preview
file-aware Chat and source inspector
Library FTS
read-only tools
small swarm proof
streaming
safe file mutation
```

---

# Product and Safety Principles

1. Local-first by default.
2. Filesystem authoritative for user files.
3. SQLite for state, history, metadata, indexes, and execution records.
4. Chat is the primary command surface.
5. Workbench is the stable visual shell.
6. History is durable; provider context is temporary.
7. Current intent stays distinct from evidence.
8. Memory and sources are inspectable and user-controlled.
9. No silent filesystem mutation.
10. Proposal before consequential action.
11. Read-only before mutation.
12. Deterministic workflows before autonomous Agents.
13. Providers are workers, not owners of project continuity.
14. Vertical slices before speculative infrastructure.
15. Complexity must pay rent.

```text
simple before clever
local before cloud
read-only before mutation
explicit before magical
human approval before destructive action
```

---

# Next Development Handoff

Working now:

```text
native Qt Workbench
real Library API and expandable file tree
real persistent Chats and AI responses
paginated native message history
smooth open-at-bottom scrolling
persistent backend Agents and cognition
```

Current migration branch:

```text
feature/qt-workbench-foundation
```

Do not delete it until the PR is verified as merged and updated `main` contains its final tip.

Recommended next branch after merge:

```text
feature/qt-agent-integration
```

First milestone:

```text
load real Agents
→ show assigned Agent
→ change Chat assignment
→ open native Agent management
```

Then:

```text
safe Library file reader
→ native preview
→ attach or pin evidence
→ file-aware response with visible sources
```

Required workflow:

```text
exact commands
fractal paths
literal current files
fresh focused context bundle
one verified patch
one build
one commit boundary
no temporary artifacts in Git
```

```text
One workspace.
Durable conversation.
Curated context.
Inspectable memory.
Observable work.
Human approval.
Local ownership.
```
