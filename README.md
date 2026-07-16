# Archivist

> **Current status:** The persistent AI chat vertical slice works. Libraries, standalone Chats, selected state, and messages survive restarts. The frontend sends real turns through the backend to OpenAI and renders the saved conversation. Next: deliberate context assembly, inspectable user/chat memory, and paginated infinite history. Library retrieval follows after the chat foundation is polished.

Archivist is a local-first desktop AI workspace for finding, understanding, organizing, creating, and safely changing the things on a computer.

```text
Choose a folder
→ talk to an AI that remembers
→ find useful information
→ review proposed changes
→ approve, reject, or undo them
```

Archivist is not intended to be another disposable-chat client. It should become a durable workspace where Libraries, conversations, preferences, decisions, sources, artifacts, and AI work remain understandable over time.

Core promise:

```text
Your files stay yours.
Chat is the main interface.
Conversation history is durable.
The model receives only the context it needs.
Meaningful changes are visible, reviewable, and reversible.
```

---

# Development Handoff Rules

This README is both the project north star and the seed for new development chats. Read this section before suggesting implementation work.

## How Zach prefers coding help

Optimize for **ergonomic, low-cognitive-load, copy-and-paste development** while Zach steers product and architectural decisions. Assume he may be tired, on the couch, or watching television with family. Reduce navigation and synthesis work without lowering architectural standards.

### Output contract

For new files, folders, or structure:

```text
Give exact shell commands first.
Assume commands run from the repository root unless stated otherwise.
Use commands to establish exact names and paths.
```

For files around 300 lines or less:

```text
Prefer complete whole-file replacements.
Include the exact path and label the action: create or replace.
A larger paste is often cheaper than several correction rounds.
```

For larger files or partial edits:

```text
1. Give a unique search anchor.
2. Show the exact old block.
3. Show the complete replacement block.
4. State clearly where the replacement ends.
```

For each step, provide:

```text
path → action → copy-pasteable code/command → verification command
```

Prefer one small milestone, one or two files at a time, complete vertical slices, exact commands, manual verification, and clear commit/PR boundaries. Give direct professional pushback when a decision is risky or unlike experienced production practice.

Avoid vague placement instructions, scattered edits, unnecessary homework, repeated questions, unrelated refactors, premature abstractions, agent swarms, and long architecture lectures during active implementation.

Collaboration model:

```text
Zach chooses direction
→ assistant finds the smallest sound implementation
→ assistant carries the navigation and copy-paste burden
→ Zach runs it and steers the next move
```

“Assume I am lazy” means instructions should carry as much cognitive load as possible. It does not mean accepting sloppy architecture.

---

# Development Snapshot

## Working now

- [x] Root npm workspace; Node 24 policy; one command starts Express, Vite, and Electron.
- [x] Electron-first React chat shell with top bar, sidebar, ChatWindow, sticky composer, and theme tokens.
- [x] Native folder picker through Electron preload/IPC.
- [x] SQLite WAL mode and versioned migrations.
- [x] Persistent Libraries with unique normalized root paths.
- [x] Library create, select, rename, description, archive, restore, and fallback selection.
- [x] Persistent standalone Chats with selection, rename, management, and deletion flows.
- [x] Persistent selected Library and selected Chat across restarts.
- [x] Persistent user, assistant, and system messages.
- [x] Conversations load from SQLite whenever a Chat is selected.
- [x] Real OpenAI provider behind an `AIProvider` interface.
- [x] Environment-based development configuration and provider smoke test.
- [x] `/respond` saves the user message, loads history, calls OpenAI, saves the assistant response, and returns both.
- [x] Frontend optimistic user message, assistant “Thinking…” state, and canonical saved replacement.
- [x] Real conversations survive Electron/backend restarts.
- [x] Shared `SidebarCard` behavior for Chats and Libraries.
- [x] Normalized active/archived modals, hover, press, and shimmer behavior.
- [x] Centralized visual tokens and reduced duplicated sidebar styling.
- [x] Static routes such as `/archived` are declared before `/:chatId`.
- [x] Backend and frontend TypeScript builds pass.

## Current limitations

- [~] The working Chat experience is primarily standalone; Library-owned Chat navigation is not yet the active UI model.
- [~] Library `Main Timeline` rows exist in the schema direction, but are not yet the polished default experience.
- [~] The model receives ordinary conversation history without a mature budget or retrieval policy.
- [~] Chat loads as one list; upward pagination and true infinite history are not built.
- [~] OpenAI uses normal request/response, not token streaming.
- [~] Accessibility, large-list behavior, and final UI polish remain ongoing.
- [~] Development credentials use environment variables; production should use the OS keychain.

## Not built yet

- [ ] Context Compiler and visible context manifest.
- [ ] Global user preferences and approved long-term memory.
- [ ] Chat-specific purpose, decisions, and memory.
- [ ] Memory propose/approve/edit/reject UI.
- [ ] Message pagination, scroll anchoring, summaries, and historical retrieval.
- [ ] Library attachment/inherited context in active Chat navigation.
- [ ] Filesystem explorer, scanner, parser, catalog, FTS, or vectors.
- [ ] Sources/context inspector and streamed AI output.
- [ ] AI wiki, tools, diffs, operation ledger, approval, and revert.
- [ ] Collections, Favorites, drag-and-drop, and command palette.

---

# Immediate Roadmap

Build a trustworthy chat intelligence system before attaching thousands of files.

## 1. Context Compiler — next

Create one deterministic service that decides what is sent to the provider.

```text
System instructions
+ global user preferences
+ chat purpose and memory
+ recent conversation window
+ explicitly attached context
= temporary provider request
```

The first version should be simple, inspectable, and easy to debug. Do not begin with swarms or autonomous context agents.

**Done when:** a message passes through `ContextCompiler`, the packet can be inspected, the provider receives it, and persistence still works.

## 2. Global and Chat memory

Initial scopes:

```text
Global memory
  Stable preferences useful across Archivist.

Chat memory
  Goals, decisions, terminology, and behavior for one conversation.
```

Preferred trust model:

```text
AI notices a durable memory candidate
→ proposes it
→ user approves, edits, rejects, or marks it temporary
→ approved memory becomes inspectable durable state
```

Do not silently maintain a magical hidden personality file.

## 3. Infinite conversation history

Separate the permanent timeline from provider context:

- Add paginated message queries.
- Load recent messages first.
- Fetch older messages when scrolling upward.
- Preserve the visible scroll anchor when inserting old rows.
- Keep recent messages hot; summarize/retrieve older history later.

```text
Conversation history = complete permanent record the user can browse.
Model context        = curated subset used for one response.
```

## 4. Attach Chats to Libraries

After context and memory boundaries are sound:

- Expose `Main Timeline` as the default Library conversation.
- Let focused Chats inherit optional Library metadata and retrieval rules.
- Preserve standalone Chats for general work.
- Make active Library context explicit rather than accidental.

## 5. Read-only Library retrieval

- Lazy filesystem explorer and ignore rules.
- File metadata catalog, extraction, and chunking.
- SQLite FTS5 for durable keyword search.
- Source cards explaining why context was selected.
- Semantic retrieval later through a swappable local vector adapter.

Later MVP progression:

```text
read-only retrieval
→ visible context sources
→ proposed file changes
→ diff and approval
→ operation ledger
→ revert
```

---

# Product and Architecture Principles

1. Local-first by default.
2. The filesystem is authoritative for user files.
3. SQLite stores structured state and history.
4. Chat is the primary command surface; navigation makes state visible.
5. Conversation history is durable; provider context is temporary and curated.
6. Meaningful memory is inspectable and user-controlled.
7. No silent filesystem mutation or normal-flow user-file deletion.
8. Archive/deprecate before destructive removal.
9. Proposal before consequential operation.
10. Deterministic workflows before autonomous agents.
11. One modular monolith before multiple services or runtimes.
12. Shared components and centralized tokens before duplicated behavior.
13. Vertical slices before speculative infrastructure.
14. Complexity must pay rent.

```text
simple before clever
local before cloud
read-only before mutation
explicit before magical
human approval before destructive action
```

---

# Current Technical Foundation

## Stack

```text
Desktop:     Electron
Frontend:    React 19 + Vite + TypeScript
Backend:     Node 24 + Express 5 + TypeScript
Validation:  Zod
Database:    SQLite through better-sqlite3
AI:          OpenAI provider adapter
Styling:     CSS Modules + centralized theme tokens
```

Planned only when a real feature needs them:

```text
Content search:       SQLite FTS5
Navigation search:    Fuse.js or equivalent
Command palette:      cmdk or equivalent
Tree UI:              evaluate against real nested data
Semantic retrieval:   swappable local vector adapter
Local AI:             Ollama / LM Studio adapters later
```

## Runtime architecture

```text
Electron shell
  native dialogs, narrow IPC, lifecycle
        |
React frontend
  navigation, ChatWindow, management surfaces
        |
Local Express API at 127.0.0.1:3333
  routes, validation, controllers
        |
Domain models and services
  Libraries, Chats, messages, app state, AI completion
        |
SQLite + filesystem + provider adapters
```

Use Electron IPC for desktop-shell capabilities only. Business behavior belongs behind the local API. Archivist is a modular monolith, not a microservice system.

---

# Repository Structure

New work should follow the existing domain-first pattern instead of creating generic dumping-ground folders.

```text
Archivist/
├── package.json
├── package-lock.json
├── .nvmrc
├── .npmrc
├── .env                  local only; never commit secrets
├── .env.example
├── README.md
├── scripts/
│   └── check-node-version.mjs
├── frontend/
│   ├── package.json
│   ├── electron/
│   │   ├── main.cjs
│   │   └── preload.cjs
│   └── src/
│       ├── App.tsx
│       ├── App.css
│       ├── main.tsx
│       ├── components/
│       │   ├── chat/ChatWindow.tsx
│       │   ├── sidebar/
│       │   │   ├── Sidebar.tsx
│       │   │   ├── SidebarCard/
│       │   │   ├── Chats/
│       │   │   └── Libraries/
│       │   └── topbar/
│       ├── domains/
│       │   ├── chat/{chat.api.ts, chat.types.ts}
│       │   └── library/{library.api.ts, library.types.ts}
│       └── styles/theme.ts
└── backend/
    ├── package.json
    ├── data/archivist.db
    └── src/
        ├── index.ts
        ├── app.ts
        ├── api/
        │   ├── appState/{controllers,models,routes,schemas,types}/
        │   ├── libraries/{controllers,models,routes,schemas,types}/
        │   └── chats/
        │       ├── controllers/
        │       ├── models/
        │       ├── routes/
        │       ├── schemas/
        │       ├── services/ChatCompletionService.ts
        │       └── types/
        ├── config/env.ts
        ├── core/ai/
        │   ├── AIProvider.ts
        │   └── providers/OpenAIProvider.ts
        ├── database/{database.ts,migrations}/
        ├── errors/
        ├── middleware/
        └── scripts/test-openai.ts
```

## Placement rules

Frontend:

- Shared visual behavior belongs in reusable components such as `SidebarCard`.
- Domain API calls and types stay under `domains/<domain>`.
- Component CSS stays beside the component; app-wide knobs live in theme/global tokens.
- `App.tsx` coordinates top-level state until a real boundary justifies extraction.

Backend:

```text
routes       map URLs and preserve route precedence
controllers  translate HTTP requests and responses
schemas      validate runtime input
models       own persistence and domain invariants
services     coordinate multi-step behavior and providers
core         vendor-independent contracts
providers    vendor-specific adapters
```

Important routing rule:

```text
Correct order: /archived → /selected → /:chatId
```

Fixed collection routes must precede parameter routes or Express may treat `archived` as an ID.

---

# Current Core Flows

## Startup and restoration

```text
Electron opens React
→ fetch active/archived Libraries
→ fetch active/archived Chats
→ fetch app state
→ restore selected Library and Chat
→ load selected Chat messages
```

A failed request inside startup `Promise.all()` can prevent successful data from being applied. Keep endpoints and response shapes stable.

## Library flow

```text
choose folder through Electron
→ POST Library
→ normalize and persist path
→ select it
→ render in sidebar
→ manage rename/archive/restore through modal
```

Archivist currently registers Library metadata. It does not scan or mutate the folder.

## Persistent AI Chat flow

```text
submit text
→ show optimistic user message
→ POST /api/chats/:chatId/respond
→ persist user message
→ load history
→ ChatCompletionService calls AIProvider
→ OpenAIProvider calls OpenAI
→ persist assistant message
→ replace optimistic row with canonical saved rows
→ update Chat activity ordering
```

Current behavior is request/response. Streaming comes later.

---

# Current Data Model

```text
libraries
  id, name, description?, root_path unique, archived_at?, created_at, updated_at

chats
  id, library_id?, title, type(main|standard), archived_at?, created_at, updated_at

messages
  id, chat_id, role(user|assistant|system), content,
  status(streaming|complete|cancelled|failed), created_at, updated_at

app_settings
  singleton id=1, selected_library_id?, selected_chat_id?, created_at, updated_at
```

`library_id = null` represents a standalone Chat. The schema also supports Library-owned Chats and one `main` Chat per Library. Messages cascade when a Chat is permanently deleted. Archiving a selected item should choose an active fallback or `null`.

---

# API Surface

Development base URL:

```text
http://127.0.0.1:3333/api
```

```text
GET    /api/health

GET    /api/libraries
GET    /api/libraries/archived
POST   /api/libraries
PATCH  /api/libraries/:libraryId
POST   /api/libraries/:libraryId/archive
POST   /api/libraries/:libraryId/restore

GET    /api/app-state
PATCH  /api/app-state/selected-library

GET    /api/chats
GET    /api/chats/archived
POST   /api/chats
PATCH  /api/chats/selected
GET    /api/chats/:chatId
PATCH  /api/chats/:chatId
DELETE /api/chats/:chatId
GET    /api/chats/:chatId/messages
POST   /api/chats/:chatId/messages
POST   /api/chats/:chatId/respond
```

Chat archive/restore behavior belongs to the same domain and should remain consistent with the frontend management flow.

---

# AI, Context, and Memory Direction

## Provider boundary

```text
ChatCompletionService
→ AIProvider interface
→ OpenAIProvider today
→ Anthropic/Ollama/LM Studio later
```

Controllers must not call vendor SDKs directly.

## Context Compiler

The Context Compiler should become the only normal path for provider input.

```text
compileContext({
  chatId,
  libraryId optional,
  newMessage,
  attachments,
  providerBudget
})

returns:
  instructions
  selected messages
  approved memories
  retrieved sources
  budget metadata
  explainable context manifest
```

Do not let every feature concatenate independent prompt strings.

## Memory tiers and scopes

```text
Hot      recent messages included directly
Warm     older messages available through pagination/retrieval
Cold     summaries and indexed historical conversation
Durable  user-approved Global, Chat, or Library memory

Global   preferences useful across Archivist
Chat     purpose, decisions, terminology, temporary working style
Library  domain meaning, policies, structure, canon, retrieval rules
```

Library memory eventually becomes portable through a human-readable `.archivist/` wiki. Global preferences must not be hidden inside an arbitrary Library.

Start the wiki compactly when retrieval is ready:

```text
.archivist/
├── soul.md
├── structure.md
├── decisions.md
├── glossary.md
├── ignore.md
└── policies.json
```

```text
Chat is the stream.
The wiki is distilled Library meaning.
SQLite is the structured ledger.
The filesystem is the source of truth.
Indexes are retrieval accelerators.
```

---

# Product Model and Navigation

## Library

A Library is a registered user-selected root folder. It can own Chats, be renamed inside Archivist without renaming the folder, and be archived/restored without touching disk contents. Permanent deletion must remain a separate explicit Danger Zone action.

## Chat

```text
Standalone Chat  durable general conversation with library_id = null
Library Chat     focused conversation inheriting optional Library context
Main Timeline    canonical durable conversation for a Library
```

The visible timeline may become effectively infinite while the Context Compiler selects only what the model needs.

## Collection — later

Collections reference Libraries and Chats for organization; they do not own, duplicate, move, or change `library_id`.

## Three different trees

```text
Navigation tree  user organization and references
Filesystem tree  actual files under a Library root
Work tree        AI/tool steps and progress
```

They may share visual primitives but require different storage, permissions, and operations.

## Sidebar contract

Current:

```text
Libraries
Archived Libraries
Chats
Archived Chats
```

Future:

```text
Libraries
└── Selected Library
    ├── Main Timeline
    ├── Focused Chats
    └── Files                 later

Standalone Chats
Collections / Favorites       later
Profiles / Tools              later
```

`SidebarCard` is the shared Library/Chat row primitive. Keep active/archived behavior consistent, centralize animations/tokens, preserve reduced motion, and avoid visual refactors during unrelated backend slices.

```text
Chat is the command surface.
Navigation is the map and state display.
```

---

# Safety and Trust

1. Local-first; filesystem authoritative.
2. No silent mutation or normal-flow user-file deletion.
3. Review consequential changes before applying them.
4. Archive/deprecate before destructive removal.
5. Show what context may leave the machine.
6. Use the OS keychain for production credentials.
7. Log operations and make revert understandable.
8. AI profiles and tools never bypass policy.
9. Memory remains inspectable and controllable.
10. Complexity must pay rent.

The user should be able to answer:

```text
What changed?
Why?
What information was used or sent to a model?
Who approved it?
Can I undo it?
```

---

# MVP Boundary

## MVP should prove

- A folder becomes a persistent Library.
- Chats/messages persist and one real provider holds a durable conversation.
- The model receives deliberate Global, Chat, and Library context.
- Long conversations remain browsable without sending the full timeline.
- Local retrieval shows relevant sources.
- Proposed changes can be reviewed and reverted.

## MVP should not attempt

- Autonomous swarms, silent file management, cloud collaboration, plugin marketplace, every provider/file format, unnecessary runtimes, arbitrary-file version control, UE5 automation, or scheduled autonomous routines before manual workflows are trustworthy.

---

# Local Development

From the repository root:

```bash
nvm use
npm install
npm run dev
```

Build and test:

```bash
npm run build
npm run build -w backend
npm run build -w frontend
npm run test:openai -w backend
```

Useful checks:

```bash
curl http://127.0.0.1:3333/api/health
curl http://127.0.0.1:3333/api/libraries
curl http://127.0.0.1:3333/api/chats
curl http://127.0.0.1:3333/api/app-state
sqlite3 backend/data/archivist.db ".tables"
sqlite3 backend/data/archivist.db "PRAGMA user_version;"
```

`SIGINT` and Electron lifecycle errors after pressing `Ctrl+C` are normal shutdown noise, not evidence that SQLite data was deleted.

---

# Feature Development Rhythm

```text
define the smallest complete behavior
→ create a narrow feature branch
→ establish paths with exact commands
→ implement a vertical slice
→ test through UI and API
→ run builds
→ fix relevant rough edges
→ commit and open PR
→ merge before the next large slice
```

Good next branches: Context Compiler foundation; Global/Chat memory persistence; memory review UI; message pagination; Library-owned navigation; read-only filesystem explorer.

Avoid branches mixing context, indexing, sidebar redesign, and tool execution at once.

---

# Seed for the Next Development Chat

Archivist is a local-first Electron AI workspace. The persistent chat spine works:

- Electron + React/Vite/TypeScript frontend.
- Express/TypeScript backend at `127.0.0.1:3333`.
- SQLite Libraries, Chats, selected state, and messages.
- Real OpenAI responses through `AIProvider` and `OpenAIProvider`.
- `/api/chats/:chatId/respond` persists both sides of a turn.
- `ChatWindow` reloads saved conversations.
- Shared `SidebarCard` and normalized active/archived management.
- Builds pass.

Next milestone: deterministic Context Compiler, then inspectable Global and Chat memory.

Suggested sequence:

1. Inspect `ChatCompletionService`, provider contract, message model, schemas, and migrations.
2. Define a small `ContextCompiler` input/output contract.
3. Route existing recent-message context through it without changing behavior.
4. Add Global and Chat memory persistence with narrow schemas.
5. Read approved memories into the compiled packet.
6. Add memory proposal/review UI after storage is stable.
7. Add message pagination and upward infinite scrolling.
8. Attach Chats to Libraries and add file retrieval afterward.

Out of scope for that slice: file scanning, vectors, AI wiki generation, mutation, tools, diffs, multi-provider routing, autonomous swarms, and large sidebar redesigns.

Required working style:

- Exact commands for new paths.
- Whole-file replacements around 300 lines or less.
- Search anchor plus exact old/new blocks for partial edits.
- Copy-pasteable, low-cognitive-load steps.
- Narrow changes that preserve unrelated working behavior.
- Direct professional pushback when architecture is risky.

```text
One workspace.
Durable conversation.
Curated context.
Inspectable memory.
Observable work.
Human approval.
Local ownership.
```
