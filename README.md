# Archivist

> **Status:** The persistent Chat, Agent, Context Compiler, retrieval, Library catalog, and modular Workbench foundations are working. The next major product slice is connecting Library file contents to Chat through safe, inspectable retrieval.

Archivist is a local-first Electron AI workspace for durable conversation, file-aware retrieval, and safe computer work.

```text
Choose a folder
→ scan and understand its structure
→ talk to an AI that remembers
→ retrieve only relevant context
→ review proposed changes
→ approve, reject, or undo them
```

Core promise:

```text
Your files stay yours.
Chat history is durable.
Provider context is temporary and curated.
Memory and consequential actions remain inspectable.
```

---

# Development Handoff Rules

This README is the seed for new development chats.

## Zach's coding-help preferences

Optimize for low-cognitive-load, copy-paste development while Zach steers product and architecture.

For new files or folders:

```text
Give exact shell commands first.
Assume repository root unless stated otherwise.
Use mkdir -p and touch when helpful.
```

For files around **500 lines or less**:

```text
Prefer complete whole-file replacements.
Label the exact path and whether the file is created or replaced.
A larger paste is usually cheaper than several correction rounds.
```

For larger files or partial edits:

```text
1. Give a unique search anchor.
2. Show the exact old block.
3. Show the complete replacement block.
4. State where the replacement ends.
```

Default step shape:

```text
path
→ action
→ copy-pasteable code or command
→ verification command
```

Prefer:

- complete vertical slices;
- one build after the full chunk;
- clear commit boundaries;
- minimal unrelated refactors;
- direct professional pushback when a choice is risky;
- reversible changes;
- fixes grounded in the actual current files.

Avoid:

- vague placement instructions;
- intentional red builds;
- scattered micro-edits;
- repeated questions when the repository already answers them;
- premature abstractions;
- long architecture lectures during active implementation;
- committing generated patches, handoff bundles, or temporary context archives.

## Mandatory frontend architecture

Archivist uses a fractal feature architecture.

```text
Feature
├── Feature.tsx
├── Feature.module.css
└── ChildFeature/
    ├── ChildFeature.tsx
    ├── ChildFeature.module.css
    └── NestedFeature/
```

Rules:

- Every meaningful visual component gets its own folder.
- Component CSS stays beside the component.
- Parent features own their child components.
- Do not flatten unrelated components into one large directory.
- Shared domain API and types stay under `domains/<domain>`.
- Shared design tokens stay centralized.
- Keep `App.tsx` focused on top-level orchestration.
- Do not casually modify the Topbar while working on unrelated features.

---

# Current Product Snapshot

## Working

### Runtime and persistence

- [x] Electron + React/Vite/TypeScript desktop shell.
- [x] Express/TypeScript backend at `127.0.0.1:3333`.
- [x] Node 24 workspace with one-command development startup.
- [x] SQLite WAL mode and versioned migrations.
- [x] Persistent Libraries, Chats, messages, Agents, and selected state.
- [x] Conversations survive Electron and backend restarts.
- [x] Backend and frontend production builds pass.

### Libraries

- [x] Library create, select, rename, archive, restore, and delete flows.
- [x] Native folder selection through the Electron shell.
- [x] Read-only Library filesystem scanning.
- [x] Root-constrained traversal that rejects escaped paths.
- [x] Persistent `library_scans` history.
- [x] Persistent `library_files` metadata catalog.
- [x] Missing-file reconciliation after successful rescans.
- [x] Ignore handling for development folders such as `.git`, `node_modules`, `dist`, `build`, and `coverage`.
- [x] Navigable Library file tree in the Explorer.
- [x] File metadata search and manual rescan controls.

### Chats

- [x] Chat create, select, rename, archive, restore, and delete flows.
- [x] Chat archive and restore HTTP endpoints.
- [x] Persistent messages and selected Chat restoration.
- [x] Optimistic user-message presentation.
- [x] Real OpenAI responses behind an `AIProvider` boundary.
- [x] Active Chat ordering updates after activity.
- [x] Distinct User, Archivist, and System message presentation.
- [x] Message timestamps and delivery/status presentation.
- [x] Working copy-message controls.
- [x] Working fenced-code copy controls.
- [x] Local thumbs-up and thumbs-down response state.
- [x] Retry, edit, resend, and additional-action presentation hooks.
- [x] Dedicated empty, loading, and thinking states.
- [x] Jump-to-latest control when browsing older messages.
- [x] Responsive transcript widths and reduced-motion support.

### Agents and generation

- [x] Persistent Agents.
- [x] Built-in Archivist default Agent.
- [x] Agent create, edit, duplicate, archive, restore, and delete flows.
- [x] Agent assignment to Chats.
- [x] Agent identity configuration.
- [x] Agent profession, mission, expertise, responsibility, limitation, and success-criteria configuration.
- [x] Doctrine and system-instruction configuration.
- [x] Output-contract and verbosity configuration.
- [x] Generation configuration for provider, model, temperature, output tokens, top-p, and penalties.
- [x] Agent-owned Context Compiler selection and configuration.
- [x] Provider health endpoint.
- [x] AI model catalog and refresh endpoint.

### Context and retrieval

- [x] Context Compiler registry with persisted per-Agent compiler/config.
- [x] Generic Context Compiler Laboratory controls.
- [x] Deterministic compilers:
  - Recent History;
  - Contiguous History;
  - Turn Pairs;
  - Keyword Recency;
  - Auto Balanced v1;
  - Auto Balanced v2.
- [x] SQLite FTS5 index for completed Chat messages.
- [x] Automatic FTS backfill and insert/update/delete synchronization.
- [x] Ranked Chat retrieval with BM25, excerpts, metadata, and token estimates.
- [x] Auto Balanced v2 separates evidence from intent.
- [x] The current user message remains last in provider context.
- [x] Provider-specific translation remains inside the provider adapter.

### Workbench shell

- [x] Modular Workbench shell replacing the old monolithic Sidebar layout.
- [x] Compact activity rail.
- [x] Collapsible Explorer dock.
- [x] Libraries, archived Libraries, search, plugins, and tools Explorer modes.
- [x] Staged Explorer widths for shallow and deeply nested trees.
- [x] Unified central workspace surface.
- [x] Persistent Chat command deck.
- [x] Attached and centered Chat dock modes.
- [x] Chat dock mode adapts when the Explorer closes.
- [x] Active Chat header with Library and Agent context.
- [x] Chat management control in the dock header.
- [x] Chats and Agents browsers inside the command deck.
- [x] Full-width Workbench status bar.
- [x] Artifact drawer shell for future durable outputs.
- [x] Responsive behavior for narrow and short desktop windows.
- [x] Reduced expensive blur and layout effects.
- [x] Hover-neighbor motion and reduced-motion fallbacks.
- [x] Workbench state persisted locally.

---

# Recent Milestone: Workbench and Chat Presentation

The `feature/workbench-shell` branch turns Archivist from a collection of sidebar panels into a coherent desktop workspace.

## Workbench structure

```text
frontend/src/components/workbench/
├── ArtifactDrawer/
├── ChatDock/
│   ├── ChatDockHeader/
│   └── DockPlaceholder/
├── WorkbenchCanvas/
└── WorkbenchShell/
    ├── ActivityRail/
    ├── ExplorerDock/
    ├── StatusBar/
    ├── WorkbenchLayoutContext.tsx
    ├── WorkbenchShell.module.css
    └── WorkbenchShell.tsx
```

The shell now provides:

```text
Topbar
→ Activity Rail
→ Explorer Dock
→ Main Workspace
→ Chat Command Deck
→ Status Bar
```

The Chat command deck can either:

```text
attach to the open Explorer
```

or:

```text
center itself beneath the workspace
```

Chats and Agents no longer consume permanent right-sidebar space. They open inside the command deck only when needed.

## Chat presentation structure

```text
frontend/src/components/chat/
├── ChatWindow.tsx
├── ChatWindow.module.css
└── ChatWindow/
    ├── ChatEmptyState/
    │   ├── ChatEmptyState.tsx
    │   └── ChatEmptyState.module.css
    └── ChatMessage/
        ├── ChatMessage.tsx
        ├── ChatMessage.module.css
        ├── ChatMessageActions/
        │   ├── ChatMessageActions.tsx
        │   └── ChatMessageActions.module.css
        └── MessageContent/
            ├── MessageContent.tsx
            └── MessageContent.module.css
```

Message presentation now includes:

```text
role identity
timestamp
status
message surface
fenced code presentation
copy controls
rating controls
future action controls
```

Working now:

- copying an entire message;
- copying an individual fenced code block;
- toggling local positive or negative response ratings;
- jumping back to the latest message.

Presented but not connected to backend behavior yet:

- retry response;
- edit user message;
- resend message;
- extended message actions.

## Endpoint repair

The frontend already expected:

```text
POST /api/chats/:chatId/archive
POST /api/chats/:chatId/restore
```

The Chat model already implemented both operations, but the controller and router wiring were missing.

Those endpoints are now exposed and preserve selected-Chat fallback behavior.

## Repository cleanup

Temporary development artifacts are ignored and removed from the branch:

```text
archivist-workbench-shell-v*.patch
*-context.tar.gz
chat-presentation-context.txt
```

Do not commit handoff bundles or generated patch files to product branches.

---

# Current Limitations

- [~] Only OpenAI is wired as a generation provider.
- [~] Library scanning currently catalogs metadata; it does not parse or index file contents.
- [~] Library files cannot yet be opened, previewed, or attached to a Chat from the Workbench.
- [~] The Context Compiler can retrieve Chat history but not Library content.
- [~] Chat history loads as one list; upward pagination is not built.
- [~] Retrieval is lexical FTS only; there are no embeddings.
- [~] Retrieved Chat evidence uses individual messages rather than neighboring turns.
- [~] AI responses use request/response rather than token streaming.
- [~] Message ratings are local UI state only.
- [~] Retry, edit, resend, and more-message controls are placeholders.
- [~] The Artifact Drawer is a presentation shell without persistent artifacts.
- [~] Source and context manifests are not visible in the primary Workbench.
- [~] Compiler diagnostic logging should eventually sit behind a development flag.
- [~] Production credentials should use the OS keychain.

## Not built yet

- [ ] Library file-content extraction.
- [ ] Library-content FTS indexing and chunk retrieval.
- [ ] File previews and safe open-in-system actions.
- [ ] Chat file attachments and pinned sources.
- [ ] Library-aware Context Compiler candidates.
- [ ] Visible source and context inspector.
- [ ] Message pagination and true infinite scroll.
- [ ] Streaming Chat responses.
- [ ] Embeddings and semantic retrieval.
- [ ] User-approved durable memory.
- [ ] Durable artifact storage.
- [ ] File-writing tools.
- [ ] Human-reviewable diffs.
- [ ] Operation ledger.
- [ ] Approval and revert workflows.
- [ ] Autonomous routines or multi-Agent orchestration.

---

# Immediate Roadmap

## 1. Merge the Workbench shell

The current PR should establish the visual and structural foundation for future product work.

Before merging:

```text
build passes
→ working tree clean
→ temporary artifacts absent
→ Chat archive and restore verified
→ PR scope reviewed
```

After merging, new work should branch from updated `main`.

## 2. Library content retrieval — next major slice

The persistent Library metadata catalog exists. The next step is making those files useful to Chat without introducing mutation.

First complete vertical slice:

```text
select a cataloged file
→ safely resolve it inside the Library root
→ read supported text content
→ preview it in Archivist
→ attach or pin it to the active Chat
→ send selected excerpts through the Context Compiler
→ show the source used for the response
```

Backend direction:

```text
LibraryFileReader
→ validates catalog ownership
→ resolves the path inside the Library root
→ enforces supported types and size limits
→ returns text plus source metadata
```

Do not let controllers read arbitrary filesystem paths.

Suggested first supported formats:

```text
.md
.txt
.json
.ts
.tsx
.js
.jsx
.css
.html
```

Keep the first version read-only.

## 3. Library indexing and retrieval

After direct file preview and attachment work:

```text
cataloged file
→ extract text
→ split into deterministic chunks
→ persist chunk metadata
→ index chunks with SQLite FTS5
→ retrieve ranked Library evidence
→ feed candidates into Auto Balanced
```

Library retrieval results should include:

```text
Library ID
file ID
relative path
chunk or line range
excerpt
ranking score
estimated tokens
```

## 4. Infinite Chat history

```text
Permanent timeline = complete browsable record.
Provider context   = curated subset for one response.
```

Required behavior:

- paginate messages;
- load recent messages first;
- fetch older rows while scrolling upward;
- preserve the visible scroll anchor;
- keep retrieval independent from UI pagination;
- retain the current jump-to-latest behavior.

## 5. Visible context and sources

The user should be able to inspect what Archivist selected.

```text
response
→ source cards
→ selected excerpts
→ Context Compiler manifest
→ token-budget explanation
```

This should fit naturally into the Artifact Drawer or a dedicated inspector surface.

## 6. Streaming and message actions

After the retrieval path is stable:

- stream assistant output;
- connect retry to a new completion attempt;
- connect edit/resend to explicit Chat operations;
- persist response ratings;
- expose more-message actions only when they have real behavior.

## 7. Tools and safe mutation

```text
read-only retrieval
→ visible sources
→ proposed file changes
→ diff and approval
→ operation ledger
→ revert
```

Do not skip directly from file reading to autonomous mutation.

---

# Core Architecture

## Runtime

```text
Electron shell
  native dialogs, narrow IPC, lifecycle
        |
React frontend
  Workbench, Chat, Explorer, management modals
        |
Express API
  routes, validation, controllers
        |
Domain services and models
  Libraries, Chats, Agents, cognition, completion
        |
SQLite + filesystem + provider adapters
```

Archivist is a modular monolith.

Use Electron IPC only for desktop-shell capabilities. Business behavior belongs behind the local HTTP API.

## Workbench architecture

```text
WorkbenchShell
├── ActivityRail
├── ExplorerDock
├── WorkbenchCanvas
├── ChatDock
├── ArtifactDrawer
└── StatusBar
```

The Workbench shell owns layout behavior.

Domain features such as Libraries, Chats, Agents, files, and artifacts supply content to those layout surfaces.

Do not put Library or Chat business logic inside generic Workbench components.

## Context architecture

```text
Search tools
→ return ranked ContextCandidates

Selectors and orchestrators
→ call one or more search tools

Context Compiler
→ merges
→ deduplicates
→ budgets
→ orders
→ builds provider messages
```

Important distinction:

```text
Intent       = final current user message
Conversation = recent real user/assistant history
Evidence     = retrieved reference material
```

Auto Balanced v2 assembles:

```text
retrieved evidence system envelope
→ recent contiguous conversation
→ current user message last
```

Retrieved old messages or Library excerpts must never masquerade as current user intent.

## Agent boundary

Agents own reusable AI behavior.

```text
Agent
├── identity
├── profession
├── doctrine
├── output contract
├── system instructions
├── generation config
└── context compiler config
```

Chats reference Agents.

The Chat completion path should resolve behavior from the assigned Agent rather than accumulating vendor or prompt settings directly on the Chat.

## Provider boundary

```text
ChatCompletionService
→ AIProvider interface
→ OpenAIProvider today
→ more providers later
```

Controllers must not call provider SDKs directly.

Provider-specific request translation belongs in the provider adapter.

## Library boundary

```text
filesystem
→ authoritative user content

Library scan
→ observes metadata

Library catalog
→ persists known file state

Library reader
→ safely reads approved content

Library retrieval
→ selects relevant excerpts

File tools
→ propose reviewed mutations later
```

The metadata catalog is not the source of truth for file contents. The filesystem remains authoritative.

---

# Product and Safety Principles

1. Local-first by default.
2. Filesystem is authoritative for user files.
3. SQLite stores structured state, history, metadata, and indexes.
4. Chat is the primary command surface.
5. The Workbench is the stable visual shell around Chat.
6. History is durable; provider context is temporary.
7. Current intent must remain distinct from retrieved evidence.
8. Meaningful memory should be inspectable and user-controlled.
9. No silent filesystem mutation.
10. Archive or deprecate before destructive removal.
11. Proposal before consequential action.
12. Deterministic workflows before autonomous Agents.
13. Read-only access before mutation.
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

# Technical Foundation

```text
Desktop:     Electron
Frontend:    React 19 + Vite + TypeScript
Backend:     Node 24 + Express 5 + TypeScript
Validation:  Zod
Database:    SQLite through better-sqlite3
Search:      SQLite FTS5
AI:          OpenAI provider adapter
Styling:     co-located CSS + centralized theme tokens
```

Key backend areas:

```text
backend/src/
├── api/
│   ├── agents/
│   ├── ai/
│   ├── appState/
│   ├── chats/
│   ├── cognition/
│   │   ├── contextCompilers/
│   │   └── contextRetrieval/
│   └── libraries/
├── core/
│   ├── ai/
│   └── cognition/
│       ├── conscious/context/
│       │   ├── compilers/
│       │   └── utilities/
│       └── retrieval/
├── database/
└── middleware/
```

Backend placement rules:

```text
routes       URL mapping and precedence
controllers  HTTP translation
schemas      runtime validation
models       persistence and invariants
services     multi-step coordination
core         vendor-independent contracts
providers    vendor-specific adapters
```

Fixed collection routes must precede parameter routes:

```text
/archived
→ /selected
→ /:id
```

Action routes must be registered explicitly:

```text
/:id/archive
/:id/restore
/:id/duplicate
/:id/respond
```

---

# Core Data Direction

```text
libraries
  id, name, description?, root_path, archived_at?, timestamps

library_scans
  id, library_id, status, counts, errors, timestamps

library_files
  id, library_id, relative_path, name, extension,
  size_bytes, modified_at, status, last_seen_scan_id,
  last_seen_at, timestamps

agents
  id, name, description, identity, profession,
  doctrine, output contract, system instructions,
  generation config, context config, built-in flag,
  archived_at, timestamps

chats
  id, library_id?, title, type, agent_id,
  archived_at, timestamps

messages
  id, chat_id, role, content, status, timestamps

message_search
  FTS5 mirror of completed non-empty messages

app_settings
  selected_library_id?, selected_chat_id?
```

`library_id = null` means a standalone Chat.

Future:

```text
library_file_chunks
library_file_search
chat attachments
pinned sources
approved memories
artifacts
operation ledger
```

---

# API Surface

Development base URL:

```text
http://127.0.0.1:3333/api
```

## Health

```text
GET /api/health
```

## Libraries

```text
GET    /api/libraries
GET    /api/libraries/archived
POST   /api/libraries
GET    /api/libraries/:libraryId
PATCH  /api/libraries/:libraryId
POST   /api/libraries/:libraryId/archive
POST   /api/libraries/:libraryId/restore
GET    /api/libraries/:libraryId/files
POST   /api/libraries/:libraryId/scan
```

## Chats

```text
GET    /api/chats
GET    /api/chats/archived
POST   /api/chats
PATCH  /api/chats/selected
GET    /api/chats/:chatId
PATCH  /api/chats/:chatId
DELETE /api/chats/:chatId
POST   /api/chats/:chatId/archive
POST   /api/chats/:chatId/restore
GET    /api/chats/:chatId/messages
POST   /api/chats/:chatId/messages
POST   /api/chats/:chatId/respond
```

## Agents

```text
GET    /api/agents
GET    /api/agents/archived
POST   /api/agents
GET    /api/agents/:agentId
PATCH  /api/agents/:agentId
DELETE /api/agents/:agentId
POST   /api/agents/:agentId/duplicate
POST   /api/agents/:agentId/archive
POST   /api/agents/:agentId/restore
```

## AI providers and models

```text
GET  /api/ai/models
POST /api/ai/models/refresh
GET  /api/ai/providers
```

## Context and retrieval

```text
GET /api/cognition/context-compilers
GET /api/cognition/search/messages
```

---

# Development Commands

From the repository root:

```bash
nvm use
npm install
npm run dev
```

Individual processes:

```bash
npm run dev:backend
npm run dev:frontend
npm run dev:electron
```

Build:

```bash
npm run build
npm run build -w backend
npm run build -w frontend
```

Lint:

```bash
npm run lint
```

Useful checks:

```bash
curl http://127.0.0.1:3333/api/health

curl http://127.0.0.1:3333/api/agents

curl http://127.0.0.1:3333/api/ai/models

curl http://127.0.0.1:3333/api/cognition/context-compilers

curl --get http://127.0.0.1:3333/api/cognition/search/messages \
  --data-urlencode "q=obsidian compass"

sqlite3 backend/data/archivist.db ".tables"

sqlite3 backend/data/archivist.db "PRAGMA user_version;"

sqlite3 backend/data/archivist.db \
  "SELECT count(*) FROM message_search;"
```

`SIGINT` and Electron lifecycle errors after `Ctrl+C` are normal shutdown noise.

## Normal branch push

```bash
git status

npm run build

git add -A

git commit -m "describe the completed vertical slice"

git push -u origin "$(git branch --show-current)"
```

## Push another commit to the current branch

```bash
git status

git add -A

git commit -m "describe the follow-up"

git push
```

## Documentation-only push

```bash
git add README.md

git commit -m "docs: update Archivist project handoff"

git push origin feature/workbench-shell
```

## Confirm the branch is clean

```bash
git status

git log --oneline -5
```

Expected:

```text
nothing to commit, working tree clean
```

## After the PR is merged

```bash
git switch main

git pull origin main

git branch -d feature/workbench-shell
```

Delete the remote branch only after confirming the merge:

```bash
git push origin --delete feature/workbench-shell
```

---

# Feature Rhythm

```text
define the smallest complete behavior
→ create a narrow branch
→ establish exact paths
→ implement the full chunk
→ verify through UI and API
→ run one build
→ commit and push
→ merge before the next large slice
```

Avoid mixing unrelated product slices into one PR.

Examples of separate PRs:

```text
Workbench shell
Library content reader
Library FTS indexing
Infinite Chat pagination
Streaming responses
File mutation tools
```

---

# Seed for the Next Development Chat

Archivist is a local-first Electron AI workspace.

Working now:

- persistent Libraries, Chats, messages, Agents, and selected state;
- real OpenAI Chat through an `AIProvider` boundary;
- Agent builder with identity, profession, doctrine, output, generation, and Context Compiler configuration;
- AI provider health and model discovery;
- versioned Context Compiler framework and compiler laboratory;
- deterministic history compilers;
- SQLite FTS5 Chat retrieval;
- Auto Balanced v2, which separates retrieved evidence from current intent;
- persistent read-only Library file scans and metadata catalog;
- navigable Library Explorer tree;
- modular Workbench shell with activity rail, Explorer, Chat command deck, Artifact Drawer shell, and status bar;
- polished Chat message presentation and copy controls;
- Chat archive and restore routes;
- backend and frontend builds pass.

The Workbench PR is on:

```text
feature/workbench-shell
```

After it merges, work from updated `main`.

Next milestone:

```text
Library content retrieval and file-aware Chat
```

Suggested sequence:

1. Inspect the Library scanner, file catalog model/types, Library routes, file tree, ContextCandidate types, Context Compiler, and Chat completion flow.
2. Add a root-constrained `LibraryFileReader`.
3. Add a safe endpoint for reading one cataloged supported text file.
4. Add a Workbench file-preview surface.
5. Allow the user to pin or attach a file to the active Chat.
6. Represent attached excerpts as explicit evidence candidates.
7. Feed them through Auto Balanced without confusing evidence with current intent.
8. Show the file path and excerpt used for the answer.
9. Keep the entire first slice read-only.
10. Follow with deterministic file chunking and Library FTS indexing.

Out of scope for that first slice:

```text
file mutation
embeddings
autonomous tools
multi-Agent orchestration
background swarms
silent memory
image generation
large visual redesign
```

Required working style:

- exact commands for new paths;
- fractal component architecture;
- whole-file replacements around 500 lines or less;
- exact search anchors and old/new blocks for partial edits;
- complete copy-pasteable chunks;
- one build after the chunk;
- direct pushback on risky architecture;
- preserve unrelated working behavior;
- never commit temporary patches or context bundles.

```text
One workspace.
Durable conversation.
Curated context.
Inspectable memory.
Observable work.
Human approval.
Local ownership.
```
