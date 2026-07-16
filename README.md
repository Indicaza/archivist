# Archivist

> **Current status:** The persistent Library-management vertical slice is complete. The next milestone is the sidebar ownership refactor, persisted Library and standalone chats, messages, and one real streamed AI provider.

Archivist is a local-first desktop AI workspace that helps people find, understand, organize, create, and safely change the things on their computer.

For a normal user, it should feel simple: choose a folder, ask questions in chat, search for something, approve a proposed change, and undo it if needed.

For a technical user, Archivist is a modular Electron application with a React frontend, local Express API, SQLite persistence, AI provider adapters, searchable Library context, structured tool events, human-reviewed diffs, and eventually local or cloud model routing.

The central idea is:

```text
Your files stay yours.
Chat is the main interface.
The AI receives only the context it needs.
Meaningful changes are visible, reviewable, and reversible.
```

Archivist is not another disposable-chat client. It is intended to become a durable workspace where Libraries, conversations, generated artifacts, decisions, and AI work remain organized over time.

---

# Development Snapshot

## Completed

- [x] Root workspace starts the backend, Vite frontend, and Electron from one command.
- [x] Node 24 project policy and workspace setup.
- [x] Electron-only development launch; the browser does not open automatically.
- [x] React chat-workspace shell with top bar, sidebar, chat area, composer, and theme tokens.
- [x] Animated collapsible sidebar and responsive Library cards.
- [x] Native Electron folder picker through preload/IPC.
- [x] SQLite connection, WAL mode, and versioned migrations.
- [x] Persistent `libraries`, `chats`, and `app_settings` tables.
- [x] Unique normalized root path for each Library.
- [x] Automatic `Main Timeline` chat row when a Library is created.
- [x] Persistent selected Library across restarts.
- [x] Library API with create, list, read, edit, archive, and restore operations.
- [x] Frontend API integration; Libraries no longer come from mock React state.
- [x] Library rename and description editing.
- [x] Reversible Library archiving.
- [x] Archived Libraries section with restore controls.
- [x] Automatic fallback selection when the active Library is archived.
- [x] Duplicate-folder protection.
- [x] Library-management modal with safe archive confirmation.
- [x] UI performance pass: removed costly continuous effects, simplified hover animation, and changed the Library shimmer to a one-shot click effect.
- [x] Archiving never deletes or changes the user’s actual folder.

## Partially complete

- [~] Chat schema exists, but chat CRUD, message persistence, and real chat selection are not wired.
- [~] Each Library owns a `Main Timeline` database row, but the sidebar still renders a separate mock Chats section instead of nesting chats beneath the selected Library.
- [~] Standalone chats are supported by the nullable `library_id` design, but their top-level sidebar home is not wired.
- [~] Sidebar Library search works locally, but unified cross-domain search does not exist.
- [~] The visual shell is strong, but final UI polish, accessibility, and large-data testing remain ongoing.

## Not built yet

- [ ] Message storage and persistent conversation history.
- [ ] AI provider connection and streaming responses.
- [ ] Context compiler.
- [ ] File scanner, parser, catalog, and retrieval.
- [ ] Read-only filesystem explorer.
- [ ] Live filesystem watcher.
- [ ] Nested Collections and drag-and-drop organization.
- [ ] Unified search and command palette.
- [ ] AI tool execution, progress events, diffs, approval, and revert.
- [ ] `.archivist/` AI wiki generation.
- [ ] Vector search and model routing.
- [ ] Rich artifact blocks, creative engines, routines, and exports.

---

# What to Build Next

The roadmap is ordered to reach a useful product quickly without overbuilding hypothetical infrastructure.

## 1. Sidebar Ownership, Persisted Chats, and Messages — next

- Replace the separate mock Chats section with the real navigation structure.
- Keep inactive Libraries compact; expand the selected Library in place.
- Show the selected Library’s chats beneath it, with `Main Timeline` first and `+ New Chat` last.
- Keep standalone chats in their own top-level section because they have no Library owner.
- Add chat creation, rename, selection, and archive.
- Add a `messages` table and message API.
- Persist both `selected_library_id` and `selected_chat_id`.
- Load recent messages from SQLite and restore the same conversation after restart.
- Preserve scroll position and add upward pagination later.

**Definition of done:**

```text
Select Library
→ open Main Timeline
→ send and store a message
→ restart Archivist
→ continue from the same conversation
```

## 2. Connect One Real AI Provider

Start with one provider behind an adapter instead of coupling chat directly to a vendor.

- OpenAI first.
- User-provided API key stored through the OS credential system.
- Stream assistant responses.
- Persist user and assistant messages.
- Keep a mock provider for tests.
- Design the interface so Anthropic, Ollama, LM Studio, and image providers can follow.

**Definition of done:**

```text
Select Library
→ open persistent timeline
→ send message
→ receive streamed response
→ restart and retain both sides
```

## 3. Context Compiler v1

The user sees one durable timeline, but models receive a small temporary context packet.

Initial packet:

- Active Library metadata.
- Active chat purpose.
- Recent messages.
- Explicitly attached text or files.
- Provider and privacy information.

Later packet:

- AI wiki.
- Relevant file chunks.
- Relevant old-chat summaries.
- Active profile.
- Tool and permission constraints.

Core rule:

```text
Permanent timeline
+ compact durable memory
+ recent conversation
+ retrieved relevant history
+ selected files
= temporary model context
```

## 4. Basic Read-Only Filesystem Explorer

Build a useful explorer before giving the AI mutation powers.

- Lazy-load folders.
- Show files and directories beneath the selected Library.
- Ignore `.git`, `node_modules`, generated output, caches, and large unsupported binaries.
- Reveal in Finder and copy paths.
- Manual refresh first.
- Keep the disk as the source of truth.

This first version is an observability surface, not a file-management engine.

## 5. Scanner, Catalog, and Search

- Scan supported files.
- Store paths, metadata, timestamps, hashes, and parse status.
- Add text extraction and chunking.
- Use SQLite FTS5 for durable full-text search.
- Add semantic retrieval later through a swappable local vector adapter.
- Show why a result matched and where it came from.

## 6. Collections, Favorites, and Drag-and-Drop Navigation

Collections are renameable organizational folders inside Archivist. They reference Libraries and chats without owning, duplicating, or moving their underlying data.

- Collections within Collections, with rename, archive, and manual sibling ordering.
- Library and chat references; the same item may appear in multiple Collections.
- `Favorites` as a built-in Collection or saved reference view using the same underlying rules.
- Optimistic drag-and-drop with backend persistence and rollback on failure.
- Typed domain operations such as move Collection, reorder siblings, add reference, and remove reference.
- Resizable sidebar, roughly 240–600px, with the chosen width persisted in app settings.
- Expand all parent nodes and reveal a result when navigating from search.

Preferred UI candidate: **React Arborist** for virtualization, keyboard navigation, inline rename, selection, nesting, and drag-and-drop. Do not hide all behavior behind one magical generic tree endpoint.

## 7. Live Filesystem and AI Work Visualization

- Watch Library folders for changes.
- Batch noisy filesystem events.
- Update only affected tree branches.
- Show planned, queued, writing, completed, and failed states.
- Render the same structured work events in the sidebar, chat, and activity view.

Example:

```text
Build Library endpoints
├── routes/LibraryRoutes.ts       complete
├── controllers/LibraryController.ts  writing
├── models/Library.ts             queued
└── npm run build                 queued
```

## 8. Proposal, Diff, Approval, and Revert

The AI should propose consequential actions rather than silently applying them.

- Proposed file create/update/move operations.
- Before/after diff.
- Affected paths and reasoning.
- Approve all, approve selected, edit, reject, or save for later.
- Operation ledger.
- Easy undo.
- Git-aware rollback for code Libraries.
- Snapshot or backup strategy for ordinary files.
- No user-file deletion in the normal flow.

## 9. AI Wiki and Durable Library Memory

Each Library eventually gains a human-readable `.archivist/` folder.

Start compactly:

```text
.archivist/
  soul.md
  structure.md
  decisions.md
  glossary.md
  policies.json
  ignore.md
```

Add style, taste, art direction, deprecated history, profiles, and exports only as real features need them.

Chat is the stream.  
The wiki is distilled memory.  
SQLite is the structured ledger.  
The filesystem is the source of truth.

## 10. Rich Tools and Multi-Model Workspace

After the foundation works:

- Rich message blocks.
- Code and file artifacts.
- Images and generated media.
- AI profiles.
- Tool registry.
- Deterministic workflows.
- Multiple providers.
- Creative engines.
- Daily briefs and optional routines.
- Codex/read mode and export.
- Specialized modules without splitting into separate applications.

---

# Product Model

## Library

A Library is a user-selected root folder registered with Archivist.

A Library:

- Has a unique normalized `root_path`.
- Owns zero or more chats.
- Receives one automatic `Main Timeline`.
- Can be renamed without renaming the folder.
- Can be archived and restored.
- Does not currently create `.archivist/`.
- Does not give Archivist permission to mutate its files.

Archiving only changes Archivist state. It preserves the database row, chats, metadata, future indexes, and user folder.

Permanent deletion should eventually exist behind a serious Danger Zone. It must explain exactly which Archivist data and generated `.archivist/` files will be erased. Deleting user content must remain a separate and extremely explicit action.

## Chat

A chat belongs to zero or one Library.

- Library chat: durable conversation inside a domain.
- Standalone chat: allowed through nullable `library_id`.
- `Main Timeline`: one per Library.
- Additional chats: focused threads that still benefit from Library context.

Long term, users should not need to constantly create disposable chats. The visible timeline may be effectively infinite while the context compiler selects only what the model needs.

## Collection

A Collection is an organizational reference tree inside Archivist.

A Collection may contain:

- Nested Collections.
- Library references.
- Chat references.
- Later: profiles, tools, workflows, artifacts, and saved searches.

Collections do not own Libraries or chats. The same item may appear in multiple Collections.

Ownership remains explicit:

```text
Library → Chat
Real ownership and inherited Library context

Collection → Library or Chat
Reference, ordering, and organization only
```

Dragging a Library or chat between Collections must not change the chat’s `library_id`, move user files, or duplicate domain records.

Planned schema:

```text
collections
  id
  parent_collection_id nullable
  name
  position
  archived_at nullable
  created_at
  updated_at

collection_items
  id
  collection_id
  item_type
  item_id
  position
  created_at
```

## Sidebar Navigation Contract

The sidebar is the map of the workspace; chat remains the command surface.

```text
Search Archivist…

Collections
└── Favorites / user-created folders

Libraries
└── Selected Library
    ├── Chats
    │   ├── Main Timeline
    │   ├── Focused chat
    │   └── + New Chat
    └── Files                 later

Standalone Chats
Archived Libraries
Profiles / Tools              later
```

Rules:

- Inactive Libraries remain compact; selecting one expands it in place.
- A Library chat has one canonical owner and inherits that Library’s context.
- A standalone chat has `library_id = null` and lives in the top-level Standalone Chats section.
- Collections and Favorites display references, not alternate ownership.
- Renaming a Library never renames its folder; moving a reference never moves files.
- The same visual tree primitives may be reused across navigation, filesystem, and live work views, but their data models remain separate.

## Three Different Trees

Do not combine these into one magical table merely because they look similar in the UI.

### 1. Navigation tree

User organization:

```text
Collections
└── Current Work
    └── Archivist
        ├── Main Timeline
        └── Architecture
```

### 2. Filesystem tree

Actual Library disk contents:

```text
Archivist/
├── frontend/
├── backend/
└── README.md
```

### 3. Work/execution tree

What an AI or workflow is doing:

```text
Implement chat API
├── Add migration       complete
├── Add repository      working
├── Add routes          queued
└── Run build           queued
```

They can share visual tree components, but they have different persistence, permissions, and meaning.

---

# Search and Navigation

The intended search experience is simple enough for a casual computer user and powerful enough for development work.

## Visible search

A debounced sidebar search should eventually span:

- Collections.
- Libraries.
- Chats.
- Files.
- Artifacts.
- Profiles.
- Tools and actions.

Results should be grouped by domain and explain their location. Selecting a result should navigate to the correct Library or chat, expand every parent node, reveal and select the target, and open the appropriate central view.

## Command palette

`Cmd+K` should use the same underlying index for both finding and acting.

Examples:

```text
Find
  Open Library
  Find chat
  Find file
  Search document contents

Act
  Create Collection
  Start chat
  Rename current item
  Scan Library
  Reveal in Finder

Ask
  Ask Archivist to find...
```

Preferred progression:

1. Fuse.js for small in-memory navigation metadata.
2. `cmdk` for an accessible command/search palette.
3. SQLite FTS5 for large persistent content search.
4. Local vectors for semantic retrieval.

---

# Human Experience

Archivist should use progressive disclosure rather than separate “simple” and “expert” modes.

At the surface:

```text
Chat
Search
Folders
Approve
Review
Undo
```

Underneath:

```text
Context retrieval
Provider routing
File parsing
Tool execution
Diffs
Work runs
Permissions
Indexing
Event streams
```

An older user should be able to ask:

> Find the document my daughter sent me about the lake house.

A developer should be able to ask:

> Add the archive endpoints, show the proposed patch, run the build, and let me approve the changes.

Both follow the same interaction:

```text
User asks
→ Archivist explains the plan
→ shows sources or affected items
→ requests approval when needed
→ performs observable work
→ shows the result
→ offers revert
```

The AI is the primary interface. Traditional navigation exists to make state understandable, inspectable, and manually controllable.

---

# Memory Model

```text
Filesystem = source of truth for user files
AI Wiki = portable human-readable meaning
SQLite = records, relationships, history, and state
Full-text/vector indexes = retrieval accelerators
Chat = ongoing stream
Operation ledger = proof and undo
```

## Chat memory tiers

```text
Hot
  Recent messages used directly.

Warm
  Older messages paginated from SQLite.

Cold
  Older conversation summarized and searchable.

Durable
  User-approved conclusions promoted into wiki memory.
```

The full timeline is never sent blindly to a model.

---

# Safety and Trust Rules

1. Local-first by default.
2. The user’s filesystem remains authoritative.
3. No silent filesystem mutation.
4. No user-file deletion in the normal workflow.
5. Meaningful changes require review and approval.
6. Archive and deprecate before destructive removal.
7. Show what context may leave the machine.
8. Store cloud credentials in the OS keychain, not project files.
9. Log applied operations.
10. Make revert understandable and easy.
11. AI profiles may propose actions but do not bypass policy.
12. Complexity must pay rent.

The user should always be able to answer:

```text
What changed?
Why?
What information was used?
Who approved it?
Can I undo it?
```

---

# Current Technical Foundation

## Stack

```text
Desktop:    Electron 42
Frontend:   React 19 + Vite + TypeScript
Backend:    Node 24 + Express 5 + TypeScript
Validation: Zod 4
Database:   SQLite via better-sqlite3
Styling:    CSS Modules + centralized theme tokens
```

Planned:

```text
Tree UI:             React Arborist
Navigation search:   Fuse.js
Command palette:     cmdk
Content search:      SQLite FTS5
Semantic retrieval:  swappable local vector adapter
Cloud AI:            provider adapters
Local AI:            Ollama / LM Studio adapters later
```

## Run locally

From the repository root:

```bash
nvm use
npm install
npm run dev
```

The root development command starts:

```text
Express API
Vite dev server
Electron desktop window
```

Electron is the target environment. Vite provides HMR inside the Electron window.

## Architecture

```text
Electron shell
  native dialogs, narrow IPC, app lifecycle
        |
React frontend
  UI state, navigation, chat, review surfaces
        |
Local Express API on 127.0.0.1
  thin HTTP controllers and validation
        |
Domain models/services
  Library, chat, app state, future tools
        |
SQLite + filesystem + future indexes/providers
```

Use Electron IPC only for desktop-shell capabilities such as folder selection, reveal/open actions, notifications, and app lifecycle. Business behavior belongs behind the local API.

The application is a modular monolith, not a microservice system.

## Backend domain structure

```text
backend/src/
  api/
    libraries/
      controllers/
      models/
      routes/
      schemas/
      types/
    chats/
      controllers/
      models/
      routes/
      schemas/
      types/
    appState/
      controllers/
      models/
      routes/
      schemas/
      types/
  database/
  errors/
  middleware/
  app.ts
  server.ts
  index.ts
```

Routes map URLs.  
Controllers handle HTTP.  
Schemas validate runtime input.  
Models own domain persistence and behavior.

## Current SQLite schema

### `libraries`

```text
id
name
description nullable
root_path unique
archived_at nullable
created_at
updated_at
```

### `chats`

```text
id
library_id nullable
title
type: main | standard
created_at
updated_at
```

There is a unique partial index allowing only one `main` chat per Library.

### `app_settings`

```text
singleton id = 1
selected_library_id nullable
created_at
updated_at
```

Archiving an active Library selects a sensible active fallback or `null`.

## Current API

```text
GET    /api/health

GET    /api/libraries
GET    /api/libraries/archived
POST   /api/libraries
GET    /api/libraries/:libraryId
PATCH  /api/libraries/:libraryId
POST   /api/libraries/:libraryId/archive
POST   /api/libraries/:libraryId/restore

GET    /api/app-state
PATCH  /api/app-state/selected-library
```

Chat and message endpoints are the next backend slice.

---

# Planned Live Work Architecture

AI tools and filesystem watchers should produce small structured events rather than directly controlling UI components.

Example events:

```text
tool.started
directory.planned
directory.created
file.planned
file.created
file.updated
command.started
command.completed
step.failed
run.completed
```

Planned records:

```text
work_runs
  id
  library_id
  chat_id
  title
  status
  started_at
  completed_at

work_events
  id
  run_id
  sequence
  event_type
  payload_json
  created_at
```

Store current state separately from event history:

```text
Current state answers: What exists now?
Events answer: What happened and what is still running?
```

The same event stream can power:

- Compact sidebar progress.
- A larger tree inside chat.
- Activity history.
- Notifications.
- Approval and operation records.

---

# Tool and Workflow Direction

Do not begin with autonomous agent swarms.

Use a deterministic progression:

```text
Tools
→ steps
→ workflows
→ runs
→ optional routines
```

A tool should declare:

- Typed input and output.
- Required capabilities.
- Risk level.
- Whether approval is required.
- A handler.
- Inspectable results.

AI profiles reason and propose. A policy-aware executor performs approved operations.

---

# UI Direction

The shell should feel:

- Calm.
- Tactile.
- Clear.
- Slightly magical.
- Trustworthy.
- Familiar to anyone who has used folders, search, and chat.

Animation matters, but the app needs a performance budget:

- Prefer `transform` and `opacity`.
- Use painted transitions briefly.
- Avoid many continuous gradients, filters, masks, and giant shadows.
- Test on older hardware.
- Keep accessibility and reduced motion support.
- Allow the sidebar to become resizable as trees deepen.

The goal is not a generic SaaS dashboard or an effects demo. The UI should make complex AI work feel understandable.

Product rule:

```text
Chat is the command surface.
The tree is the map, state display, and progress indicator.
```

---

# MVP Boundary

## MVP should prove

- A folder can become a persistent Library.
- A Library has a persistent conversation.
- One real AI provider can answer inside that Library.
- Archivist can retrieve relevant local information.
- The user can see sources and context.
- Proposed changes can be reviewed.
- Applied changes can be reverted.
- The app remains understandable after restarting.

## MVP should not attempt

- Fully autonomous agents.
- Silent file management.
- Cloud sync and collaboration.
- A public plugin marketplace.
- Every file format.
- Every AI provider.
- Full semantic version control for arbitrary files.
- UE5 automation.
- Multi-user permissions.
- Scheduled autonomous routines before manual workflows are trustworthy.

---

# Long-Term Modules

These should reuse the same Library, context, tool, proposal, and artifact systems rather than become separate chat applications.

- Developer workspace and Code Forge.
- Writer and worldbuilding tools.
- Character and AI profile chamber.
- Image studio and art-direction memory.
- Codex/read mode and polished exports.
- Research and startup workbench.
- Personal document finder.
- Daily brief and novelty engine.
- Local/private model workbench.
- Game-development and UE5 bridges.
- Optional momentum-planning tools.

Product mantra:

```text
One workspace.
Many models.
Durable context.
Observable work.
Human approval.
Local ownership.
```

---

# Seed for the Next Development Chat

Use this section to orient a new implementation session quickly.

## Current state

The Library persistence and management vertical slice is complete:

- SQLite migrations.
- Library creation through the Electron folder picker.
- Persistent selected Library.
- Rename and description editing.
- Archive and restore.
- Active and archived sidebar sections.
- Safe fallback selection.
- Electron-first UI and API wiring.

## Next milestone

Refactor the sidebar ownership model, then implement persistent chats and messages.

Milestone navigation contract:

```text
Libraries
└── Selected Library
    ├── Main Timeline
    ├── Focused chats
    └── + New Chat

Standalone Chats
└── Chats with library_id = null
```

Suggested sequence:

1. Review the current `chats` schema and remove the mock sibling-only behavior from the frontend `Chats` component.
2. Add `selected_chat_id`, chat archive support, and the message migration.
3. Add chat CRUD, Library-nested listing, standalone-chat listing, and message endpoints.
4. Expand only the selected Library and load its chats beneath its Library row.
5. Keep standalone chats in a separate top-level section.
6. Restore the selected Library and selected chat from `app_settings`.
7. Load recent messages into `ChatWindow` and persist user messages.
8. Connect one provider adapter, then stream and persist assistant messages.
9. Verify restart continuity and fallback behavior when chats or Libraries are archived.

## Explicitly out of scope for that milestone

- Collections, Favorites, and drag-and-drop persistence.
- Filesystem tree and full indexing.
- Vector database.
- File mutation.
- AI wiki generation.
- Tool execution.
- Multi-provider routing.
- Final infinite-scroll summarization.

Preserve the future ownership rules now, but build the smallest complete product loop first. Do not prematurely implement a universal tree model while only Library chats and standalone chats are real.
