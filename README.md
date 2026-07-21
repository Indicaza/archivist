<p align="center">
  <img src="./assets/Mnemosyne.png" alt="Mnemosyne, the memory of Archivist" width="100%">
</p>

# Archivist

> A local-first AI workspace for durable project memory, curated context, and inspectable computer work.

Archivist turns folders into long-lived **Libraries** where files, conversations, Agents, decisions, and context stay connected over time. Instead of treating every AI session as disposable, Archivist gives each project a persistent home and keeps the user in control of what the model sees and does.

```text
Choose a Library
→ browse and preview its files
→ attach trusted sources to a Chat
→ talk to a persistent Agent
→ inspect the context used
```

Archivist is under active development. The native Qt desktop client is the primary application; the older Electron/React client remains in the repository as a behavioral reference.

## Core features

- **Local-first Libraries** — register folders, scan their contents, and browse cataloged files without surrendering ownership of the filesystem.
- **Safe native file preview** — open supported text and source files through a root-constrained, read-only backend boundary.
- **Durable Chat** — persistent conversations, selected state, large-history pagination, and native variable-height message rendering.
- **File-aware Chat** — explicitly attach Library files to a conversation, persist those attachments, and remove them at any time.
- **Visible sources** — attached files appear in the Workbench, while sources included in the latest response are clearly marked.
- **Bounded context** — attached evidence is token-budgeted and passed through the selected Context Compiler without replacing the user's current intent.
- **Configurable Agents** — create reusable AI identities with personality, profession, doctrine, output rules, generation settings, and Context Compiler configuration.
- **Chat-to-Agent assignment** — assign different Agents to different conversations and preserve those choices across restarts.
- **Native management workflows** — create, rename, archive, restore, duplicate, and delete Chats and Agents through the Qt client.
- **Provider abstraction** — OpenAI is currently supported behind an adapter boundary so project continuity does not belong to one model vendor.
- **Inspectable persistence** — SQLite stores structured state, history, attachments, metadata, and indexes while the filesystem remains authoritative for user files.

## Product philosophy

Archivist is designed around a few simple rules:

1. **Local before cloud.** User files and project continuity should remain under the user's control.
2. **Durable history, temporary provider context.** Chats persist; only curated context is sent to a model.
3. **Evidence is not intent.** Retrieved material must remain visibly distinct from the user's current request.
4. **Read-only before mutation.** Inspection and retrieval come before file-writing or autonomous actions.
5. **Human approval before consequences.** Important operations should be reviewable, attributable, and reversible.
6. **Providers are workers, not owners.** Archivist owns memory, sources, permissions, artifacts, and continuity.
7. **Complexity must pay rent.** Prefer small, complete vertical slices over speculative infrastructure.

## Architecture

Archivist is a local modular monolith with clear domain boundaries:

```text
Qt 6 / QML desktop
  native Workbench, Explorer, Chat, source controls, desktop lifecycle
        |
C++ domain stores
  HTTP requests, client state, QML-facing models
        |
Express API
  validation, orchestration, product behavior
        |
Domain models and services
  persistence, safe file access, context compilation
        |
SQLite + filesystem + AI providers
  durable state, source files, retrieval, generation
```

The API owns business rules. QML owns presentation and interaction. C++ stores bridge the two without duplicating backend invariants.

### Context flow

```text
current user message
+ recent durable conversation
+ explicitly attached Library evidence
→ Context Compiler
→ bounded provider request
→ response and source manifest
```

The current user message remains the highest-priority intent. Attached files are evidence, not instructions.

### Data ownership

```text
Filesystem  → authoritative user file contents
SQLite      → Libraries, Chats, messages, Agents, attachments, metadata, indexes
Providers   → temporary generation workers
Archivist   → continuity, context, permissions, provenance, outcomes
```

## Repository layout

```text
Archivist/
├── assets/               README artwork and project media
├── backend/              Express 5, TypeScript, SQLite, AI and cognition domains
├── qt/                   Primary Qt 6 / QML desktop client
│   ├── qml/App/          Theme, Workbench, Explorer, Chat, previews and editors
│   └── src/App/Domains/  C++ Library, Chat and Agent stores
├── frontend/             Legacy Electron/React reference client
├── scripts/              Qt configure, build, run and development helpers
└── README.md
```

## Requirements

The current development environment targets macOS.

- Node.js 24 LTS
- npm
- Qt 6.8 or newer
- CMake 3.24 or newer
- Ninja

Example macOS dependencies:

```bash
brew install qt cmake ninja
```

## Setup

From the repository root:

```bash
nvm use
npm install

cp backend/.env.example backend/.env
# Add the required AI-provider credentials to backend/.env.

./scripts/qt-dev
```

`qt-dev` validates the Node version and native SQLite module, starts the local backend when needed, waits for the health endpoint, builds the native client, and launches Archivist.

The backend API runs at:

```text
http://127.0.0.1:3333/api
```

### Native-module recovery

`better-sqlite3` is compiled for a specific Node ABI. After changing Node versions, rebuild it before starting the backend:

```bash
nvm use
npm rebuild better-sqlite3
```

The backend logs the active database path, schema version, and attachment-table readiness at startup.

## Development commands

Run the complete local application:

```bash
./scripts/qt-dev
```

Work with the native client directly:

```bash
./scripts/qt-configure
./scripts/qt-build
./scripts/qt-run
```

Useful checks:

```bash
npm run build -w backend
npm run build
npm run lint
curl http://127.0.0.1:3333/api/health
sqlite3 backend/data/archivist.db "PRAGMA user_version;"
```

When testing backend or database changes, stop any stale process already listening on port `3333` so the new code and migrations actually start:

```bash
lsof -tiTCP:3333 -sTCP:LISTEN | xargs kill 2>/dev/null || true
./scripts/qt-dev
```

## Status

Archivist currently has a native Workbench, persistent Libraries and Chats, paginated history, real AI responses, configurable Agents, safe Library file preview, persistent Chat attachments, bounded file-aware context, and visible source state.

Current development is focused on richer context/source inspection, Library indexing and retrieval, and observable read-only Agent tools.

---

**One workspace. Durable conversation. Curated context. Inspectable memory. Local ownership.**
