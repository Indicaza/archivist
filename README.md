# Archivist

> **Status:** The persistent Chat foundation, Context Compiler framework, SQLite FTS retrieval, and Auto Balanced context assembly are working. The next major slice is provider/model controls, followed by Agents.

Archivist is a local-first Electron AI workspace for durable conversation, file-aware retrieval, and safe computer work.

```text
Choose a folder
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
path → action → copy-pasteable code/command → verification command
```

Prefer complete vertical slices, one build after the chunk, clear commit boundaries, minimal unrelated refactors, and direct professional pushback when a choice is risky.

Avoid vague placement instructions, intentional red builds, scattered micro-edits, repeated questions, premature abstractions, and long architecture lectures during active implementation.

---

# Current Product Snapshot

## Working

- [x] Electron + React/Vite/TypeScript desktop shell.
- [x] Express/TypeScript backend at `127.0.0.1:3333`.
- [x] Node 24 workspace with one-command development startup.
- [x] SQLite WAL mode and versioned migrations.
- [x] Persistent Libraries, Chats, messages, and selected state.
- [x] Library and Chat create/select/manage/archive/restore flows.
- [x] Real OpenAI responses behind an `AIProvider` boundary.
- [x] Conversations survive Electron/backend restarts.
- [x] Context Compiler registry with per-Chat persisted compiler/config.
- [x] Generic Context Compiler Laboratory controls.
- [x] Deterministic compilers: Recent History, Contiguous History, Turn Pairs, Keyword Recency, Auto Balanced v1/v2.
- [x] SQLite FTS5 index for completed Chat messages.
- [x] Automatic FTS backfill and insert/update/delete synchronization.
- [x] Ranked retrieval with BM25, excerpts, metadata, and token estimates.
- [x] Auto Balanced v2 separates evidence from intent and keeps the current user message last.
- [x] Backend and frontend builds pass.

## Current limitations

- [~] Provider and generation controls are mostly hardcoded.
- [~] Only OpenAI is wired.
- [~] Chats load as one list; upward pagination is not built.
- [~] Retrieval is lexical FTS only; no embeddings yet.
- [~] Retrieved evidence uses individual messages rather than neighboring turns.
- [~] Library files are not indexed or available to Chat yet.
- [~] Streaming output is not built.
- [~] Compiler manifest logs should eventually sit behind a development flag.
- [~] Production credentials should use the OS keychain.

## Not built yet

- [ ] Provider/model selection UI.
- [ ] Temperature, top-p, output-token, and supported penalty controls.
- [ ] Agents and Agent assignment to Chats.
- [ ] Agent system instructions, tools, permissions, and Library scope.
- [ ] Message pagination and true infinite scroll.
- [ ] Read-only Library scanning, parsing, and retrieval.
- [ ] Embeddings and semantic retrieval.
- [ ] Visible source/context inspector.
- [ ] User-approved durable memory.
- [ ] File tools, diffs, operation ledger, approval, and revert.

---

# Immediate Roadmap

## 1. Provider and generation controls — next

Create reusable, persisted generation configuration rather than embedding vendor-specific values in controllers.

```text
GenerationConfig
- provider
- model
- temperature
- maxOutputTokens
- topP
- frequencyPenalty
- presencePenalty
```

Add capability metadata so the UI only renders controls a selected model supports.

```text
ModelCapabilities
- supportsTemperature
- supportsTopP
- supportsFrequencyPenalty
- supportsPresencePenalty
- maximumOutputTokens
```

Start with OpenAI text models only. No image generation in this slice.

## 2. Agents MVP

Agents should become the reusable owner of AI behavior.

```text
Agent
- id
- name
- description
- systemInstructions
- generationConfig
- contextCompiler reference/config
- default Library scope later
- archivedAt
```

UI direction:

```text
Agents tab
→ same card language as Libraries and Chats
→ dedicated Agent Management modal

Chat Management
→ selected Agent
→ button opens Agent modal
```

First Agent slice:

- create, rename, duplicate, archive, and restore;
- configure instructions, model settings, and compiler;
- assign one Agent to a Chat;
- ship one built-in Archivist Default Agent.

Do not add tools, swarms, autonomous loops, or complex permissions yet.

## 3. Infinite Chat history

```text
Permanent timeline = complete browsable record.
Provider context   = curated subset for one response.
```

- paginate messages;
- load recent messages first;
- fetch older rows while scrolling upward;
- preserve the visible scroll anchor;
- keep retrieval independent from UI pagination.

## 4. Library retrieval

- lazy filesystem explorer and ignore rules;
- file metadata catalog and extraction;
- SQLite FTS5 keyword search;
- source cards explaining selection;
- embeddings later through a swappable adapter.

## 5. Tools and safe mutation

```text
read-only retrieval
→ visible sources
→ proposed file changes
→ diff and approval
→ operation ledger
→ revert
```

---

# Core Architecture

## Runtime

```text
Electron shell
  native dialogs, narrow IPC, lifecycle
        |
React frontend
  navigation, ChatWindow, management modals
        |
Express API
  routes, validation, controllers
        |
Domain services and models
  Libraries, Chats, cognition, completion
        |
SQLite + filesystem + provider adapters
```

Archivist is a modular monolith. Use Electron IPC only for desktop-shell capabilities; business behavior belongs behind the local API.

## Context architecture

```text
Search tools
→ return ranked ContextCandidates

Selectors / orchestrators
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

Retrieved old messages must never masquerade as current user intent.

## Provider boundary

```text
ChatCompletionService
→ AIProvider interface
→ OpenAIProvider today
→ more providers later
```

Controllers must not call vendor SDKs directly.

---

# Product and Safety Principles

1. Local-first by default.
2. Filesystem is authoritative for user files.
3. SQLite stores structured state, history, and indexes.
4. Chat is the main command surface.
5. History is durable; provider context is temporary.
6. Current intent must remain distinct from retrieved evidence.
7. Meaningful memory should be inspectable and user-controlled.
8. No silent filesystem mutation.
9. Archive/deprecate before destructive removal.
10. Proposal before consequential action.
11. Deterministic workflows before autonomous agents.
12. Vertical slices before speculative infrastructure.
13. Complexity must pay rent.

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
│   ├── appState/
│   ├── chats/
│   ├── cognition/{contextCompilers,contextRetrieval}/
│   └── libraries/
├── core/
│   ├── ai/
│   └── cognition/
│       ├── conscious/context/{compilers,utilities}/
│       └── retrieval/{tools,ContextRetrievalTool.ts,ContextRetrievalTypes.ts}
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
/archived → /selected → /:id
```

---

# Core Data Direction

```text
libraries
  id, name, description?, root_path, archived_at?, timestamps

chats
  id, library_id?, title, type, archived_at?,
  context_compiler_id, context_compiler_version,
  context_compiler_config, timestamps

messages
  id, chat_id, role, content, status, timestamps

message_search
  FTS5 mirror of completed non-empty messages

app_settings
  selected_library_id?, selected_chat_id?
```

`library_id = null` means standalone Chat.

Future:

```text
agents
generation profiles
agent-to-chat assignment
approved memories
library file catalog
operation ledger
```

---

# Development Commands

From repository root:

```bash
nvm use
npm install
npm run dev
```

Build:

```bash
npm run build
npm run build -w backend
npm run build -w frontend
```

Useful checks:

```bash
curl http://127.0.0.1:3333/api/health
curl http://127.0.0.1:3333/api/cognition/context-compilers
curl --get http://127.0.0.1:3333/api/cognition/search/messages \
  --data-urlencode "q=obsidian compass"

sqlite3 backend/data/archivist.db ".tables"
sqlite3 backend/data/archivist.db "PRAGMA user_version;"
sqlite3 backend/data/archivist.db "SELECT count(*) FROM message_search;"
```

`SIGINT` and Electron lifecycle errors after `Ctrl+C` are normal shutdown noise.

---

# Feature Rhythm

```text
define the smallest complete behavior
→ create a narrow branch
→ establish exact paths
→ implement the full chunk
→ verify through UI/API
→ run one build
→ commit and push
→ merge before the next large slice
```

Do not mix provider controls, Agents, Library indexing, sidebar redesign, and tool execution into one PR.

---

# Seed for the Next Development Chat

Archivist is a local-first Electron AI workspace.

Working now:

- persistent Libraries, Chats, messages, and selected state;
- real OpenAI Chat through an `AIProvider` boundary;
- versioned Context Compiler framework and generic compiler modal;
- deterministic history compilers;
- SQLite FTS5 Chat retrieval;
- Auto Balanced v2, which separates retrieved evidence from current intent and keeps the latest user message last;
- builds pass.

The Context Compiler/retrieval PR has been merged. Work from `main`.

Next milestone: **provider and generation controls**, designed so Agents can own them in the following PR.

Suggested sequence:

1. Inspect `AIProvider`, `OpenAIProvider`, `ChatCompletionService`, Chat types, schemas, migrations, and management modal.
2. Define provider-independent `GenerationConfig` and model capability contracts.
3. Add persisted per-Chat generation config as a temporary ownership boundary.
4. Add OpenAI model selection and supported text-generation controls.
5. Render controls dynamically from provider/model capabilities.
6. Keep provider-specific translation inside `OpenAIProvider`.
7. Preserve the schema so ownership can move from Chat to Agent without redesigning the contract.
8. Follow with an Agents MVP and Agent-to-Chat assignment.

Out of scope for the next slice:

```text
image generation
Library file indexing
embeddings
tools
permissions
autonomous loops
multi-agent swarms
large sidebar redesign
```

Required working style:

- exact commands for new paths;
- whole-file replacements around 500 lines or less;
- exact search anchors and old/new blocks for partial edits;
- complete copy-pasteable chunks;
- one build after the chunk;
- direct pushback on risky architecture;
- preserve unrelated working behavior.

```text
One workspace.
Durable conversation.
Curated context.
Inspectable memory.
Observable work.
Human approval.
Local ownership.
```
