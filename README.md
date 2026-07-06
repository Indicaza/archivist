# The Archivist Design Doc

## Run from root:

cd archivist

nvm use

npm run dev

## Simple Description

The Archivist is a local-first desktop app that turns messy personal knowledge folders into organized, searchable, AI-readable libraries.

A user can add any folder as a Library, teach Archivist what that Library is, and then chat with an AI assistant that can search, summarize, organize, rename, move, compare, and maintain that Library through human-approved proposals.

The Archivist does not upload the user’s full library to a hosted database. Files, indexes, metadata, operation history, and vector storage stay local. Cloud AI providers may be used with user-provided API keys, but only selected context snippets are sent, never the entire library by default.

The long-term vision is a local AI librarian that can organize personal knowledge, preserve decisions, detect inconsistencies, maintain canon, explain folder context, expose safe library access to external AI tools through a future MCP-style bridge, and eventually offer “grandma git” style rollbacks and semantic versioning for nontechnical users.

The Archivist is not a magic file janitor.

It is a librarian.

It reads the room, understands the library, proposes careful changes, shows its work, asks permission, preserves what came before, and keeps a record of every operation.

---

# 1. Product Definition

## 1.1 What We Are Building

The Archivist is a desktop app for managing personal knowledge libraries.

A Library is any user-selected folder that contains meaningful files the user wants to organize, search, and evolve over time.

Examples:

- Game lore notes
- Startup notes
- Research folders
- Obsidian vaults
- Writing projects
- Homestead planning folders
- Personal documents
- Creative project folders
- Future photo/document archives

The app is intentionally generic.

It does not hardcode concepts like “Game Design Library,” “Startup Library,” “Homestead Library,” or “Research Library.”

Instead, each Library gets its own local `.archivist/` folder containing an AI wiki, policy files, structure notes, glossary files, decision logs, ignore rules, proposals, history, and local databases.

The app starts generic, then becomes specialized per Library through conversation and local metadata.

Each Library teaches Archivist how to behave.

## 1.2 Core Product Promise

The Archivist helps users:

- Create local AI-readable libraries from messy folders.
- Search and chat with their own files.
- Understand what is inside a folder without manually opening everything.
- Organize files into durable human-readable structures.
- Preserve context about why the folder exists.
- Preserve current canon, deprecated drafts, and old directions.
- Preview all proposed file changes before anything happens.
- Apply changes only after human approval.
- Move superseded files into a Deprecated area instead of deleting them.
- Undo file operations through a local operation ledger.
- Preserve user control and local ownership of data.
- Eventually expose safe Library access to external AI tools like ChatGPT through a bridge.

## 1.3 Core Philosophy

The Archivist is a librarian, not an omnipotent janitor.

It should:

- Catalog
- Search
- Summarize
- Group
- Suggest
- Explain
- Preview
- Ask for approval
- Preserve history
- Move old material to Deprecated instead of deleting it
- Undo changes
- Maintain the Library’s AI wiki

It should not:

- Freestyle the user’s filesystem
- Delete files in v1
- Rearrange code projects
- Modify files without approval
- Hide what it is doing
- Upload data without showing what context is being sent
- Treat filenames as the only source of truth
- Lose old ideas just because the current direction changed

The four product principles are:

1. Human-readable.
2. AI-readable.
3. Reversible.
4. Permissioned.

A fifth practical rule governs all file changes:

5. Non-destructive by default.

---

# 2. The AI Wiki

## 2.1 What the AI Wiki Is

Every Library has an internal AI wiki stored inside its `.archivist/` folder.

This wiki is not a marketing feature.

It is one of the core systems of the product.

The AI wiki teaches Archivist:

- What this Library is.
- What the user is trying to accomplish.
- What matters in this folder.
- How the folder is structured.
- Which files are important.
- Which folders are active, draft, archived, or deprecated.
- What terms mean inside this Library.
- What decisions have already been made.
- What old directions should not be treated as current truth.
- What Archivist is allowed to touch.
- What Archivist should never touch.
- How the AI should behave while working in this Library.

Without the AI wiki, the AI is only guessing from filenames and snippets.

With the AI wiki, Archivist understands the Library as a living body of knowledge.

## 2.2 AI Wiki Goals

The AI wiki exists so Archivist can:

- Retrieve better context.
- Avoid confusing old drafts with current canon.
- Understand folder purpose.
- Understand directory structure.
- Understand local terminology.
- Preserve user preferences.
- Respect safety rules.
- Explain its reasoning.
- Make better proposals.
- Avoid repeating already-settled decisions.
- Help future AI sessions understand the Library quickly.

## 2.3 AI Wiki Structure

Suggested `.archivist/` structure:

- `soul.md`
- `policies.json`
- `structure.md`
- `glossary.md`
- `decisions.md`
- `ignore.md`
- `deprecated.md`
- `context-cheatsheet.md`
- `catalog.sqlite`
- `vectors/`
- `proposals/`
- `history/`

These files are human-readable where possible.

The database is for speed.

The wiki is for meaning.

## 2.4 soul.md

`soul.md` describes the identity and purpose of the Library.

It answers:

- What is this Library?
- What is the user trying to do with it?
- What matters here?
- What should Archivist optimize for?
- What should Archivist avoid?
- What tone or behavior should the assistant use when working here?
- What is the current “north star” of this Library?

Example sections:

# Library Soul

## Purpose

This Library contains notes for a long-running creative project.

## User Goals

- Keep ideas organized.
- Preserve drafts.
- Make current direction easy to find.
- Help AI retrieve context accurately.
- Reduce duplicate and conflicting notes.
- Protect old ideas without letting them pollute current canon.

## Important Preferences

- Prefer human-readable folder structures.
- Prefer reversible changes.
- Ask before modifying files.
- Keep old ideas in Deprecated instead of deleting them.
- Preserve context about why files were moved.

## Current Important Concepts

- Thresh
- Cradles
- Shards
- Dungeon ecology
- Custodian

## Behavioral Instructions

Archivist should behave like a careful librarian. It may suggest changes, but it should not act like it owns the Library.

## 2.5 policies.json

`policies.json` is machine-enforced.

It defines what Archivist is allowed to do.

Example:

{
"permissions": {
"read": "allowed",
"index": "allowed",
"summarize": "allowed",
"createFiles": "approval_required",
"modifyFiles": "approval_required",
"moveFiles": "approval_required",
"renameFiles": "approval_required",
"deleteFiles": "denied"
},
"approval": {
"requireApprovalForAllFileChanges": true,
"allowBulkApproval": true
},
"safety": {
"preserveOriginals": true,
"neverDelete": true,
"moveSupersededFilesToDeprecated": true,
"respectIgnoreRules": true,
"blockCodeProjectReorganization": true
}
}

Allowed permission modes:

- `denied`
- `allowed`
- `approval_required`

In v1:

- `deleteFiles` must always be denied.
- File modifications require approval.
- File moves require approval.
- File renames require approval.
- Memory file updates require approval.
- Moving files to Deprecated still requires approval.

## 2.6 structure.md

`structure.md` defines how the Library is organized.

This is the directory cheat sheet.

It prevents the AI from relying only on filenames.

It explains what folders mean.

Example sections:

# Structure Preferences

## Organization Style

Prefer a fractal structure.

Each major folder should have:

- `_index.md`
- `Drafts/`
- `Canon/`
- `Deprecated/`
- `Open Questions.md` where useful

## Folder Meanings

### Canon/

Current accepted direction.

### Drafts/

Active experiments and unresolved ideas.

### Deprecated/

Old directions, outdated drafts, replaced notes, superseded files, and previous versions that should be preserved but not treated as current truth.

### Research/

Reference material and external information.

### Indexes/

Human-readable summaries and navigation files.

## Naming

Use readable file names.

Prefer concept names over dates unless chronology matters.

## Folder Rules

- Group related files by topic.
- Use Deprecated for old or replaced ideas.
- Do not flatten everything.
- Do not delete old notes.
- Do not assume a file is irrelevant just because it is old.

## 2.7 context-cheatsheet.md

`context-cheatsheet.md` is a quick orientation file for the AI.

It gives the AI a compact map of the Library.

This file should answer:

- What are the major folders?
- What does each folder mean?
- Which folders are active?
- Which folders are historical?
- Which folders are dangerous or no-touch?
- Where should new generated files go?
- Where should replaced files go?
- What should be included in retrieval?
- What should be ignored unless explicitly requested?

Example:

# Context Cheat Sheet

## Active Areas

- `Canon/`: current accepted material.
- `Drafts/`: active unresolved ideas.
- `Indexes/`: navigation and summary files.

## Historical Areas

- `Deprecated/`: superseded material. Search only when the user asks for history, comparison, contradictions, or old versions.

## No-Touch Areas

- `.git/`
- `node_modules/`
- `dist/`
- `build/`
- Unreal Engine generated folders
- Dependency folders
- Code project source folders in v1

## Default Destinations

- New indexes go in `Indexes/`.
- Replaced files go in `Deprecated/`.
- Unsure files go in `Needs Review/`.
- Old but potentially useful drafts go in `Deprecated/` with a reason.

## Retrieval Guidance

Prefer Canon and Indexes for current answers.

Include Drafts when brainstorming.

Include Deprecated when checking contradictions, history, or old directions.

## 2.8 glossary.md

`glossary.md` stores important terms, entities, acronyms, and concepts in the Library.

It improves retrieval and context compilation.

Example:

# Glossary

## Thresh

A race or civilization concept in the project. Multiple drafts may exist.

## Cradles

Respawn or rebirth-related concept.

## Custodian

AI director / simulation management concept.

## 2.9 decisions.md

`decisions.md` records important user-approved decisions.

Example:

# Decisions

## 2026-07-05 - Thresh Origin

User chose to keep the hybrid swamp/Deepkin/Maw origin direction.

Pure Maw-created origin should be treated as deprecated draft material.

This helps prevent “fractured concurrency” where old notes accidentally compete with current canon.

## 2.10 deprecated.md

`deprecated.md` explains how Deprecated material should be handled.

This file is important because old ideas are not trash.

They are historical context.

Example:

# Deprecated Material Rules

Deprecated does not mean worthless.

Deprecated means:

- Not current canon.
- Not the default source of truth.
- Preserved for history.
- Searchable when needed.
- Useful for comparison.
- Useful for recovering old ideas.
- Useful for explaining why a direction changed.

Archivist should move superseded files into a Deprecated folder instead of deleting them.

When moving a file to Deprecated, Archivist should record:

- Original path
- New path
- Reason for deprecation
- Related proposal
- Date
- Whether this was user-approved
- What replaced it, if anything

Deprecated files should not be used as current truth unless the user explicitly asks for old versions, contradictions, history, or brainstorming.

## 2.11 ignore.md

`ignore.md` contains human-readable ignore rules.

Example:

# Ignore Rules

Never touch:

- code projects
- dependency folders
- generated folders
- backups
- exported builds

Patterns:

- `.git`
- `node_modules`
- `dist`
- `build`
- `Binaries`
- `Intermediate`
- `Saved`

The machine-readable version may also be stored in SQLite or JSON.

---

# 3. Deprecated Void

## 3.1 What the Deprecated Void Is

The Deprecated void is the default destination for old, replaced, superseded, or no-longer-current files.

It is not the trash.

It is not deletion.

It is a safe historical holding area.

When Archivist proposes to replace, reorganize, consolidate, rewrite, or supersede files, it should usually move the old versions into a `Deprecated/` folder instead of deleting them.

Example:

Original:

- `Thresh weird old idea.md`

New:

- `Canon/Races/Thresh.md`

Old file moved to:

- `Deprecated/Races/Thresh/Thresh weird old idea.md`

## 3.2 Why Deprecated Exists

Deprecated exists because the user’s notes are creative and historical.

Old ideas may become useful again.

Bad drafts may contain good fragments.

Conflicting lore may explain how the project evolved.

Old startup notes may contain useful research.

Old plans may contain reasoning that should not be lost.

Archivist should never assume old means worthless.

## 3.3 Deprecated Rules

Archivist should:

- Preserve superseded files.
- Move them to Deprecated instead of deleting them.
- Record why they were moved.
- Record what replaced them.
- Keep them searchable.
- Avoid treating them as current truth by default.
- Include them when the task requires comparison, contradiction detection, history, or recovery.

Archivist should not:

- Delete deprecated files in v1.
- Hide deprecated files from the user.
- Use deprecated material as current canon unless requested.
- Move files to Deprecated without approval.
- Treat Deprecated as a junk drawer with no metadata.

## 3.4 Deprecated Metadata

When a file is moved to Deprecated, Archivist should store metadata in the operation ledger and, where useful, a local note.

Metadata should include:

- Original path
- Deprecated path
- Date moved
- Proposal ID
- Reason
- Replacement file, if any
- User approval status
- Reversible status
- File hash before move
- File hash after move

---

# 4. Human-in-the-Loop Diffing

## 4.1 Core Rule

Archivist must never alter files silently.

Before any file operation, the user must see what will happen.

This includes:

- Moving files
- Renaming files
- Creating files
- Modifying files
- Updating AI wiki files
- Moving files into Deprecated
- Archiving files
- Applying generated indexes
- Consolidating notes

## 4.2 Chat-Based Diff Mode

The main interaction surface is chat.

When Archivist wants to change files, the chat should switch into a proposal/diff mode.

The user should be able to see:

- What Archivist wants to change.
- Why it wants to change it.
- Which files are affected.
- What text is being replaced.
- What new text is being created.
- Which files are being moved.
- Which files are being sent to Deprecated.
- Risk level.
- Confidence.
- Undo availability.

The user should be able to approve, reject, edit, or partially approve.

## 4.3 Diff UI Requirements

For move/rename operations, show:

- Original path
- New path
- Destination folder meaning
- Reason
- Whether the old path will remain
- Whether the file is going to Deprecated

For content changes, show:

- Before text
- After text
- Changed sections
- Source reasoning
- Related files used as context

For Deprecated moves, show:

- File being deprecated
- Why it is being deprecated
- What replaces it
- Where it will go
- Whether it remains searchable
- Whether undo is available

## 4.4 Approval Controls

The UI should support:

- Approve all
- Approve selected
- Reject all
- Skip item
- Edit destination
- Edit generated text
- Move to Deprecated
- Keep in place
- Save proposal for later
- Apply approved operations
- Undo after apply

## 4.5 User Trust Rule

The user should never wonder:

“What did it just do?”

Archivist should always be able to answer:

- What changed?
- Why did it change?
- When did it change?
- Who approved it?
- What was the original?
- Can I undo it?

---

# 5. Locked Tech Stack

The agreed stack for v1:

- Electron
- React + Vite
- TypeScript
- Express
- Zod
- SQLite
- Local vector DB

## 5.1 Desktop Shell

Use Electron.

Electron provides:

- Cross-platform desktop app support
- macOS, Windows, and Linux builds
- Native folder/file dialogs
- Ability to launch local backend services
- Ability to package the app as a normal desktop app
- A familiar web-based UI environment

Electron should mostly act as a shell around the local web app and backend.

Electron responsibilities:

- Start the local Express API
- Choose or receive an available dynamic local port
- Open the desktop window
- Load the React/Vite UI
- Provide minimal native helpers
- Handle app lifecycle
- Shut down services cleanly

Electron should not contain core business logic.

## 5.2 Frontend

Use:

- React
- Vite
- TypeScript
- Custom design system
- Custom token system

The UI is written from scratch.

The app should use a centralized design token system so the visual identity can be controlled from one place and later exposed to users for personalization.

Future personalization may include:

- Light/dark/custom themes
- Accent colors
- Density
- Font size
- Border radius
- Motion level
- Panel style
- Background style
- Workspace personality themes

The frontend should feel like a local AI workspace, not a generic SaaS dashboard.

## 5.3 Backend

Use:

- Node.js
- Express
- TypeScript
- Zod
- SQLite
- Local vector DB

The backend runs locally on the user’s machine.

The backend should expose a normal local HTTP API over:

`http://127.0.0.1:<dynamic-port>`

Do not bind to:

`0.0.0.0`

The API should use a dynamic port to avoid conflicts with developer tools or other local services.

The app should generate a local session token on startup and require it for local API requests.

Example request header:

`Authorization: Bearer <local-session-token>`

HTTPS is not required for the internal desktop app talking to itself over localhost.

HTTPS/tunneling can be added later for external bridge scenarios.

## 5.4 Why Express

Express is chosen because:

- It is familiar.
- It is widely used in Node jobs.
- It supports a normal API mental model.
- It avoids unnecessary framework ceremony.
- It lets us define our own architecture.
- It is easy to test with browser, curl, Postman, or internal clients.

Express should remain a thin transport layer.

Core business logic belongs in services, not routes.

## 5.5 Validation

Use Zod for:

- API request validation
- Shared schemas
- Runtime safety
- Type inference
- AI response validation where useful
- Internal data contracts

Important entities should have Zod schemas:

- Library
- LibraryPolicy
- FileRecord
- Chunk
- Proposal
- FileOperation
- Diff
- SearchResult
- ContextPacket
- AIProviderConfig
- WikiFile
- DeprecatedRecord

## 5.6 Database

Use SQLite for local structured data.

SQLite stores:

- Registered libraries
- File catalog records
- File metadata
- Scan history
- Chunk metadata
- Proposal records
- Operation ledger
- Settings
- AI provider settings
- Library status
- Search/cache metadata
- Deprecated metadata
- Wiki metadata

SQLite should be local only.

Recommended:

Global app DB:

- registered libraries
- app settings
- model/provider settings
- recent activity

Per-library DB:

- file catalog
- chunks
- proposals
- operation history
- library-specific search/index state
- deprecated records
- wiki state

## 5.7 Local Vector DB

Use a local vector database or local vector storage adapter.

The product requirement is:

- No paid hosted vector database.
- No cloud database dependency.
- Files and indexes stay local.

Candidate local vector stores:

- LanceDB
- Chroma
- Qdrant local
- SQLite vector extension
- Other swappable local vector solution

The implementation should use an adapter interface so the vector backend can be swapped later.

Conceptual interface:

- `upsertChunks(chunks)`
- `search(queryEmbedding, filters)`
- `deleteByFileId(fileId)`
- `rebuildLibraryIndex(libraryId)`

The exact v1 vector DB can be chosen during implementation, but it must be local-first.

## 5.8 AI Providers

The app should support a provider adapter system.

Initial likely providers:

- OpenAI with user API key
- Anthropic with user API key
- Ollama local
- LM Studio local
- Mock provider for testing

The user’s files stay local.

If a cloud provider is used, only selected context snippets are sent.

The app should eventually show:

- Cloud AI is enabled.
- Archivist will not upload your files.
- Only selected excerpts used for this answer may be sent to the model provider.
- View context being sent.
- Disable cloud AI.

The AI layer should be replaceable and provider-agnostic.

---

# 6. Architecture Overview

## 6.1 High-Level Shape

Electron Desktop Shell

→ React + Vite Frontend

→ Local Express API

→ Archivist Core Services

→ SQLite

→ Local Vector DB

→ Local Filesystem

→ AI Provider Adapters

Electron starts the app and local backend.

React talks to the backend through HTTP.

Express routes call core services.

Core services own the actual behavior.

SQLite and vector DB store local knowledge and app state.

The filesystem remains the source of truth for user files.

The `.archivist/` wiki explains the meaning of the Library.

## 6.2 API vs IPC Decision

The primary app architecture uses a normal local HTTP API.

Electron IPC should only be used for narrow desktop-shell utilities, such as:

- Open native folder picker
- Open a file in Finder/Explorer
- Get application data path
- Show native notification
- App lifecycle events

Do not put business logic in IPC.

Main business logic flows through the local Express API.

This keeps the architecture familiar, testable, and reusable for the future MCP bridge.

## 6.3 Modular Monolith

The app should be a modular monolith.

It is one local app, not a microservice swarm.

Good simple architecture:

- One app.
- One main backend.
- Clear modules.
- Clear service boundaries.
- Few runtimes.
- Few languages.

Avoid:

- Electron + Next.js + NestJS + Rust + Python + IPC + HTTP + MCP all in v1.

The architecture should be easy to understand.

Complexity must pay rent.

---

# 7. Suggested Project Structure

Initial structure:

archivist/
apps/
desktop/
electron/
main.ts
preload.ts
renderer/
index.html
src/
app/
components/
features/
styles/
tokens/
clients/
routes/
server/
src/
index.ts
api/
routes/
middleware/
controllers/
core/
libraries/
wiki/
policies/
scanner/
parser/
catalog/
vector/
ai/
context/
proposals/
diffs/
deprecated/
files/
ledger/
search/
db/
config/
utils/
shared/
schemas/
types/
constants/
package.json
README.md

This can be simplified at first, but the conceptual separation should remain.

---

# 8. Core Domain Concepts

## 8.1 Library

A Library is a user-selected folder that Archivist is allowed to manage according to policy.

A Library has:

- ID
- Name
- Root path
- `.archivist/` path
- AI wiki
- Local policy
- Local metadata/memory files
- File catalog
- Vector index
- Proposal history
- Operation history
- Deprecated history

Example:

Library:

- name: Shard Notes
- rootPath: `/Users/zach/Documents/Shard`
- archivistPath: `/Users/zach/Documents/Shard/.archivist`

Libraries are generic.

They are not hardcoded as game, startup, or research libraries.

The user and AI define the purpose and structure of each Library conversationally.

## 8.2 Library Memory Files

Each Library gets a local `.archivist/` folder.

Suggested structure:

- `soul.md`
- `policies.json`
- `structure.md`
- `context-cheatsheet.md`
- `glossary.md`
- `decisions.md`
- `deprecated.md`
- `ignore.md`
- `catalog.sqlite`
- `vectors/`
- `proposals/`
- `history/`

These files are human-readable where possible.

They teach the AI how to work inside that specific Library.

The database is for speed.

The memory files are for meaning.

## 8.3 Library Wiki Service

The Library Wiki Service is responsible for:

- Creating default AI wiki files.
- Reading wiki files into context.
- Updating wiki files through approved diffs.
- Validating wiki structure.
- Keeping policies machine-readable.
- Keeping meaning human-readable.
- Making the Library understandable to future AI sessions.

The wiki should not be edited silently.

AI-assisted updates to the wiki require proposal and approval.

## 8.4 Policy Engine

Responsible for:

- Enforcing permissions.
- Checking no-touch rules.
- Blocking unsafe operations.
- Validating proposals before execution.
- Preventing deletes in v1.
- Preventing code project modification.
- Checking Library scope boundaries.
- Ensuring Deprecated moves are approval-driven.
- Ensuring all file changes route through proposals.

Every file operation must pass through the Policy Engine.

No service should directly move, rename, or modify files without policy validation.

## 8.5 Deprecated Record

A DeprecatedRecord tracks material moved out of active use.

Fields:

- ID
- Library ID
- File ID
- Original path
- Deprecated path
- Reason
- Replacement path
- Proposal ID
- Operation ID
- Deprecated at
- Approved by user
- Reversible
- Hash before
- Hash after

Deprecated is a first-class state, not an afterthought.

---

# 9. Safety Rules

## 9.1 No Deletes in V1

Archivist should not delete files in v1.

Deletion is too risky.

Instead, it can suggest:

- Move to Deprecated
- Move to Archive
- Mark as Deprecated
- Move to Needs Review
- Preserve original

## 9.2 Deprecated Instead of Destructive Replacement

When a file is replaced, consolidated, superseded, or no longer current, Archivist should not destroy the old version.

The default behavior is:

- Create or keep the new active version.
- Move the old version into Deprecated.
- Record the reason.
- Record what replaced it.
- Keep it searchable.
- Avoid using it as current truth by default.

## 9.3 No Code Project Reorganization in V1

Archivist should not rearrange code projects.

This is a hard product boundary.

The app is for knowledge files first, not source code management.

Default no-touch markers:

- `.git`
- `node_modules`
- `package.json`
- `package-lock.json`
- `pnpm-lock.yaml`
- `yarn.lock`
- `tsconfig.json`
- `vite.config.*`
- `next.config.*`
- `src/`
- `app/`
- `components/`
- `pages/`
- `public/`
- `dist/`
- `build/`
- `.venv/`
- `__pycache__/`
- `Cargo.toml`
- `go.mod`
- `CMakeLists.txt`
- `*.xcodeproj`
- `*.sln`
- `*.csproj`
- `*.vcxproj`
- `*.uproject`
- `Binaries/`
- `Intermediate/`
- `Saved/`
- `DerivedDataCache/`

The app may eventually read documentation inside code projects, but it should not move or rename project files in v1.

## 9.4 Human Approval Required

Any file operation that modifies the filesystem must be previewed and approved.

Operations requiring approval:

- Move file
- Rename file
- Modify file contents
- Create generated file
- Archive file
- Move file to Deprecated
- Update memory files
- Apply organization proposal

The user should see:

- What will change
- Why it is suggested
- Which files are affected
- Risk level
- Before/after paths
- Diff for content changes
- Deprecated destination, if applicable
- Option to approve, skip, reject, or modify

## 9.5 Non-Destructive by Default

The app should preserve original data whenever possible.

Preferred behavior:

- Move instead of delete
- Deprecated instead of remove
- Archive instead of erase
- Create canon/index files instead of overwriting old drafts
- Preserve old notes in Drafts, Archive, or Deprecated
- Record every operation in the ledger

---

# 10. Main App Systems

## 10.1 Library Manager

Responsible for:

- Creating libraries
- Registering libraries
- Loading library metadata
- Creating `.archivist/` folder
- Creating default AI wiki files
- Reading/writing policy files
- Listing libraries
- Removing library registration without deleting files

Create Library flow:

1. User chooses folder.
2. Electron opens native folder picker.
3. Frontend sends selected path to local API.
4. API validates path.
5. LibraryManager creates `.archivist/`.
6. LibraryManager writes initial AI wiki files.
7. Library is registered in global SQLite.
8. UI shows Library dashboard.
9. User is invited to describe the Library.
10. Archivist proposes initial soul, structure, glossary, and policies.

## 10.2 Scanner

Responsible for:

- Walking Library folders
- Respecting ignore rules
- Detecting file types
- Detecting no-touch folders
- Hashing files
- Recording metadata
- Detecting changed/new/missing files
- Updating catalog records

Scanner should start with simple file support:

- `.md`
- `.txt`
- `.json`
- `.yaml`
- `.yml`

Later:

- `.pdf`
- `.docx`
- `.rtf`
- images
- audio transcripts
- video metadata

The scanner should not parse everything immediately.

It should catalog first, then parsing/indexing can happen as jobs.

## 10.3 Parser

Responsible for extracting text and structure from supported files.

For markdown:

- Extract title
- Extract headings
- Preserve heading hierarchy
- Preserve links
- Detect frontmatter
- Split into sections

For text:

- Read content
- Normalize encoding
- Chunk by size or semantic boundaries

Later parsers can support:

- PDF text extraction
- DOCX extraction
- OCR
- Image metadata
- Transcription

## 10.4 Catalog

The catalog is the local structured record of files.

It stores:

- File path
- Normalized path
- File name
- Extension
- Size
- Hash
- Created/modified timestamps
- Last scan time
- File status
- Touch policy
- Summary
- Detected topics
- Related files
- Indexing state
- Current/deprecated/archive state

The catalog lets Archivist know what exists without constantly rereading every file.

## 10.5 Chunker

Responsible for splitting file content into retrievable chunks.

Initial chunking strategy:

- Prefer markdown headings.
- Keep related sections together.
- Use fallback chunk size for long text.
- Store file path and heading metadata.
- Avoid chunks so large they pollute retrieval.
- Avoid chunks so small they lose context.

Each chunk should track:

- Chunk ID
- File ID
- Library ID
- Text
- Heading
- Chunk index
- Token estimate
- Vector ID
- File status
- Current/deprecated/archive state
- Created/updated timestamps

## 10.6 Embedding Provider

Responsible for converting chunks and queries into embeddings.

Provider options:

- Local embedding model
- Ollama embedding endpoint
- Cloud embedding provider with user key
- Mock embeddings for tests

Embeddings should be provider-agnostic.

The app should not assume OpenAI, Pinecone, or any hosted service.

## 10.7 Vector Store

Responsible for local semantic search.

Stores embeddings and metadata.

Search should support filters like:

- Library ID
- File path
- Folder
- File status
- Current/canon/draft/deprecated/archive
- Topic
- Modified date
- User-selected scope

Initial requirement:

- Search chunks semantically.
- Return ranked results.
- Include source file paths.
- Include snippets.
- Include metadata.

## 10.8 Search Service

Responsible for combining retrieval methods.

Search should eventually support:

- Vector search
- Keyword search
- Metadata filters
- Path-aware search
- Glossary-aware search
- Recent files
- User-pinned files
- Decision-aware search
- Deprecated-aware search

RAG should not blindly retrieve similar chunks.

It should retrieve with intent.

Example:

User asks:

“Find inconsistent Thresh lore.”

Search Service should:

- Search glossary for Thresh.
- Search exact keyword “Thresh.”
- Search semantically.
- Include related files.
- Include `decisions.md`.
- Include Drafts.
- Include Deprecated because inconsistency detection needs history.
- Prefer current canon when identifying the accepted direction.

Example:

User asks:

“What is the current Thresh origin?”

Search Service should:

- Prefer Canon.
- Include decisions.
- Include glossary.
- Avoid Deprecated unless needed for explanation.

## 10.9 Context Compiler

The Context Compiler assembles the context packet for each AI call.

Every AI call should include only relevant, compact context.

Inputs:

- User message
- Active Library
- Current mode/task
- Policies
- Relevant AI wiki files
- Relevant retrieved chunks
- Current proposal state
- Allowed tools
- Recent decisions
- Operation history if needed
- Deprecated state if relevant

Outputs:

- AI system/task prompt
- Context snippets
- Tool constraints
- Safety instructions
- Source metadata
- Optional “context being sent” preview for user

The Context Compiler is one of the most important systems in the app.

It lets the app stay generic while each Library becomes specialized through its AI wiki.

## 10.10 AI Harness

Responsible for:

- Calling AI providers
- Passing compiled context
- Managing tool schemas
- Parsing structured responses
- Validating responses with Zod
- Retrying/repairing malformed responses where safe
- Tracking token usage
- Showing what context may leave the machine
- Supporting local/cloud provider choices

AI providers should be adapter-based.

Conceptual interface:

- `complete(request)`
- `stream(request)`
- `getUsage()`

The AI Harness should not directly change files.

It can produce:

- Answers
- Summaries
- Classifications
- Proposed operations
- Conflict reports
- Draft content
- Structured proposal data
- AI wiki update proposals

Actual file changes go through proposals, policy, diffing, human approval, operation service, and ledger.

## 10.11 Proposal Engine

The Proposal Engine creates structured plans for possible changes.

Proposal types:

- OrganizationProposal
- RenameProposal
- MoveProposal
- IndexGenerationProposal
- ContentEditProposal
- CanonUpdateProposal
- ConflictReportProposal
- ArchiveProposal
- DeprecatedMoveProposal
- WikiUpdateProposal

Every proposal should include:

- ID
- Goal
- Affected files
- Proposed operations
- Reasons
- Risk level
- Confidence
- Preview data
- Diff data
- Deprecated destination data
- Approval state
- Undo plan
- Created timestamp

The Proposal Engine does not apply changes directly.

It creates plans.

The user approves plans.

The Operation Service applies approved plans.

## 10.12 Diff Engine

Responsible for showing before/after changes.

For file moves/renames:

From:

`/old/path/thresh messy idea.md`

To:

`/new/path/02_Races/Thresh/Drafts/Thresh - Swamp Origin Draft.md`

For Deprecated moves:

From:

`/Races/Thresh/old origin.md`

To:

`/Deprecated/Races/Thresh/old origin.md`

Reason:

`Superseded by Canon/Races/Thresh.md`

For content edits:

- Old statement: The Thresh were created directly by the Maw.
- New statement: The Thresh appear to be a people transformed by long exposure to Maw-corrupted wetlands.

The UI should allow:

- Approve
- Reject
- Modify destination
- Modify content
- Skip item
- Approve selected
- Save proposal for later

## 10.13 Operation Ledger

The Operation Ledger records every filesystem change.

Each operation should record:

- Operation ID
- Proposal ID
- Library ID
- Timestamp
- Action type
- Original path
- New path
- File hash before
- File hash after
- Approved by user
- Reversible status
- Reason
- Result
- Error if failed

Example action types:

- create_file
- modify_file
- move_file
- rename_file
- archive_file
- deprecate_file
- create_folder
- update_memory_file
- update_wiki_file

The ledger enables:

- Undo
- Audit trail
- User trust
- Future grandma git
- Future Git integration
- Future semantic history

## 10.14 File Operation Service

Responsible for applying approved operations.

Must:

- Validate proposal is approved.
- Validate policy allows operation.
- Validate paths are inside Library.
- Validate no-touch rules.
- Avoid overwriting files silently.
- Create needed folders.
- Record operation before/after.
- Roll back on failure where possible.
- Update catalog after changes.
- Trigger reindex if needed.
- Mark deprecated records when relevant.

This service is dangerous and should be designed carefully.

No random part of the app should directly call filesystem mutations.

## 10.15 Chat System

The chat UI is the main interaction surface.

The user should be able to ask:

- “What is in this Library?”
- “Find everything related to Thresh.”
- “Organize these notes into a fractal structure.”
- “Find inconsistent lore directions.”
- “Create an index for this folder.”
- “Rename these files more clearly, but preview first.”
- “Update the Library soul to prefer concept-first organization.”
- “Move outdated drafts to Deprecated.”
- “Show me what you are about to change before applying anything.”
- “What context are you sending to the AI provider?”

Chat responses should show:

- Answer
- Sources
- Relevant files
- Proposed actions
- Risk notes
- Diff previews
- Deprecated destinations
- Approval buttons where relevant

The chat system should support modes implicitly through intent, not separate rigid UI modes at first.

Possible internal modes:

- Search
- Riff
- Organize
- Summarize
- Conflict review
- Proposal editing
- Diff review
- Apply proposal
- Library setup
- AI wiki editing

## 10.16 UI System

Important screens:

- Welcome / Onboarding
- Add Library
- Library Dashboard
- Chat Workspace
- File Browser
- Search Results
- Proposal Preview
- Diff Viewer
- Deprecated Review
- Operation History
- Settings
- AI Provider Settings
- Library Wiki Editor
- Policy Editor

The UI should make local control obvious.

For cloud AI calls, eventually show:

- Provider used
- Estimated tokens
- Context snippets being sent
- Sources used
- Privacy warning/toggle

---

# 11. Local API Design

## 11.1 API Principles

The local API should be:

- Simple
- Typed
- Validated with Zod
- Bound to localhost
- Protected by startup token
- Thin over core services
- Easy to test
- Reusable for future MCP bridge

Routes should not contain business logic.

Routes call controllers or services.

## 11.2 Initial Route Groups

Suggested initial API route groups:

- `/libraries`
- `/wiki`
- `/scans`
- `/search`
- `/chat`
- `/proposals`
- `/operations`
- `/files`
- `/deprecated`
- `/settings`
- `/providers`

## 11.3 Example API Routes

Libraries:

- `GET /libraries`
- `POST /libraries`
- `GET /libraries/:libraryId`
- `PATCH /libraries/:libraryId`
- `POST /libraries/:libraryId/scan`

Wiki:

- `GET /libraries/:libraryId/wiki`
- `GET /libraries/:libraryId/wiki/:fileName`
- `POST /libraries/:libraryId/wiki/propose-update`

Search:

- `POST /search`
- `POST /libraries/:libraryId/search`

Chat:

- `POST /chat`
- `POST /chat/stream`
- `POST /libraries/:libraryId/chat`

Proposals:

- `POST /proposals`
- `GET /proposals/:proposalId`
- `PATCH /proposals/:proposalId`
- `POST /proposals/:proposalId/approve`
- `POST /proposals/:proposalId/apply`

Operations:

- `GET /operations`
- `GET /operations/:operationId`
- `POST /operations/:operationId/undo`

Deprecated:

- `GET /libraries/:libraryId/deprecated`
- `POST /libraries/:libraryId/deprecated/propose`

Files:

- `GET /files/:fileId`
- `POST /files/open`
- `POST /files/reveal`

Settings:

- `GET /settings`
- `PATCH /settings`

Providers:

- `GET /providers`
- `PATCH /providers/:providerId`
- `POST /providers/:providerId/test`

---

# 12. Data Model Sketch

## 12.1 Library

Library:

- id
- name
- rootPath
- archivistPath
- status
- createdAt
- updatedAt

## 12.2 LibraryPolicy

LibraryPolicy:

- libraryId
- read
- index
- summarize
- createFiles
- modifyFiles
- moveFiles
- renameFiles
- deleteFiles
- requireApprovalForAllFileChanges
- preserveOriginals
- moveSupersededFilesToDeprecated
- blockCodeProjectReorganization

## 12.3 WikiFile

WikiFile:

- id
- libraryId
- fileName
- path
- type
- summary
- lastReadAt
- lastUpdatedAt
- hash

## 12.4 FileRecord

FileRecord:

- id
- libraryId
- path
- normalizedPath
- name
- extension
- sizeBytes
- hash
- createdAt
- modifiedAt
- firstIndexedAt
- lastIndexedAt
- status
- touchPolicy
- summary
- libraryState

Possible library states:

- current
- canon
- draft
- archive
- deprecated
- ignored
- no_touch
- needs_review

## 12.5 ChunkRecord

ChunkRecord:

- id
- libraryId
- fileId
- chunkIndex
- heading
- text
- tokenEstimate
- vectorId
- libraryState
- createdAt
- updatedAt

## 12.6 Proposal

Proposal:

- id
- libraryId
- type
- goal
- status
- riskLevel
- summary
- createdAt
- updatedAt
- approvedAt
- appliedAt

Proposal statuses:

- draft
- ready_for_review
- approved
- partially_approved
- applied
- rejected
- failed

## 12.7 ProposedOperation

ProposedOperation:

- id
- proposalId
- type
- fromPath
- toPath
- fileId
- contentBefore
- contentAfter
- reason
- riskLevel
- status
- deprecatedReason
- replacementPath

Operation statuses:

- pending
- approved
- skipped
- applied
- failed

## 12.8 OperationLedgerEntry

OperationLedgerEntry:

- id
- proposalId
- proposedOperationId
- libraryId
- actionType
- originalPath
- newPath
- hashBefore
- hashAfter
- approvedByUser
- reversible
- reason
- createdAt
- result
- error

## 12.9 DeprecatedRecord

DeprecatedRecord:

- id
- libraryId
- fileId
- originalPath
- deprecatedPath
- reason
- replacementPath
- proposalId
- operationId
- deprecatedAt
- approvedByUser
- reversible
- hashBefore
- hashAfter

---

# 13. Key User Workflows

## 13.1 Add Library

1. User clicks “Add Library.”
2. Electron opens native folder picker.
3. User selects folder.
4. Frontend sends path to API.
5. API validates folder.
6. LibraryManager creates `.archivist/`.
7. Default AI wiki files are created.
8. Library is registered.
9. User is invited to describe the Library.
10. AI helps fill or refine `soul.md`, `structure.md`, `context-cheatsheet.md`, and `policies.json`.

## 13.2 Scan Library

1. User starts scan.
2. Scanner walks folder.
3. Ignore rules are applied.
4. No-touch detection runs.
5. Supported files are cataloged.
6. Hashes and metadata are stored.
7. Changed/new/missing files are detected.
8. UI shows scan summary.

Example scan output:

- 214 files scanned.
- 163 markdown files indexed.
- 12 files ignored by policy.
- 7 possible code-project folders skipped.
- 39 files need analysis.

## 13.3 Analyze Library

1. Parser extracts text from supported files.
2. Chunker splits files.
3. AI or local heuristics summarize files.
4. Topics/entities are detected.
5. Embeddings are generated.
6. Vector index is updated.
7. Catalog records are updated.
8. Deprecated/current/canon/draft state is stored.

## 13.4 Chat With Library

1. User sends message.
2. Context Compiler loads relevant AI wiki files.
3. Search Service retrieves relevant chunks.
4. Search respects current/draft/deprecated intent.
5. AI Harness sends compact context to selected model.
6. Response includes sources.
7. UI displays answer and relevant files.

## 13.5 Organize Library

1. User asks to organize a Library.
2. AI reads Library wiki and current catalog.
3. Search/analysis identifies clusters.
4. Proposal Engine creates organization proposal.
5. UI displays folder tree preview.
6. UI displays move/rename operations.
7. UI highlights files proposed for Deprecated.
8. User approves, edits, or rejects.
9. File Operation Service applies approved operations.
10. Operation Ledger records everything.
11. Deprecated records are created where relevant.
12. Search index updates.

## 13.6 Replace or Consolidate Files

1. User asks Archivist to consolidate messy notes.
2. Archivist identifies overlapping files.
3. Archivist proposes a new clean file.
4. Diff mode shows the generated file.
5. Diff mode shows old files proposed for Deprecated.
6. User reviews what is being replaced.
7. User approves selected operations.
8. New file is created.
9. Old files are moved to Deprecated.
10. Ledger records everything.
11. Deprecated records explain what happened.

## 13.7 Find Inconsistencies

1. User asks for inconsistencies.
2. Context Compiler includes AI wiki files, glossary, decisions, and relevant chunks.
3. Search Service retrieves related canon, drafts, and deprecated files.
4. AI compares claims/directions.
5. App generates Conflict Report Proposal.
6. User chooses which direction to keep.
7. Archivist can propose updates to canon/index/decision files.
8. Superseded files can be proposed for Deprecated.
9. User previews diffs.
10. User approves changes.
11. Ledger records changes.

## 13.8 Update the AI Wiki

1. User says something like:
   “Going forward, organize this Library concept-first and preserve old drafts.”
2. Archivist proposes updates to wiki files.
3. Diff mode shows changes to `soul.md`, `structure.md`, or `context-cheatsheet.md`.
4. User approves.
5. Wiki files update.
6. Ledger records update.
7. Future AI behavior changes.

## 13.9 Undo

1. User opens operation history.
2. User selects operation or proposal.
3. App validates reversibility.
4. App previews undo.
5. User approves.
6. File Operation Service reverses changes.
7. Ledger records undo operation.
8. Catalog/index updates.

---

# 14. MVP Scope

## 14.1 V1 Must Have

V1 is done when Archivist can:

- Launch as a desktop app.
- Start a local Express API.
- Create/register one or more libraries.
- Create `.archivist/` AI wiki files.
- Scan markdown/text files.
- Store file metadata in SQLite.
- Parse/chunk supported files.
- Generate or store embeddings in a local vector DB.
- Search a Library semantically.
- Chat with a Library using retrieved context.
- Show source files for answers.
- Use the AI wiki in context compilation.
- Generate a simple organization proposal.
- Preview move/rename operations.
- Preview files being moved to Deprecated.
- Apply approved operations.
- Record operations in ledger.
- Undo basic move/rename operations.

## 14.2 V1 Should Not Include

Do not build these in v1:

- Whole-computer auto-cleaner
- File deletion
- Code project reorganization
- Image understanding
- OCR
- Full MCP bridge
- Cloud database
- Hosted accounts/auth
- Collaboration
- Git integration
- Mobile app
- Complex plugin system

---

# 15. Future Features

## 15.1 MCP / ChatGPT Bridge

Future feature:

Expose a safe local tool bridge so ChatGPT, Claude, or other AI systems can query approved libraries.

Safe tools:

- `list_libraries()`
- `search_library()`
- `get_relevant_context()`
- `get_file_summary()`
- `create_proposal()`
- `preview_proposal()`
- `request_local_approval()`

Unsafe tools should not be exposed remotely:

- `write_any_file()`
- `read_any_file()`
- `delete_file()`
- `run_shell_command()`

External bridge operations should remain Library-scoped and approval-driven.

## 15.2 Discovery Mode

Future feature:

Scan common folders and suggest possible libraries.

Example:

Potential Libraries Found:

1. Shard Notes
2. Startup Notes
3. Homestead Planning
4. Downloads Inbox

The app should ask the user which folders to add as libraries.

Do not start by scanning the whole machine automatically.

## 15.3 Image Knowledge

Future feature:

Support images and visual search.

Example user query:

“Where is that picture of Johnny with the big fish?”

This requires:

- Image indexing
- Metadata extraction
- Possibly local vision model
- Thumbnails
- Face/object/event search
- Strong privacy controls

Not v1.

## 15.4 Grandma Git

Future feature:

A simple human-friendly versioning layer.

User-facing concepts:

- Snapshots
- Restore points
- Undo
- Before cleanup
- After cleanup
- Named versions

Not shown as raw Git to normal users.

Power-user future:

- Git init
- Semantic commits
- Semantic branches
- Compare versions
- Merge branches
- Export patch

Example semantic commit:

“Organized Thresh notes and deprecated old origin drafts.”

---

# 16. Development Philosophy

The app should be built with UX and DX as north stars.

Good architecture means:

- Easy to understand
- Easy to debug
- Easy to test
- Easy to extend
- Easy for users to trust

No complexity unless it provides real value.

Avoid complexity theater.

The app itself is about turning chaos into clarity.

The codebase should reflect that.

Preferred architecture style:

- Modular monolith.
- Thin routes.
- Clear services.
- Strong schemas.
- Local-first.
- Human-approved operations.
- Non-destructive file handling.
- AI wiki as Library memory.

---

# 17. Logical Todo List

## 1. Project Foundation

### 1a. Create repo and basic workspace

Set up:

- Electron
- React
- Vite
- TypeScript
- Express
- Zod
- SQLite

Create initial folders:

- `apps/desktop`
- `server`
- `shared`

### 1b. Start desktop shell

Electron should:

- Launch app window.
- Start local Express server.
- Use dynamic port.
- Pass API URL/token to renderer.
- Shut down cleanly.

### 1c. Build local API skeleton

Add:

- Express app
- Health route
- Local auth token middleware
- Error handler
- Zod validation helper
- Basic logging

## 2. Shared Types and Schemas

### 2a. Define core schemas

Create Zod schemas for:

- Library
- LibraryPolicy
- WikiFile
- FileRecord
- ChunkRecord
- Proposal
- ProposedOperation
- OperationLedgerEntry
- DeprecatedRecord
- SearchResult
- AIProviderConfig

### 2b. Define API contracts

Create request/response schemas for:

- createLibrary
- scanLibrary
- searchLibrary
- createProposal
- approveProposal
- applyProposal
- undoOperation
- proposeWikiUpdate
- proposeDeprecatedMove

### 2c. Set up frontend API client

Create a typed client used by React.

The frontend should not call fetch randomly throughout components.

## 3. Library Creation and AI Wiki

### 3a. Native folder picker

Use minimal Electron IPC only for folder selection.

### 3b. Create Library API

Build endpoint:

`POST /libraries`

It should:

- Validate path.
- Create Library record.
- Create `.archivist/`.
- Create default AI wiki files.
- Store Library in global SQLite.

### 3c. Default AI wiki files

Create:

- `soul.md`
- `policies.json`
- `structure.md`
- `context-cheatsheet.md`
- `glossary.md`
- `decisions.md`
- `deprecated.md`
- `ignore.md`

### 3d. Library dashboard

UI should show:

- Library name
- Root path
- Scan status
- Policy summary
- AI wiki status
- Buttons for scan/search/chat/settings

## 4. SQLite Persistence

### 4a. Choose SQLite driver

Recommended candidates:

- better-sqlite3
- drizzle
- kysely

Keep it simple.

### 4b. Create global DB

Store:

- Libraries
- App settings
- Provider settings

### 4c. Create per-library DB

Store:

- File catalog
- Chunks
- Proposals
- Operations
- Deprecated records
- Wiki metadata

## 5. Scanner and Catalog

### 5a. Implement file scanner

Start with:

- `.md`
- `.txt`
- `.json`
- `.yaml`
- `.yml`

Respect:

- `.archivist/`
- ignore rules
- no-touch patterns

### 5b. Store file records

For each file, store:

- Path
- Name
- Extension
- Size
- Hash
- Modified timestamp
- Status
- Library state

### 5c. Add scan UI

Show:

- Files scanned
- Files ignored
- Files changed
- Errors
- No-touch warnings

## 6. Parser and Chunker

### 6a. Markdown parser

Extract:

- Title
- Headings
- Body text
- Frontmatter if present

### 6b. Chunk files

Use heading-aware chunks first.

Fallback to size-based chunks.

### 6c. Store chunks

Save chunks in SQLite with file metadata and Library state.

## 7. Local Vector Index

### 7a. Pick local vector DB

Start with the simplest viable local option.

Likely:

- LanceDB

But keep a VectorStore interface.

### 7b. Add embedding provider

Start with one provider.

Options:

- OpenAI user key
- Ollama local
- Mock provider for tests

Even if OpenAI is first, storage remains local.

### 7c. Index chunks

Generate embeddings and upsert to local vector DB.

## 8. Search

### 8a. Semantic search endpoint

Build:

`POST /libraries/:libraryId/search`

Return:

- Matching chunks
- File paths
- Snippets
- Scores
- Metadata
- Current/draft/deprecated state

### 8b. Search UI

Show:

- Search box
- Results
- Source file path
- Snippet
- Open/reveal buttons
- Current/deprecated badge

### 8c. Deprecated-aware search

Search should treat Deprecated material differently depending on intent.

Default answers should prefer current/canon material.

Contradiction/history/recovery tasks may include Deprecated.

## 9. AI Harness and Context Compiler

### 9a. Provider settings

Let user configure:

- OpenAI API key
- Local provider URL
- Model choice

### 9b. Context Compiler v1

For a chat request, include:

- User message
- Relevant AI wiki files
- Retrieved chunks
- Source metadata
- Allowed behavior
- Current/deprecated search intent

### 9c. Chat endpoint

Build:

`POST /libraries/:libraryId/chat`

Return answer plus sources.

Streaming can come later.

## 10. Chat UI

### 10a. Basic chat workspace

UI includes:

- Message list
- Input
- Active Library
- Source panel
- Model/provider indicator

### 10b. Source display

Every grounded answer should show source files.

### 10c. Context preview later

Add “view context sent to AI” for privacy/trust.

## 11. Proposal Engine

### 11a. Proposal schema

Implement:

- Proposal
- ProposedOperation
- Statuses
- Risk levels

### 11b. Simple organization proposal

Given selected files, generate proposed moves/renames.

Start rule-based before going fully AI-driven if needed.

### 11c. Deprecated move proposal

Support proposals that move superseded files to Deprecated.

This must include reason, replacement path, and undo plan.

### 11d. Save proposal

Store proposal in SQLite and `.archivist/proposals/` if useful.

## 12. Diff and Preview UI

### 12a. Move/rename preview

Show before/after paths.

### 12b. Content diff preview

For generated or modified markdown files, show text diff.

### 12c. Deprecated preview

Show what files are being moved to Deprecated and why.

### 12d. Approval controls

Allow:

- Approve all
- Approve selected
- Skip
- Edit destination
- Reject proposal
- Save for later

## 13. Apply and Undo

### 13a. File Operation Service

Apply only approved operations.

Validate through Policy Engine.

### 13b. Operation Ledger

Record every change.

### 13c. Undo basic operations

Support undo for:

- Move file
- Rename file
- Create file where safe
- Create folder where safe
- Move to Deprecated

## 14. Library Wiki Editing

### 14a. View wiki files

UI can show:

- `soul.md`
- `structure.md`
- `context-cheatsheet.md`
- `glossary.md`
- `decisions.md`
- `deprecated.md`
- `ignore.md`
- `policies.json`

### 14b. Edit wiki files

User can edit manually.

### 14c. AI-assisted updates

User can say:

“Going forward, organize this Library concept-first and preserve old drafts.”

Archivist proposes an update to wiki files.

User approves via diff.

## 15. Hardening

### 15a. Safety tests

Test:

- Cannot delete files.
- Cannot move files outside Library.
- Cannot touch ignored paths.
- Cannot modify code project folders.
- Cannot apply unapproved proposal.
- Cannot move files to Deprecated without approval.
- Cannot silently update AI wiki files.

### 15b. Error handling

Make failed operations recoverable and clearly reported.

### 15c. Backup/restore sanity

Ensure operation ledger can recover from partial failures.

## 16. Future Bridge

### 16a. Design safe tool API

Expose only Library-scoped tools.

### 16b. Add local bridge server

Optional stable port or user-started bridge.

### 16c. Add MCP-style compatibility

External AI can query but not directly mutate without local approval.

---

# 18. Final North Star

Build the smallest clear system that gives us real powers:

- Create a Library.
- Teach Archivist what it is through an AI wiki.
- Explain the directory structure so AI understands context, not just filenames.
- Scan and index files locally.
- Chat with the Library.
- Search it.
- Propose changes.
- Preview diffs.
- Show what gets replaced.
- Move superseded files to Deprecated instead of deleting them.
- Approve changes.
- Record history.
- Undo mistakes.

Everything else comes later.

The Archivist is a local AI librarian with memory, manners, and a paper trail.
