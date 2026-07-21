<p align="center">
  <img src="./assets/Mnemosyne.png" alt="Mnemosyne, the memory of Archivist" width="100%">
</p>

# Archivist

> A local-first AI workspace for durable project memory, curated context, and inspectable computer work.

Archivist turns folders into long-lived **Libraries** where files, conversations, Agents, decisions, and context can stay connected over time. Instead of treating every AI session as disposable, Archivist gives projects a persistent home and keeps the user in control of what the model sees and does.

```text
Choose a Library
→ browse its files
→ talk to a persistent Agent
→ retrieve only relevant context
→ inspect the evidence and outcome
```

Archivist is under active development. The native Qt desktop client is the primary application; the older Electron/React client remains in the repository as a behavioral reference.

## Core features

- **Local-first Libraries** — register folders, scan their contents, and browse cataloged files without surrendering ownership of the filesystem.
- **Durable Chat** — persistent conversations, selected state, large-history pagination, and native variable-height message rendering.
- **Configurable Agents** — create reusable AI identities with personality, profession, doctrine, output rules, generation settings, and Context Compiler configuration.
- **Chat-to-Agent assignment** — assign different Agents to different conversations and preserve those choices across restarts.
- **Native management workflows** — create, rename, archive, restore, duplicate, and delete Chats and Agents through the Qt client.
- **Deterministic context** — versioned Context Compilers decide how conversation, evidence, and current intent are merged and budgeted.
- **Provider abstraction** — OpenAI is currently supported behind an adapter boundary so project continuity does not belong to one model vendor.
- **Inspectable persistence** — SQLite stores structured state, history, metadata, and indexes while the filesystem remains authoritative for user files.

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
  native Workbench, Explorer, Chat, animation, desktop lifecycle
        |
C++ domain stores
  HTTP requests, client state, QML-facing models
        |
Express API
  validation, orchestration, product behavior
        |
Domain models and services
  persistence, invariants, context compilation
        |
SQLite + filesystem + AI providers
  durable state, source files, retrieval, generation
```

The API owns business rules. QML owns presentation and interaction. C++ stores bridge the two without duplicating backend invariants.

### Data ownership

```text
Filesystem  → authoritative user file contents
SQLite      → Libraries, Chats, messages, Agents, metadata, indexes
Providers   → temporary generation workers
Archivist   → continuity, context, permissions, provenance, outcomes
```

## Repository layout

```text
Archivist/
├── backend/              Express 5, TypeScript, SQLite, AI and cognition domains
├── qt/                   Primary Qt 6 / QML desktop client
│   ├── qml/App/          Theme, Topbar, Workbench, Explorer, Chat and editors
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

`qt-dev` starts the local backend when needed, waits for the health endpoint, builds the native client, and launches Archivist.

The backend API runs at:

```text
http://127.0.0.1:3333/api
```

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
```

## Status

Archivist already has a native Workbench, persistent Libraries, Chats, paginated history, real AI responses, configurable Agents, and native Chat/Agent management. Development is currently focused on safe Library file reading, visible source handling, and file-aware context.

---

**One workspace. Durable conversation. Curated context. Inspectable memory. Local ownership.**
