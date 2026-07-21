<p align="center">
  <img src="./assets/Mnemosyne.png" alt="Mnemosyne, the memory of Archivist" width="100%">
</p>

# Archivist

> A local-first AI workspace for durable project memory, curated context, and inspectable computer work.

Archivist turns folders into long-lived **Libraries** where files, conversations, Agents, decisions, and context stay connected over time.

Instead of treating every AI session as disposable, Archivist gives each project a persistent home and keeps the user in control of what the model sees, remembers, and does.

```text
Choose a Library
→ browse and preview its files
→ attach trusted sources to a Chat
→ talk to a persistent Agent
→ inspect the exact context used
→ reopen the original evidence
```

Archivist is under active development. The native Qt desktop client is the primary application; the older Electron/React client remains in the repository as a behavioral reference.

## Core features

- **Local-first Libraries** — register folders, scan their contents, and browse cataloged files without surrendering ownership of the filesystem.
- **Safe native file preview** — open supported text and source files through a root-constrained, read-only backend boundary.
- **Durable Chat** — preserve conversations, selected state, and large histories with cursor-based pagination.
- **File-aware Chat** — explicitly attach Library files to an individual conversation and remove them at any time.
- **Visible sources** — show attached files in the Workbench and mark sources included in the latest response.
- **Bounded context** — budget attached evidence through the selected Context Compiler without replacing the user's current intent.
- **Durable Context Inspector** — inspect the compiler, model, token accounting, warnings, message selection, and source outcomes behind individual assistant responses.
- **Reopenable evidence** — jump from a recorded context source back to the authoritative Library file.
- **Configurable Agents** — create reusable AI identities with personality, profession, doctrine, output rules, generation settings, and Context Compiler configuration.
- **Chat-to-Agent assignment** — assign different Agents to different conversations and preserve those choices across restarts.
- **Native management workflows** — create, rename, archive, restore, duplicate, and delete Chats and Agents through the Qt client.
- **Provider abstraction** — OpenAI is currently supported behind an adapter boundary so project continuity does not belong to one model vendor.
- **Inspectable persistence** — SQLite stores structured state, history, attachments, context records, metadata, and indexes while the filesystem remains authoritative for user files.
- **Managed native development** — build and run the backend and Qt desktop client from the repository root in one supervised terminal session.

## Product philosophy

Archivist is designed around a few simple rules:

1. **Local before cloud.** User files and project continuity should remain under the user's control.
2. **Durable history, temporary provider context.** Chats persist; only curated context is sent to a model.
3. **Evidence is not intent.** Retrieved material must remain visibly distinct from the user's current request.
4. **Inspection before automation.** A context system should explain what it used before it begins making broader decisions.
5. **Read-only before mutation.** Inspection and retrieval come before file-writing or autonomous actions.
6. **Human approval before consequences.** Important operations should be reviewable, attributable, and reversible.
7. **Providers are workers, not owners.** Archivist owns memory, sources, permissions, artifacts, and continuity.
8. **Complexity must pay rent.** Prefer small, complete vertical slices over speculative infrastructure.

## Architecture

Archivist is a local modular monolith with clear domain boundaries:

```text
Qt 6 / QML desktop
  native Workbench, Explorer, Chat, source controls, Context Inspector
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
  durable state, authoritative files, retrieval, generation
```

The API owns business rules. QML owns presentation and interaction. C++ stores bridge the two without duplicating backend invariants.

### Context flow

```text
current user message
+ recent durable conversation
+ explicitly attached Library evidence
→ Context Compiler
→ bounded provider request
→ assistant response
→ durable context-run snapshot
→ native Context Inspector
```

The current user message remains the highest-priority intent. Attached and retrieved files are evidence, not instructions.

### Context inspection

Each newly generated assistant response can preserve:

```text
compiler ID and version
provider, model, and Agent
input budget and response reserve
estimated tokens used
included and omitted messages
compiler warnings and timing
source outcomes and token contributions
```

Source outcomes are explicit:

```text
Included
Truncated
Omitted
Unavailable
Failed
```

Context records describe what happened during a specific response. They are not reconstructed later from files that may have changed.

### Data ownership

```text
Filesystem  → authoritative user file contents
SQLite      → Libraries, Chats, messages, Agents, attachments, context records, indexes
Providers   → temporary generation workers
Archivist   → continuity, context, permissions, provenance, outcomes
```

## Repository layout

```text
Archivist/
├── assets/               README artwork and project media
├── backend/              Express 5, TypeScript, SQLite, AI and cognition domains
├── qt/                   Primary Qt 6 / QML desktop client
│   ├── qml/App/          Workbench, Explorer, Chat, previews, inspectors and editors
│   └── src/App/Domains/  C++ Library, Chat and Agent stores
├── frontend/             Legacy Electron/React reference client
├── scripts/              Native build, runtime, cleanup and context helpers
└── README.md
```

## Requirements

The current development environment targets macOS.

- Node.js 24 LTS
- npm 10 or newer
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

npm run dev
```

The root development command owns the complete native session:

```text
validate Node and better-sqlite3
→ stop the previous managed backend
→ clean up stale Archivist listeners
→ start one backend
→ wait for API health
→ build the Qt client
→ launch Archivist
→ clean up the backend when Archivist exits
```

The backend API runs at:

```text
http://127.0.0.1:3333/api
```

## Development commands

Build the complete active application:

```bash
npm run build
```

Build and launch the backend and native Qt client in one terminal:

```bash
npm run dev
```

Stop a previous managed development session:

```bash
npm run dev:stop
```

Focused native commands:

```bash
npm run dev:qt
npm run build:qt
npm run qt:configure
npm run qt:run
```

The legacy Electron/React client remains available as a reference:

```bash
npm run dev:legacy
npm run build:legacy
```

Useful checks:

```bash
npm run lint
curl http://127.0.0.1:3333/api/health
sqlite3 backend/data/archivist.db "PRAGMA user_version;"
lsof -nP -iTCP:3333 -sTCP:LISTEN
```

## Runtime management

The managed backend PID is stored at:

```text
backend/data/runtime/qt-dev-backend.pid
```

`npm run dev` owns the backend process tree and cleans it up when the Qt application exits or the terminal receives `Ctrl+C`.

When a previous session ended unexpectedly:

```bash
npm run dev:stop
npm run dev
```

The cleanup script only stops backend processes belonging to this Archivist repository. It refuses to kill an unrelated application using port `3333` and reports the conflicting process instead.

### Native-module recovery

`better-sqlite3` is compiled for a specific Node ABI. After changing Node versions:

```bash
nvm use
npm rebuild better-sqlite3
npm run dev
```

A native-module mismatch commonly appears as:

```text
ERR_DLOPEN_FAILED
```

The development launcher validates the active Node version and SQLite module before starting the application.

The backend also logs the active database path, schema version, and important table readiness during startup.

## Status

Archivist currently has:

```text
native Qt Workbench
persistent Libraries and Chats
safe Library file preview
real provider-backed Agents
persistent per-Chat file attachments
bounded file-aware context
durable per-response context records
native source and token inspection
reopenable evidence
one-command native development workflow
```

The next milestone is deterministic Library text indexing and lexical retrieval:

```text
extract supported Library text
→ create stable chunks with file and line provenance
→ index chunks with SQLite FTS5
→ search and inspect results manually
→ later feed ranked evidence into Chat
```

Embeddings and automatic retrieval remain intentionally deferred until deterministic indexing is dependable and inspectable.

---

**One workspace. Durable conversation. Curated context. Inspectable memory. Local ownership.**
