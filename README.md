# The Archivist Design Doc

## Purpose of This Document

This document is the north-star seed for building The Archivist.

Use it to quickly bring a new AI/chat session up to speed, guide development, prevent scope drift, and preserve the important product decisions already made. It should be useful both as a project README and as a context seed for future implementation chats.

The most important idea:

**The Archivist is a local-first AI librarian and creative workbench. It turns messy folders into organized, searchable, AI-readable Libraries while keeping the human in control of every meaningful change.**

---

# 0. Current Development Setup

## Run from Root

```bash
cd archivist
nvm use
npm run dev
```

The current development setup uses a root workspace package so the project can be managed from one place.

Current intended shape:

```text
archivist/
  package.json
  package-lock.json
  .nvmrc
  .npmrc
  frontend/
    package.json
    electron/
      main.cjs
      preload.cjs
    src/
      App.tsx
      main.tsx
      components/
      features/
      styles/
      types/
  backend/
    package.json
    src/
      index.ts
      routes/
      core/
  shared/
    schemas/
    types/
```

Current daily dev flow:

```bash
nvm use
npm run dev
```

This should start:

```text
Backend Express API
Vite React frontend
Electron desktop shell
```

The app is developed inside the Electron window with Vite HMR. The browser version is useful for debugging, but the Electron window is the real target environment.

## Version Policy

Use boring, safe, currently supported versions.

Current target:

```text
Node: 24 LTS
Electron: 42.6.x stable line
React: 19 stable
TypeScript: current stable installed by project
Vite: current stable installed by project
```

Project should enforce Node 24 through:

```text
.nvmrc
package.json engines
.npmrc engine-strict=true
optional check-node script
```

The goal is to avoid Node version drift across projects.

---

# 1. Prioritized Feature Roadmap

This section exists so development can proceed without having to reread the full design doc every time.

## P0: Project Foundation and UI Shell

These are the immediate working steps.

1. Root workspace DX
   - Run backend, frontend, and Electron from one root command.
   - Enforce Node version.
   - Keep dependency list lean.
   - Maintain one lockfile.

2. Electron + React shell
   - Electron app opens the Vite frontend.
   - HMR works inside Electron.
   - Native folder picker works through preload/IPC.
   - Core UI is developed in the app, not only the browser.

3. Port/refactor the existing chat UI from Vue to React
   - Navbar.
   - Sidebar.
   - Sidebar collapse/reveal.
   - Chat window.
   - Composer.
   - CSS variable theme system.
   - Static mock data first.

4. Basic chat shell
   - Message list.
   - User and assistant bubbles.
   - Empty states.
   - Sticky composer.
   - Send on Enter, newline with Shift+Enter.
   - Mock assistant responses.

## P1: Rich Chat Workbench

Make the chat view more than text bubbles.

1. Rich message blocks
   - Markdown block.
   - Code block with copy button.
   - Source card block.
   - File attachment block.
   - Image preview block.
   - Proposal/diff block.
   - Context packet block.

2. Better composer
   - Attach file.
   - Mention files/folders later.
   - Slash commands later.
   - Optional formatting helpers.
   - Manual copy/paste friendly.

3. Source and context visibility
   - Show sources used by each answer.
   - Show context being sent to the AI.
   - Allow copy context packet.
   - Allow pin/exclude source later.

4. Streaming responses
   - Start with normal request/response.
   - Add Server-Sent Events for token streaming.
   - Add WebSockets later for long-running jobs and progress events.

## P2: Library Creation and AI Wiki

Turn folders into Libraries.

1. Add Library flow
   - User selects folder.
   - Backend validates path.
   - App creates `.archivist/`.
   - App writes default AI wiki files.
   - Library is registered in SQLite.

2. AI wiki files
   - `soul.md`
   - `policies.json`
   - `structure.md`
   - `context-cheatsheet.md`
   - `glossary.md`
   - `decisions.md`
   - `deprecated.md`
   - `style-guide.md`
   - `art-direction.md`
   - `taste.md`
   - `ignore.md`

3. Library dashboard
   - Library name.
   - Root path.
   - Scan status.
   - Policy summary.
   - AI wiki status.
   - Open chat.
   - Scan/index controls.

## P3: Local Persistence and Main Timeline

Archivist should remember conversations without forcing the user to constantly restart context.

1. SQLite storage
   - Libraries.
   - Files.
   - Chat sessions.
   - Messages.
   - Message chunks.
   - AI profiles.
   - Proposals.
   - Operations.
   - Deprecated records.

2. Persistent Main Timeline
   - Each Library has a default long-running Main Timeline.
   - Load recent messages by default.
   - Infinite scroll upward for older history.
   - Paginate older messages.
   - Summarize older chunks for retrieval.
   - Never blindly send the full forever-chat to the AI.

3. Alternate chats
   - Lore Riffage.
   - Quest Workshop.
   - Character Chamber.
   - Canon Review.
   - Proposal Review.
   - Research Thread.
   - User can create more later.

## P4: AI Harness and Provider Layer

Connect to real AI without coupling the app to one vendor.

1. Provider adapter system
   - Mock provider.
   - OpenAI with user key.
   - Anthropic later.
   - Ollama local.
   - LM Studio local.

2. Context Compiler
   - Compiles relevant AI wiki files.
   - Includes recent chat.
   - Includes retrieved file chunks.
   - Includes relevant old chat summaries.
   - Includes active AI profile instructions.
   - Includes policy/tool constraints.
   - Shows what context may leave the machine.

3. Structured AI responses
   - Normal markdown for normal answers.
   - Structured blocks for code, sources, proposals, diffs, files, context packets, and artifacts.
   - Validate structured responses with Zod.

## P5: Scanner, Parser, Search, and Vector Retrieval

Make Libraries searchable and AI-readable.

1. Scanner
   - Walk selected Library folder.
   - Respect ignore rules.
   - Skip no-touch folders.
   - Catalog supported file types.
   - Store file metadata and hashes.

2. Parser/chunker
   - Markdown.
   - Text.
   - JSON/YAML.
   - Later PDF/DOCX.
   - Chunk content for retrieval.

3. Local vector store
   - Embeddings for chunks.
   - Semantic search.
   - Search filters.
   - Current/draft/deprecated awareness.

4. Hybrid search
   - Keyword search.
   - Semantic search.
   - Path-aware search.
   - Glossary-aware search.
   - Decision-aware search.
   - Deprecated-aware search.

## P6: Human-in-the-Loop Proposal and Diff System

This is the safety heart of Archivist.

1. Proposal Engine
   - Organization proposals.
   - Move/rename proposals.
   - Content edit proposals.
   - Canon update proposals.
   - AI wiki update proposals.
   - Deprecated move proposals.
   - Conflict reports.

2. Diff UI
   - Before/after text.
   - Before/after paths.
   - Deprecated destination preview.
   - Reason and risk level.
   - Approve/reject/edit/skip.

3. Operation Ledger
   - Every file operation is recorded.
   - Undo supported for safe reversible operations.
   - No silent filesystem mutation.

## P7: AI Profiles and Creative Councils

Reusable AI personalities and roles that work inside Library context.

1. AI Profiles
   - Archivist.
   - Worldbuilder.
   - Continuity Keeper.
   - Quest Architect.
   - Style Auditor.
   - Art Director.
   - Character profiles.
   - Startup/product/recruiter profiles for non-lore Libraries.

2. Profile behavior
   - Role.
   - Personality.
   - Voice.
   - Priorities.
   - Forbidden behaviors.
   - Context preferences.
   - Permissions.
   - Profile picture/avatar.

3. Multi-AI chat modes
   - Single AI.
   - Summon specific AI with mentions.
   - Panel discussion.
   - Round robin.
   - Critique mode.
   - Character chamber.

Profiles may answer, brainstorm, critique, roleplay, and propose actions. They do not directly modify files.

## P8: Creative Engines

Build tools that grow the Library, especially for lore, writing, games, and worldbuilding.

1. Lore Riffage
   - Brainstorm within setting context.
   - Include Drafts and Deprecated when useful.
   - Extract canon candidates.
   - Track open questions.

2. Quest Builder
   - Hook.
   - NPCs.
   - Location.
   - Objectives.
   - Branches.
   - Rewards.
   - Failure states.
   - World consequences.

3. Character Chamber
   - Talk to in-world characters.
   - Test voice.
   - Simulate reactions.
   - Export profiles later for game AI.

4. City Planner / Worldbuilder
   - Districts.
   - Trade.
   - Religion.
   - Crime.
   - Infrastructure.
   - Factions.
   - Hooks.

5. Faction/NPC simulator
   - Run turns.
   - Generate consequences.
   - Create rumors.
   - Discover quest hooks.
   - Mine emergent lore.

## P9: Codex, Beautiful Export, and Taste Learning

Turn Library content into beautiful reading artifacts.

1. Morning Codex
   - Generate morning coffee reading material from Library context.
   - Chapter, myth, quest tale, faction history, travelogue, scene, or lore entry.
   - Save as Maybe/Draft/Canon only with user approval.
   - Rate and refine.

2. Professional PDF export
   - Lore bible.
   - Quest packet.
   - City folio.
   - Faction dossier.
   - Character dossier.
   - World bible.
   - Old English / codex / grimoire visual style.

3. Read Mode / Codex Mode
   - Beautiful rendered view.
   - Drop caps.
   - Section dividers.
   - Images.
   - Typography.
   - Callouts.
   - Appendices.
   - Glossary.

4. Art direction and images
   - `art-direction.md`.
   - Entity portraits.
   - AI profile pictures.
   - NPC portraits.
   - Faction sigils.
   - Scene illustrations.
   - Illustration suggestions.

5. Taste memory
   - `taste.md`.
   - `style-guide.md`.
   - Ratings and feedback.
   - Style linting.
   - No em dashes if forbidden.
   - No fancy punctuation if disabled.
   - Conform subtly to user taste over time.

## P10: Polish, Local Tool Bridges, and Advanced UI

Advanced but aligned polish.

1. Ambient UI system
   - Glassmorphic cards.
   - Subtle smoky/void background.
   - Depth layers.
   - Motion.
   - Atmospheric but not distracting.

2. Local tool bridges
   - Open in VS Code.
   - Reveal in Finder/Explorer.
   - Copy path.
   - Copy context packet.
   - Copy as markdown/JSON/prompt.
   - Save output as file.
   - Export selected output.

3. Media and file previews
   - PDF previews.
   - DOCX extraction.
   - Image previews.
   - Audio transcription.
   - Text-to-speech.
   - Voice-to-text.
   - Three.js 3D model viewer for `.glb` / `.gltf` first.

4. Future bridge
   - MCP-style local bridge.
   - Safe tools only.
   - Library-scoped access.
   - No remote direct mutation without local approval.

## P11: Modular Workspace and Tool Registry

This is how Archivist avoids becoming twenty separate chat apps.

1. One workspace house
   - Libraries.
   - Chats.
   - AI profiles.
   - Tools.
   - Workflows.
   - Providers.
   - Artifacts.
   - Proposals.
   - Operation ledger.

2. Internal module boundaries
   - Librarian module.
   - Worldbuilding module.
   - Codex export module.
   - Code Forge module.
   - Image Studio module.
   - NPC Lab module.
   - UE5 Bridge module later.
   - Opportunity Watcher module later.
   - Momentum Planner module later.

3. Tool Registry skeleton
   - Tools have Zod-validated inputs and outputs.
   - Tools declare risk level.
   - Tools declare required permissions.
   - Dangerous tools require approval.
   - AI agents request tools; they do not bypass policy.

4. Workflow Engine
   - Tools become steps.
   - Steps become chains.
   - Chains become workflows.
   - Workflows become scheduled routines.
   - Every run is logged, inspectable, and rerunnable where possible.

## P12: Proactive Routines and Daily Briefs

Archivist should eventually become proactive within strict limits.

1. Manual daily brief first
   - Summarize recent Library changes.
   - Surface open questions.
   - Suggest next actions.
   - Generate one optional lore snippet.
   - Show routine outputs in an Inbox.

2. Scheduled routines later
   - Lore Watcher.
   - Code Watcher.
   - Opportunity Watcher.
   - Tech/World Watcher.
   - Library Health Check.
   - Morning Codex.
   - Momentum Coach.

3. Routine rules
   - Scoped.
   - Permissioned.
   - Logged.
   - Non-destructive.
   - Local-model friendly.
   - No silent file changes.
   - Outputs go to the Routine Inbox for review.

4. Notifications
   - Dashboard card first.
   - App notification later.
   - Desktop notification later.
   - Text/email only when explicitly configured.
   - Quiet hours and max-ping limits.

## P13: Model Router and Local AI Workbench

Archivist should eventually route tasks across local and cloud models.

1. Model Router
   - Local/private mode.
   - Balanced mode.
   - Strongest mode.
   - Cheapest mode.
   - Custom routing by task type.

2. Local model use cases
   - Overnight scans.
   - File summaries.
   - First-pass code review.
   - Lore contradiction detection.
   - Routine brief drafts.
   - Novelty suggestions.
   - Low-cost background loops.

3. Cloud model use cases
   - Hard reasoning.
   - Final prose polish.
   - Deep architecture plans.
   - Complex code planning.
   - Premium image/text generation.

4. Long-term workstation goal
   - One local AI workspace with the full gamut of local and outside models at the user’s fingertips.
   - Avoid bouncing between twenty different AI apps.

## P14: Momentum Planner and Personal OS Layer

This should remain optional and user-controlled.

1. Lazy planning
   - Loose time blocks.
   - Obligations.
   - Health reminders.
   - Creative momentum nudges.
   - Project continuity checks.

2. Tone configuration
   - Gentle.
   - Coach.
   - Drill Sergeant Lite.
   - Goblin Wrangler.
   - Silent Dashboard Only.

3. Scope
   - This is not a minute-by-minute scheduling prison.
   - It should help the user keep promises to themselves.
   - It should preserve creative momentum without becoming intrusive.

## P15: Shard Lab and Deep Workflow Integration

This is the personal wizard build, not the first public product.

1. Shard-specific workflows
   - Worldbuilding forge.
   - Character chamber.
   - AI NPC profile lab.
   - Morning Codex.
   - Myth and artifact generator.
   - Quest builder.
   - Faction simulator.

2. UE5 bridge later
   - Generate artifact spec.
   - Generate icon/concept prompt.
   - Create placeholder asset folder.
   - Create DataAsset metadata.
   - Place placeholder actor in a test level.
   - Always preview and approve before mutating game project files.

3. Public product separation
   - Archivist Core remains clean and useful.
   - Shard Lab can be a private module/edition.
   - Specialized product editions can reuse the same platform without forking the codebase.

---

# 2. Simple Product Description

The Archivist is a local-first desktop app that turns messy personal knowledge folders into organized, searchable, AI-readable Libraries.

A user can add any folder as a Library, teach Archivist what that Library is, and then chat with an AI assistant that can search, summarize, organize, rename, move, compare, create, and maintain that Library through human-approved proposals.

The Archivist does not upload the user’s full Library to a hosted database. Files, indexes, metadata, operation history, chat history, AI wiki files, and vector storage stay local. Cloud AI providers may be used with user-provided API keys, but only selected context snippets are sent, never the entire Library by default.

The long-term vision is a local AI librarian and creative workbench that can:

- Organize personal knowledge.
- Preserve decisions.
- Detect inconsistencies.
- Maintain canon.
- Explain folder context.
- Support long-running Library conversations.
- Host reusable AI profiles.
- Generate creative writing, quests, lore, and structured artifacts.
- Export beautiful PDFs and codex-style documents.
- Expose safe Library access to external AI tools through a future bridge.
- Eventually offer “grandma git” style rollbacks and semantic versioning for nontechnical users.

The Archivist is not a magic file janitor.

It is a librarian.

It reads the room, understands the Library, proposes careful changes, shows its work, asks permission, preserves what came before, and keeps a record of every operation.

---

# 3. Core Product Definition

## 3.1 What We Are Building

The Archivist is a desktop app for managing personal knowledge Libraries.

A Library is any user-selected folder that contains meaningful files the user wants to organize, search, understand, and evolve over time.

Examples:

- Game lore notes.
- Startup notes.
- Research folders.
- Obsidian vaults.
- Writing projects.
- Homestead planning folders.
- Personal documents.
- Creative project folders.
- Future photo/document archives.

The app is intentionally generic. It does not hardcode concepts like “Game Design Library,” “Startup Library,” or “Homestead Library.”

Instead, each Library gets its own local `.archivist/` folder containing:

- AI wiki files.
- Policy files.
- Structure notes.
- Context cheat sheets.
- Glossaries.
- Decision logs.
- Style guides.
- Art direction.
- Taste memory.
- Ignore rules.
- Proposals.
- Operation history.
- Local databases.
- Local indexes.

The app starts generic, then becomes specialized per Library through user conversation, local metadata, and approved memory updates.

Each Library teaches Archivist how to behave.

## 3.2 Core Product Promise

Archivist helps users:

- Create local AI-readable Libraries from messy folders.
- Search and chat with their own files.
- Understand what is inside a folder without manually opening everything.
- Organize files into durable human-readable structures.
- Preserve context about why the folder exists.
- Preserve current canon, deprecated drafts, and old directions.
- Maintain long-running Library conversations.
- Create reusable AI profiles that work inside Library context.
- Generate structured creative artifacts from Library knowledge.
- Preview all proposed file changes before anything happens.
- Apply changes only after human approval.
- Move superseded files into Deprecated instead of deleting them.
- Undo file operations through a local operation ledger.
- Preserve user control and local ownership of data.
- Export beautiful documents, PDFs, lore bibles, and codex-style artifacts later.
- Eventually expose safe Library access to external AI tools like ChatGPT through a bridge.

## 3.3 Core Philosophy

The Archivist is a librarian, not an omnipotent janitor.

It should:

- Catalog.
- Search.
- Summarize.
- Group.
- Suggest.
- Explain.
- Preview.
- Ask for approval.
- Preserve history.
- Move old material to Deprecated instead of deleting it.
- Undo changes.
- Maintain the Library’s AI wiki.
- Maintain long-running Library timelines.
- Support creative exploration without confusing drafts for canon.
- Let the user decide what becomes durable memory.

It should not:

- Freestyle the user’s filesystem.
- Delete files in v1.
- Rearrange code projects in v1.
- Modify files without approval.
- Hide what it is doing.
- Upload data without showing what context is being sent.
- Treat filenames as the only source of truth.
- Lose old ideas just because the current direction changed.
- Treat generated creative material as canon by default.
- Let AI profiles directly mutate files.

The product principles are:

1. Human-readable.
2. AI-readable.
3. Reversible.
4. Permissioned.
5. Non-destructive by default.
6. Local-first.
7. Context-aware.
8. Creative, but controlled.

---

# 4. Core Mental Model

## 4.1 Four Memory Layers

Archivist has several memory layers that each serve a different job.

```text
Filesystem = source of truth
AI Wiki = meaning
SQLite = records and history
Vector DB = semantic retrieval
```

More poetically:

```text
The filesystem is the body.
The wiki is the soul.
SQLite is the memory ledger.
The vector DB is the instinct for association.
```

## 4.2 Filesystem

The filesystem contains the user’s actual files.

Archivist should never pretend the database is the source of truth for user documents.

## 4.3 AI Wiki

The AI wiki explains what the Library means.

It is human-readable, AI-readable, editable, and portable.

It answers:

- What is this Library?
- What matters here?
- How is the folder structured?
- What is canon?
- What is draft?
- What is deprecated?
- What style does the user prefer?
- What visual direction should images follow?
- What is allowed?
- What is forbidden?

## 4.4 SQLite

SQLite stores structured records:

- Libraries.
- Files.
- File hashes.
- Chunks.
- Chat sessions.
- Messages.
- Message chunks.
- AI profiles.
- Proposals.
- Diffs.
- Operation ledger.
- Deprecated records.
- Settings.
- Provider settings.
- Scan status.
- Index status.
- Ratings and feedback.

SQLite is the local filing cabinet and audit ledger.

## 4.5 Vector DB

The vector store enables semantic search.

Normal keyword search finds exact words.

Vector search finds related meaning.

Example:

```text
Search: "rebirth machine"

May find:
- Cradles
- resurrection systems
- soul return mechanics
- respawn temples
- ancient womb-like devices
```

The vector DB is not the Library’s soul and not the source of truth. It is a retrieval index.

---

# 5. The AI Wiki

## 5.1 What the AI Wiki Is

Every Library has an internal AI wiki stored inside its `.archivist/` folder.

This wiki is not a marketing feature. It is one of the core systems of the product.

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
- What writing style is preferred.
- What art direction should guide visual generation.
- What creative taste the user has approved or rejected.

Without the AI wiki, the AI is only guessing from filenames and snippets.

With the AI wiki, Archivist understands the Library as a living body of knowledge.

## 5.2 AI Wiki Goals

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
- Preserve writing style.
- Preserve art direction.
- Preserve user taste.
- Help creative tools stay coherent.

## 5.3 AI Wiki Structure

Suggested `.archivist/` structure:

```text
LibraryRoot/
  .archivist/
    soul.md
    policies.json
    structure.md
    context-cheatsheet.md
    glossary.md
    decisions.md
    deprecated.md
    style-guide.md
    art-direction.md
    taste.md
    ignore.md
    profiles/
    chats/
    proposals/
    history/
    exports/
    catalog.sqlite
    vectors/
```

These files are human-readable where possible.

The database is for speed.

The wiki is for meaning.

## 5.4 soul.md

`soul.md` describes the identity and purpose of the Library.

It answers:

- What is this Library?
- What is the user trying to do with it?
- What matters here?
- What should Archivist optimize for?
- What should Archivist avoid?
- What tone or behavior should the assistant use when working here?
- What is the current north star of this Library?

Example sections:

```markdown
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
```

## 5.5 policies.json

`policies.json` is machine-enforced.

It defines what Archivist is allowed to do.

Example:

```json
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
```

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

## 5.6 structure.md

`structure.md` defines how the Library is organized.

This is the directory cheat sheet. It prevents the AI from relying only on filenames.

It explains what folders mean.

Example sections:

```markdown
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
```

## 5.7 context-cheatsheet.md

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

```markdown
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
```

## 5.8 glossary.md

`glossary.md` stores important terms, entities, acronyms, and concepts in the Library.

It improves retrieval and context compilation.

Example:

```markdown
# Glossary

## Thresh

A race or civilization concept in the project. Multiple drafts may exist.

## Cradles

Respawn or rebirth-related concept.

## Custodian

AI director / simulation management concept.
```

## 5.9 decisions.md

`decisions.md` records important user-approved decisions.

Example:

```markdown
# Decisions

## 2026-07-05 - Thresh Origin

User chose to keep the hybrid swamp/Deepkin/Maw origin direction.

Pure Maw-created origin should be treated as deprecated draft material.

This helps prevent fractured concurrency where old notes accidentally compete with current canon.
```

## 5.10 deprecated.md

`deprecated.md` explains how Deprecated material should be handled.

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

Deprecated files should not be used as current truth unless the user explicitly asks for old versions, contradictions, history, or brainstorming.

## 5.11 style-guide.md

`style-guide.md` stores writing style preferences for the Library.

This is used by creative tools, Codex generation, lore writing, quest writing, character dialogue, and style linting.

Example:

```markdown
# Style Guide

## Overall Style

Restrained mythic prose with grounded sensory detail.

Prefer clear readable sentences over purple prose.

## Avoid

- Em dashes.
- Fancy punctuation unless explicitly requested.
- Modern slang.
- Quippy Marvel-style banter.
- Generic prophecy voice.
- Over-explaining supernatural mechanics.
- Phrases like "ancient evil awakens" unless earned.

## Prefer

- Concrete physical details.
- Religious and cultural consequences.
- Mystery preserved instead of explained too early.
- Characters speaking from worldview, not exposition.
- Short strong sentences mixed with ceremonial longer passages.
```

## 5.12 art-direction.md

`art-direction.md` stores the visual direction for generated images and beautiful exports.

It helps generated visuals stay coherent across NPC portraits, faction sigils, location art, quest images, and Codex exports.

Example:

```markdown
# Art Direction

## Overall Style

Painterly grit. Grounded fantasy. Ancient, damp, worn, mysterious.

## Materials

Worn iron, linen, bone, reed, wet stone, tarnished brass, old wood, smoke, candle wax.

## Lighting

Overcast marsh dawn, candlelit shrine, stormlight, torchlight through fog.

## Avoid

- Shiny high-fantasy armor.
- Clean MMO plastic.
- Overly polished anime styling.
- Generic fantasy castle aesthetics.

## Image Use

Images should feel like plates from an old field guide, codex, illuminated manuscript, or RPG sourcebook.
```

## 5.13 taste.md

`taste.md` stores user feedback patterns.

It should not be intrusive or magical. It is a local, explicit summary of what the user tends to accept, reject, rate highly, or revise.

Example:

```markdown
# Taste Memory

## Observed Preferences

- User likes restrained mythic tone.
- User dislikes em dashes.
- User likes lore that leaves mystery intact.
- User likes grounded consequences more than magic spectacle.
- User likes old-world religious texture.
- User dislikes modern slang and quippy dialogue.

## High-Rated Patterns

- Character voices with cultural worldview.
- Scene details that imply history.
- Faction conflict rooted in material needs.
- Mystery that creates questions.

## Low-Rated Patterns

- Generic fantasy prophecy.
- Overly flowery prose.
- Explaining too much too soon.
- Characters sounding like the same narrator.
```

Feedback should be distilled into `taste.md` through user approval, not silently rewritten by the app.

## 5.14 ignore.md

`ignore.md` contains human-readable ignore rules.

Example:

```markdown
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
```

The machine-readable version may also be stored in SQLite or JSON.

---

# 6. Deprecated Void

## 6.1 What the Deprecated Void Is

The Deprecated void is the default destination for old, replaced, superseded, or no-longer-current files.

It is not the trash.

It is not deletion.

It is a safe historical holding area.

When Archivist proposes to replace, reorganize, consolidate, rewrite, or supersede files, it should usually move the old versions into a `Deprecated/` folder instead of deleting them.

Example:

```text
Original:
  Thresh weird old idea.md

New:
  Canon/Races/Thresh.md

Old file moved to:
  Deprecated/Races/Thresh/Thresh weird old idea.md
```

## 6.2 Why Deprecated Exists

Deprecated exists because the user’s notes are creative and historical.

Old ideas may become useful again.

Bad drafts may contain good fragments.

Conflicting lore may explain how the project evolved.

Old startup notes may contain useful research.

Old plans may contain reasoning that should not be lost.

Archivist should never assume old means worthless.

## 6.3 Deprecated Rules

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

## 6.4 Deprecated Metadata

When a file is moved to Deprecated, Archivist should store metadata in the operation ledger and, where useful, a local note.

Metadata should include:

- Original path.
- Deprecated path.
- Date moved.
- Proposal ID.
- Reason.
- Replacement file, if any.
- User approval status.
- Reversible status.
- File hash before move.
- File hash after move.

---

# 7. Human-in-the-Loop Diffing

## 7.1 Core Rule

Archivist must never alter files silently.

Before any file operation, the user must see what will happen.

This includes:

- Moving files.
- Renaming files.
- Creating files.
- Modifying files.
- Updating AI wiki files.
- Moving files into Deprecated.
- Archiving files.
- Applying generated indexes.
- Consolidating notes.
- Promoting generated creative material to canon.

## 7.2 Chat-Based Diff Mode

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

## 7.3 Diff UI Requirements

For move/rename operations, show:

- Original path.
- New path.
- Destination folder meaning.
- Reason.
- Whether the old path will remain.
- Whether the file is going to Deprecated.

For content changes, show:

- Before text.
- After text.
- Changed sections.
- Source reasoning.
- Related files used as context.

For Deprecated moves, show:

- File being deprecated.
- Why it is being deprecated.
- What replaces it.
- Where it will go.
- Whether it remains searchable.
- Whether undo is available.

For generated creative artifacts, show:

- Whether this is Canon, Draft, Maybe, Inspiration, Rejected, or Style Reference.
- Which source material was used.
- Which new lore claims are implied.
- Which files would be created or updated.
- Whether a style/taste/art-direction memory update is proposed.

## 7.4 Approval Controls

The UI should support:

- Approve all.
- Approve selected.
- Reject all.
- Skip item.
- Edit destination.
- Edit generated text.
- Move to Deprecated.
- Keep in place.
- Save proposal for later.
- Apply approved operations.
- Undo after apply.
- Save generated artifact as Maybe.
- Promote selected parts to Draft or Canon.
- Extract selected lines into glossary/decisions/style guide.

## 7.5 User Trust Rule

The user should never wonder:

```text
What did it just do?
```

Archivist should always be able to answer:

- What changed?
- Why did it change?
- When did it change?
- Who approved it?
- What was the original?
- Can I undo it?

---

# 8. Chat System and Rich Artifact Stream

## 8.1 Core Idea

The chat view should not be a plain text scroll.

It should become an artifact stream.

A message can contain:

- Markdown.
- Code.
- Sources.
- Files.
- Images.
- Diffs.
- Proposals.
- Context packets.
- Attachments.
- 3D previews later.
- Audio blocks later.
- Export actions.
- Save/apply buttons.

## 8.2 Message Blocks

Instead of every message being only a string, messages should eventually be modeled as blocks.

Conceptual shape:

```text
ChatMessage
  id
  role
  profileId
  createdAt
  blocks[]
```

Block types:

```text
markdown
code
file
image
pdf
docx
model3d
sourceList
diff
proposal
operationResult
audio
contextPacket
toolCall
exportArtifact
ratingPrompt
```

## 8.3 Markdown Blocks

Markdown blocks handle normal assistant writing.

Use cases:

- Summaries.
- Explanations.
- Lore writing.
- Quest notes.
- AI wiki suggestions.
- General answers.

Rendered markdown should be sanitized. AI output should not be trusted as raw HTML.

## 8.4 Code Blocks

Code blocks should have serious affordances:

- Copy button.
- Language label.
- Filename suggestion.
- Save to file.
- Open in VS Code later.
- Apply as patch later.
- Create proposal from code later.

## 8.5 Copyable Text Blocks

Not all reusable output is code.

Copyable text blocks should support:

- Email drafts.
- README sections.
- Prompt templates.
- Lore passages.
- AI wiki content.
- Meeting notes.
- Job application blurbs.

Actions:

- Copy.
- Save to Library.
- Add to decisions.
- Append to glossary.
- Turn into proposal.

## 8.6 Source Cards

Every grounded answer should be able to show source cards.

Source card actions:

- Open.
- Reveal in Finder/Explorer.
- Copy path.
- Preview snippet.
- Pin to context.
- Exclude from future context later.

## 8.7 File Attachment Blocks

User messages can include files.

Possible attachments:

- Markdown.
- Text.
- PDF.
- DOCX.
- Image.
- Audio.
- Video transcript.
- 3D model.
- Folder reference.

Each attachment block should show:

- File name.
- Type.
- Size.
- Preview if possible.
- Whether it was indexed.
- Whether it was sent to a model.
- Actions.

## 8.8 Proposal and Diff Blocks

Proposals and diffs should render as first-class blocks with approval controls.

Examples:

```text
Move:
  From: /messy/thresh old thing.md
  To: /Races/Thresh/Deprecated/thresh old thing.md

Reason:
  Superseded by current canon.

Actions:
  Approve
  Skip
  Change destination
```

## 8.9 Context Packet Blocks

Before or after an AI call, the UI can show the context packet.

Context packet should display:

- AI profile used.
- Library.
- Chat.
- Recent messages.
- AI wiki files included.
- Source chunks included.
- Deprecated material included or excluded.
- Estimated tokens.
- Cloud/local provider.
- Privacy note.

Actions:

- Copy packet.
- Remove source.
- Pin source.
- Open source.
- Send.

## 8.10 Streaming

Start with normal request/response.

Then add Server-Sent Events for assistant text streaming.

Use WebSockets later for:

- Scan progress.
- Index progress.
- Long-running job progress.
- Multi-profile panel events.
- File operation progress.

Pushback rule:

Use SSE first for one-way AI streaming. Do not overbuild WebSockets until there are truly bidirectional live events.

---

# 9. Persistent Main Timeline and Alternate Chats

## 9.1 Core Idea

Each Library should have a persistent Main Timeline that can run indefinitely.

The user should not need to restart the core Library conversation every session.

The Library’s long-running conversation becomes part of the Library’s history.

## 9.2 Main Timeline UX

The user opens a Library and sees recent conversation.

As the user scrolls upward, older messages load.

The chat UI should support:

- Recent messages loaded by default.
- Infinite scroll upward.
- Pagination.
- Preserved scroll position when loading older messages.
- Older messages archived into chunks.
- Chunk summaries searchable later.

## 9.3 Important Context Rule

The full forever-chat is never blindly sent to the AI.

Archivist compiles context from:

- Recent messages.
- Pinned memories.
- AI wiki files.
- Relevant files.
- Relevant old chat summaries.
- Operation history.
- Active AI profile.
- Current task/mode.

## 9.4 Chat Memory Tiers

Use memory tiers:

```text
Hot:
  Recent messages currently loaded in UI.

Warm:
  Older messages paginated from SQLite.

Cold:
  Summarized chunks searchable but not loaded.

Durable:
  Important decisions promoted into decisions.md, soul.md, structure.md, glossary.md, style-guide.md, or taste.md.
```

## 9.5 Chat Chunks

Older conversation should be grouped into chunks.

Chunk boundary options:

- Every 40 to 60 messages.
- Token estimate.
- Time window.
- Topic change.
- Proposal completion.
- Manual archive.

Each chunk should store:

- Start message.
- End message.
- Summary.
- Topics.
- Decisions.
- Source references.
- Created date.
- Archived date.

## 9.6 Memory Promotion

Important conclusions from chat should be promoted into durable AI wiki memory through approval.

Example:

```text
This seems like a durable Library decision. Save it to decisions.md?
```

Or:

```text
This sounds like a structure preference. Add it to structure.md?
```

Core rule:

```text
Chat is the stream.
Wiki is the distilled memory.
Ledger is the proof.
```

## 9.7 Alternate Chats

Each Library may have multiple chats.

Examples:

- Main Timeline.
- Lore Riffage.
- Quest Workshop.
- Character Chamber.
- Canon Review.
- Proposal Review.
- Research Thread.
- Morning Codex.
- City Planner.
- Faction Simulator.
- Style Lab.

Each chat can define:

- Purpose.
- Active AI profiles.
- Retrieval preferences.
- Context rules.
- Included folders.
- Excluded folders.
- Whether Deprecated material is allowed.
- Output style.

---

# 10. AI Profiles and Creative Councils

## 10.1 Core Idea

AI Profiles are reusable assistant personalities that can participate in Library chats.

A profile defines:

- Name.
- Role.
- Type.
- Personality.
- Voice.
- Tone.
- Priorities.
- Forbidden behaviors.
- Context preferences.
- Permissions.
- Optional avatar/profile image.

Profiles can be practical, creative, critical, or character-based.

## 10.2 Profile Types

Suggested profile types:

- Archivist Profile.
- Creative Profile.
- Reviewer Profile.
- Character Profile.
- Utility Profile.
- Art Profile.
- Style Profile.

## 10.3 Example Profiles

### Archivist

Keeps the Library organized.

Responsibilities:

- Maintain structure.
- Summarize decisions.
- Propose wiki updates.
- Protect policies.
- Manage file operation proposals.

### Worldbuilder

Brainstorms setting ideas.

Responsibilities:

- Generate cultures, locations, myths, factions, and history.
- Use Drafts and Deprecated when helpful.
- Keep ideas labeled as draft unless approved.

### Continuity Keeper

Finds contradictions, canon drift, timeline issues, and weak assumptions.

Responsibilities:

- Cite sources.
- Distinguish Canon, Draft, Maybe, and Deprecated.
- Avoid inventing canon.
- Suggest resolution proposals.

### Quest Architect

Turns lore into playable quest structure.

Responsibilities:

- Hooks.
- Objectives.
- Branches.
- Failure states.
- Rewards.
- Consequences.
- Player agency.

### Character Profile

Embodies a person, NPC, faction voice, deity, villain, witness, or historian.

Use cases:

- Dialogue testing.
- Lore immersion.
- Character reaction simulation.
- Game AI seed later.

### Style Auditor

Checks generated writing against style rules.

Responsibilities:

- No em dashes if forbidden.
- No fancy punctuation if disabled.
- No modern slang.
- No generic fantasy phrasing.
- Preserve Library tone.
- Flag violations and revise.

### Art Director

Maintains visual coherence.

Responsibilities:

- Suggest illustrations.
- Generate image prompts.
- Respect `art-direction.md`.
- Avoid forbidden aesthetics.
- Help produce visual identity.

## 10.4 Profile Storage

Profiles should live locally.

Suggested structure:

```text
.archivist/
  profiles/
    archivist.profile.md
    continuity-keeper.profile.md
    worldbuilder.profile.md
    elder-veyr.profile.md
```

A profile can be markdown with frontmatter:

```markdown
---
id: continuity-keeper
type: reviewer
name: Continuity Keeper
tone: calm, precise, dry
---

# Role

You find contradictions, canon drift, timeline issues, and weak assumptions.

# Priorities

- Preserve accepted decisions.
- Cite sources.
- Distinguish canon from draft.
- Ask before changing files.

# Forbidden

- Do not invent canon.
- Do not treat Deprecated as current truth.
```

## 10.5 Multi-AI Chat Modes

Supported later:

- Single AI.
- Panel Discussion.
- Round Robin.
- Summon Specific AI.
- Critique Mode.
- Character Mode.

Examples:

```text
@ContinuityKeeper does this contradict anything?
@ElderVeyr how would your people describe this place?
@QuestArchitect make this playable.
```

## 10.6 Safety Rule

Do not make every profile an autonomous agent at first.

Profiles may:

- Answer.
- Brainstorm.
- Critique.
- Roleplay.
- Summarize.
- Propose actions.

Profiles may not:

- Directly modify files.
- Bypass policies.
- Delete files.
- Apply operations without approval.

Archivist remains the file-operation authority.

---

# 11. Creative Engines

## 11.1 Core Idea

The same Library context, AI wiki, profiles, retrieval, and proposal system can power creative tools.

The danger is trying to build these before the librarian foundation works.

The correct order:

```text
1. Build the librarian foundation.
2. Build rich chat artifacts.
3. Add AI profiles.
4. Add creative engines as workflows on top.
```

## 11.2 Lore Riffage

A chat mode for brainstorming setting ideas within Library context.

Can include:

- Canon.
- Drafts.
- Deprecated material for inspiration.
- Glossary.
- Decisions.
- Open questions.
- Style guide.
- Taste memory.

Outputs:

- New lore ideas.
- Canon candidates.
- Open questions.
- Contradiction warnings.
- Suggested files.
- Suggested wiki updates.

## 11.3 Quest Builder

A human-in-the-loop quest workflow.

Inputs:

- Location.
- Characters.
- Factions.
- Conflict.
- Player goal.
- Tone.
- Constraints.

Outputs:

- Quest brief.
- Hook.
- Objectives.
- Branching choices.
- NPC motivations.
- Dialogue snippets.
- Rewards.
- Failure states.
- World consequences.
- Lore implications.
- Files to create/update.

## 11.4 Character Chamber

A place to test character profiles.

Use cases:

- Riff with an NPC.
- Test dialogue.
- Simulate reactions.
- Explore backstory.
- Generate voice samples.
- Export profile seed later for game AI.

## 11.5 City Planner

A worldbuilding workflow for settlements.

Can model:

- Districts.
- Trade.
- Food supply.
- Water.
- Religion.
- Crime.
- Politics.
- Infrastructure.
- Important NPCs.
- Quest hooks.

## 11.6 Faction Simulator

A workflow for faction turns.

Can simulate:

- Goals.
- Resources.
- Leaders.
- Enemies.
- Secrets.
- Actions.
- Reactions.
- Rumors.
- Consequences.
- Quest hooks.

## 11.7 NPC Quest Simulator

Send a group of NPCs on a mission and mine the results for lore, quests, dialogue, and character development.

Example:

```text
Send Elder Veyr, a thief, a novice priest, and a deserter into the sunken bell tower.
```

Outputs:

- Who leads.
- Who argues.
- Who betrays.
- Who saves someone.
- What they discover.
- What changes.
- What comes back wrong.

## 11.8 Myth Generator

Generate conflicting cultural myths from different perspectives.

Truth layers matter:

- Canon.
- Rumor.
- In-world belief.
- Secret truth.
- Player-facing truth.
- Designer-only truth.
- Deprecated.

## 11.9 Contradiction Engine

Find contradictions across:

- Canon.
- Drafts.
- Deprecated.
- Decisions.
- Chat history.
- Related files.

Outputs:

- Contradiction summary.
- Sources.
- Current accepted direction.
- Suggested resolution.
- Proposed Deprecated moves.
- Proposed canon/index updates.

## 11.10 Director Mode

A future creative producer profile that watches the process and periodically says:

- You are drifting from the core tone.
- This idea is strong.
- This NPC has become important.
- This conflict deserves a quest.
- This old decision should be promoted to canon.
- You have competing versions of this race.

---

# 12. Codex, Morning Reading, and Beautiful Export

## 12.1 Core Idea

Archivist should not only organize knowledge. It should help grow the Library in the user’s taste.

The Codex system turns Library context into beautiful reading artifacts.

This can be used for:

- Morning coffee lore reading.
- Chapters.
- Myths.
- Quest tales.
- Faction histories.
- Travelogues.
- Character scenes.
- World bible excerpts.
- Lore bibles.
- Quest packets.
- City folios.
- Faction dossiers.
- Character dossiers.

## 12.2 Morning Codex

A ritual feature.

The user can ask:

```text
Generate tomorrow morning’s lore reading.
```

Archivist can ask lightweight questions:

- What Library?
- What tone?
- What characters?
- What region?
- Canon only, or allow draft weirdness?
- How long?
- Advance story, explore history, or dramatize existing lore?
- Include art suggestions?
- Save as Maybe by default?

Output can include:

- Title page.
- Chapter heading.
- Stylized first paragraph.
- In-world quote.
- Chapter body.
- Illustration suggestions.
- Lore implications.
- Canon candidates.
- Open questions.
- Rating controls.

Generated Codex entries are not canon by default.

They can be:

- Rated.
- Revised.
- Saved to Maybe.
- Promoted to Draft.
- Promoted partially to Canon.
- Used as Style Reference.
- Rejected.
- Moved to Deprecated later.

## 12.3 Maybe Pile

Generated material should not be binary canon/trash.

Possible states:

- Canon.
- Draft.
- Maybe.
- Inspiration.
- Rejected.
- Deprecated.
- Style Reference.
- Character Voice Reference.

The Maybe pile is valuable because generated material often contains one good line, concept, or mood even if the whole draft is not canon.

Example metadata:

```text
Rating: 3/5
Good: atmosphere, priest voice
Bad: too much prophecy language
Useful fragments: sunken bell, child witness, river tithe
Canon status: not canon
```

## 12.4 Ratings and Feedback

Ratings should be lightweight but useful.

Possible ratings:

- Tone.
- Canon fit.
- Originality.
- Usefulness.
- Character voice.
- Keepability.

Tags:

- too modern.
- too flowery.
- great image.
- wrong character.
- good mystery.
- save phrase.
- strong setting detail.
- no em dashes violation.
- too much exposition.

Feedback should help update `taste.md` or `style-guide.md` through human approval.

## 12.5 Writer Profiles

Book writers need their own souls.

Possible writer profiles:

- Old Chronicle.
- Campfire Storyteller.
- Scholar of the Archive.
- War Journal.
- Priest’s Testimony.
- Traveler’s Account.
- Quest Scribe.
- Dark Fairy Tale.
- Military After-Action Report.

Each profile defines:

- Voice.
- Purpose.
- Style.
- Avoid list.
- Preferred sentence rhythm.
- Punctuation rules.
- Output format.

## 12.6 Style Checks

A Style Auditor should check generated writing.

Checks:

- No em dashes if forbidden.
- No fancy punctuation if disabled.
- No modern slang.
- No unwanted phrases.
- No generic fantasy prophecy voice.
- No exposition dumping.
- No contradictions with `decisions.md`.
- No Deprecated lore treated as canon.
- No names outside approved naming style.

The pipeline can be:

```text
Writer drafts
Continuity Keeper checks canon
Style Auditor checks taste rules
Art Director suggests visuals
Archivist packages output
User rates it
```

## 12.7 Beautiful PDF Export

Archivist should eventually export professional documents.

Export types:

- Lore Bible.
- World Bible.
- Quest Packet.
- City Folio.
- Faction Dossier.
- Character Dossier.
- Codex Reading.
- Chapter Collection.
- Style Guide.
- Art Direction Brief.

PDF features:

- Cover page.
- Table of contents.
- Section dividers.
- Drop caps.
- Serif typography.
- Old English / codex / grimoire vibe.
- Callout boxes.
- In-world quotes.
- Side notes.
- Footnotes.
- Glossary.
- Appendix.
- Timelines.
- Faction trees.
- Diagrams.
- Generated or user-selected art.
- Captioned image plates.

The intended vibe is not unreadable medieval manuscript. It is more like:

```text
Tolkien appendix meets old field journal meets premium RPG sourcebook.
```

## 12.8 Read Mode / Codex Mode

A file or generated artifact should eventually support:

- Edit mode: raw markdown / structured editor.
- Read mode: beautiful rendered document.

Read Mode can include:

- Typography.
- Drop caps.
- Section dividers.
- Images.
- Callouts.
- Background texture.
- Page-like layout.
- Print/PDF export.

## 12.9 Illustration Suggestions

While generating lore, quests, stories, or Codex entries, the writer can suggest where an image would improve the piece.

Example:

```text
Illustration Suggestion:
A flooded shrine interior, half-submerged pews, a bell rope disappearing into black water.
```

User actions:

- Generate image.
- Skip.
- Use placeholder.
- Find existing image.
- Add to art backlog.

## 12.10 Entity Portraits

Entities can have profile pictures.

Entity types:

- AI profiles.
- NPCs.
- Characters.
- Factions.
- Cities.
- Creatures.
- Locations.
- Artifacts.
- Quests.

Portrait sources:

- User uploaded.
- Generated.
- Placeholder.
- Imported.

This improves UI feel and supports future game AI workflows.

---

# 13. Visual UI and Local App Polish

## 13.1 UI Direction

Archivist should feel like a local AI workspace, not a generic SaaS dashboard.

Desired feel:

- Beautiful.
- Immersive.
- Calm.
- High-trust.
- Slightly magical.
- Functional first.

Balance target:

```text
70 percent elegant and functional.
20 percent atmospheric and premium.
10 percent weird magic.
```

## 13.2 Ambient Visual Theme System

Future polish idea:

- Glassmorphic cards.
- Subtle smoky/void background.
- Slow undulating depth.
- Soft grain.
- Ambient particle drift.
- Glow edges.
- Layered depth.
- Parallax.
- Motion that can be reduced/disabled.

Pushback rule:

Do not make the app feel like a crypto dashboard from hell.

The visual system should serve clarity and ritual, not distraction.

## 13.3 Local App Advantages

Electron still renders web tech:

- HTML.
- CSS.
- JS.
- WebGL.
- Canvas.
- Three.js.
- SVG.

But because it is local, Archivist can also support:

- Native folder/file dialogs.
- Local file previews.
- Reveal in Finder/Explorer.
- Open in VS Code.
- Clipboard integration.
- Local asset storage.
- Local helpers/processes.
- File watching.
- Export pipelines.
- Safer manual handoff tools.

## 13.4 3D and Media UI Ideas

Future supported blocks/views:

- Three.js model viewer for `.glb` / `.gltf`.
- Turntable preview.
- Wireframe toggle.
- Lighting presets.
- Bounding box.
- Image previews.
- PDF previews.
- DOCX extraction.
- Audio transcript blocks.
- Voice-to-text.
- Text-to-speech.

3D should serve meaning, not just flex.

Good uses:

- Asset viewer.
- Entity cards.
- World maps.
- Relationship graphs.
- Ambient backgrounds.
- Special Library Atlas/World Room views.

---

# 14. Locked Tech Stack

The agreed stack for v1:

- Electron.
- React + Vite.
- TypeScript.
- Express.
- Zod.
- SQLite.
- Local vector DB.

## 14.1 Desktop Shell

Use Electron.

Electron responsibilities:

- Start the local Express API eventually.
- Choose or receive an available dynamic local port.
- Open the desktop window.
- Load the React/Vite UI.
- Provide minimal native helpers.
- Handle app lifecycle.
- Shut down services cleanly.

Electron should not contain core business logic.

## 14.2 Frontend

Use:

- React.
- Vite.
- TypeScript.
- Custom design system.
- Custom token system.
- CSS modules where useful.
- CSS variables from theme tokens.

The frontend should be designed like a local AI workspace.

## 14.3 Backend

Use:

- Node.js.
- Express.
- TypeScript.
- Zod.
- SQLite.
- Local vector DB.

The backend runs locally on the user’s machine.

The backend should expose a normal local HTTP API over:

```text
http://127.0.0.1:<dynamic-port>
```

Do not bind to:

```text
0.0.0.0
```

The API should use a dynamic port eventually.

The app should generate a local session token on startup and require it for local API requests.

HTTPS is not required for the internal desktop app talking to itself over localhost. HTTPS/tunneling can be added later for external bridge scenarios.

## 14.4 Why Express

Express is chosen because:

- It is familiar.
- It is widely used in Node jobs.
- It supports a normal API mental model.
- It avoids unnecessary framework ceremony.
- It lets us define our own architecture.
- It is easy to test with browser, curl, Postman, or internal clients.

Express should remain a thin transport layer.

Core business logic belongs in services, not routes.

## 14.5 Validation

Use Zod for:

- API request validation.
- Shared schemas.
- Runtime safety.
- Type inference.
- AI response validation.
- Internal data contracts.
- Structured message block validation.
- Proposal validation.

Important entities should have Zod schemas:

- Library.
- LibraryPolicy.
- WikiFile.
- FileRecord.
- ChunkRecord.
- ChatSession.
- ChatMessage.
- ChatChunk.
- ChatBlock.
- AIProfile.
- Proposal.
- ProposedOperation.
- OperationLedgerEntry.
- DeprecatedRecord.
- SearchResult.
- ContextPacket.
- AIProviderConfig.
- CodexEntry.
- Rating.
- ExportJob.

## 14.6 Database

Use SQLite for local structured data.

SQLite stores:

- Registered libraries.
- File catalog records.
- File metadata.
- Scan history.
- Chunk metadata.
- Chat sessions.
- Chat messages.
- Chat chunks.
- AI profiles.
- Proposal records.
- Operation ledger.
- Deprecated records.
- Settings.
- AI provider settings.
- Library status.
- Search/cache metadata.
- Wiki metadata.
- Rating metadata.
- Export metadata.

SQLite should be local only.

Recommended:

Global app DB:

- Registered libraries.
- App settings.
- Model/provider settings.
- Recent activity.
- Global profiles if supported later.

Per-library DB:

- File catalog.
- Chunks.
- Chat sessions/messages/chunks.
- AI profiles.
- Proposals.
- Operation history.
- Library-specific search/index state.
- Deprecated records.
- Wiki state.
- Codex entries.
- Ratings.

## 14.7 SQLite vs PostgreSQL

SQLite is preferred for v1 because Archivist is a local-first desktop app.

SQLite advantages:

- No database server to install.
- No running service.
- No username/password setup.
- No ports.
- No Docker requirement.
- Easy app packaging.
- One local file per app/library.
- Great for desktop apps.
- Fast enough for this use case.

PostgreSQL may be useful later if Archivist gains:

- Central server.
- Collaboration.
- Multiple users.
- Hosted sync.
- Advanced remote permissions.
- Server-side deployment.

For local security, PostgreSQL login does not magically protect data if the user account or machine is compromised. Local security should rely on:

- OS user permissions.
- Disk encryption.
- Keychain/Credential Manager for API keys.
- Optional app-level encryption later.

## 14.8 Local Vector DB

Use a local vector database or local vector storage adapter.

Requirements:

- No paid hosted vector database.
- No cloud database dependency.
- Files and indexes stay local.

Candidate local vector stores:

- LanceDB.
- Chroma.
- Qdrant local.
- SQLite vector extension.
- Other swappable local vector solution.

Use an adapter interface so the vector backend can be swapped later.

Conceptual interface:

```text
VectorStore
  upsertChunks(chunks)
  search(queryEmbedding, filters)
  deleteByFileId(fileId)
  rebuildLibraryIndex(libraryId)
```

## 14.9 AI Providers

The app should support a provider adapter system.

Initial likely providers:

- Mock provider for testing.
- OpenAI with user API key.
- Anthropic with user API key later.
- Ollama local.
- LM Studio local.

The user’s files stay local.

If a cloud provider is used, only selected context snippets are sent.

The app should eventually show:

- Cloud AI is enabled.
- Archivist will not upload your files.
- Only selected excerpts used for this answer may be sent to the model provider.
- View context being sent.
- Disable cloud AI.

API keys should be stored in the OS keychain/keyring, not plain text in project files.

---

# 15. Architecture Overview

## 15.1 High-Level Shape

```text
Electron Desktop Shell
        |
        v
React + Vite Frontend
        |
        v
Local Express API
        |
        v
Archivist Core Services
        |
        +--> SQLite
        +--> Local Vector DB
        +--> Local Filesystem
        +--> AI Provider Adapters
```

Electron starts the app and local backend eventually.

React talks to the backend through HTTP.

Express routes call core services.

Core services own the actual behavior.

SQLite and vector DB store local knowledge and app state.

The filesystem remains the source of truth for user files.

The `.archivist/` wiki explains the meaning of the Library.

## 15.2 API vs IPC Decision

The primary app architecture uses a normal local HTTP API.

Electron IPC should only be used for narrow desktop-shell utilities, such as:

- Open native folder picker.
- Open a file in Finder/Explorer.
- Reveal a file.
- Get application data path.
- Show native notification.
- App lifecycle events.

Do not put business logic in IPC.

Main business logic flows through the local Express API.

This keeps the architecture familiar, testable, and reusable for the future MCP bridge.

## 15.3 Modular Monolith

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

# 16. Modular Workspace and Product Editions

## 16.1 Core Idea

Archivist should start as a local-first AI librarian, but the architecture should support a larger local AI workspace.

The core abstraction is:

```text
Workspace
  Libraries
  Chats
  Profiles
  Tools
  Workflows
  Providers
  Artifacts
  Proposals
  Ledger
```

A Library is one kind of context source.

A Tool is one kind of capability.

A Profile is one kind of mind or role.

A Workflow is a reusable process.

An Artifact is a durable output.

A Proposal is a human-reviewable change.

The goal is one house with many rooms, not twenty separate chat apps.

## 16.2 UI House

The UI should have a stable shell that can host many modes.

Suggested layout:

```text
Left sidebar:
  Libraries
  Workspaces
  Tools
  Profiles
  Recent timelines

Center:
  Active chat / artifact / workflow / document

Right panel:
  Sources
  Context packet
  Inspector
  Tool output
  Proposal review
  Routine output
```

If the user is worldbuilding, the right panel can show:

- Canon sources.
- Related entities.
- Open questions.
- Art direction.
- Generated artifacts.

If the user is coding, the right panel can show:

- Repo context.
- File tree.
- Diffs.
- Test output.
- Terminal/tool calls.

If the user is working with a game engine bridge, the right panel can show:

- Selected asset.
- Generated blueprint/spec.
- Editor action preview.
- Import/export state.

Same shell. Different tools.

## 16.3 Core vs Lab Builds

There should be two conceptual layers:

```text
Archivist Core
  Stable public product.

Archivist Lab / Toolbelt
  Personal wizard modules and experimental workflows.
```

Public Archivist Core can focus on:

- Chat with folders.
- Organize notes.
- Search locally.
- Approve changes.
- Generate clean exports.
- Maintain local memory.

The personal Lab build can include:

- Shard worldbuilding engine.
- AI NPC profile chamber.
- UE5 tool hooks.
- Procedural artifact generator.
- Code Forge.
- Custom local model routing.
- Book/codex exporter.
- Proactive routines.

Same platform. Different enabled modules.

## 16.4 Editions Instead of Forks

Do not create long-lived branches for every niche.

Use editions or feature packs.

Possible editions:

- Archivist Core.
- Archivist Writer.
- Archivist Dungeon Master.
- Archivist Organizer.
- Archivist Developer.
- Archivist Chef.
- Zach Lab / Shard Lab.

Each edition can define:

- App name.
- Default theme.
- Enabled features.
- Default AI profiles.
- Starter wiki files.
- Library templates.
- Prompt templates.
- Export templates.
- Example workflows.

The codebase should remain one modular monolith.

## 16.5 Module Manifest Concept

Modules can eventually declare their own tools, panels, workflows, and permissions.

Conceptual shape:

```ts
type ArchivistModule = {
  id: string;
  name: string;
  description: string;
  permissions: PermissionRequest[];
  tools: ToolDefinition[];
  workflows: WorkflowDefinition[];
  panels: PanelDefinition[];
  profiles?: AIProfileTemplate[];
  routes?: ApiRouteDefinition[];
};
```

Do not build a public plugin system in v1.

But keep internal module boundaries clean enough that a plugin/module system could exist later.

## 16.6 Product Mantra

```text
One workspace.
Many tools.
Shared context.
Human approval.
Local ownership.
```

# 17. Tool Registry and Workflow Engine

## 17.1 Core Idea

AI harness work should be composable.

Do not rely on one giant prompt or one magical autonomous agent.

Use:

```text
Tools -> Steps -> Chains -> Workflows -> Routines
```

Definitions:

- Tool: a single callable capability.
- Step: a single unit inside a workflow.
- Workflow: a reusable graph of steps.
- Run: one execution of a workflow.
- Routine: a workflow scheduled or triggered automatically.
- Agent: a model/profile/tool bundle that can perform reasoning steps.
- Artifact: a durable output from a step or run.
- Proposal: a human-reviewable change request.
- Ledger: the permanent audit trail.

This is atomic design for AI harness work.

## 17.2 Workflow, Run, Step, Tool

A workflow is the reusable blueprint.

A run is one execution of that workflow.

A step is one node inside the workflow.

A tool call is one concrete action.

An artifact is an output.

A proposal is a suggested persistent change.

A ledger entry is proof of what was applied.

Example:

```text
Workflow:
  Generate Morning Codex

Run:
  July 6 morning codex run

Steps:
  Retrieve context
  Draft myth
  Audit style
  Check continuity
  Suggest images
  Package artifact

Tool calls:
  search_library
  read_wiki
  generate_text
  lint_style

Artifacts:
  myth draft
  image prompts
  canon candidates

Proposals:
  save myth to Maybe
  update glossary

Ledger:
  user approved glossary update
```

## 17.3 Graphs, Not Only Chains

Workflows should eventually support graphs.

Example:

```text
Generate Morning Codex
  ├─ Retrieve Library Context
  ├─ Retrieve Taste/Style Guide
  ├─ Retrieve Recent Decisions
  │
  └─ Draft Codex Entry
       ├─ Style Audit
       ├─ Continuity Audit
       ├─ Art Direction Pass
       └─ Extract Canon Candidates
            └─ Package Review Artifact
```

Some steps can run in parallel.

Some depend on earlier outputs.

Some produce proposals.

Some are read-only.

Some require approval.

## 17.4 Deterministic Runner First

Pushback rule:

Do not start with fully autonomous swarms.

Start with a deterministic workflow runner:

- The workflow defines known steps.
- The system executes them.
- AI fills specific reasoning/generation steps.
- Inputs and outputs are typed.
- Errors are visible.
- Runs are logged.

Later, an AI planner can suggest or assemble workflows.

If the first version gives a master AI full authority to decide everything, the result will be a haunted slot machine.

## 17.5 Tool Definition

Every tool should have a schema.

Conceptual shape:

```ts
type ToolDefinition<TInput, TOutput> = {
  id: string;
  moduleId: string;
  name: string;
  description: string;
  inputSchema: ZodSchema<TInput>;
  outputSchema: ZodSchema<TOutput>;
  riskLevel: "safe" | "review" | "dangerous";
  requiresApproval: boolean;
  requiredCapabilities: Capability[];
  handler: (input: TInput, ctx: ToolContext) => Promise<TOutput>;
};
```

Example tools:

- `search_library`
- `read_file_summary`
- `create_file_proposal`
- `create_patch_proposal`
- `run_typecheck`
- `open_in_vscode`
- `generate_codex`
- `create_image_prompt`
- `create_ue5_asset_placeholder`

## 17.6 Capability Contracts

Each module and routine should declare what it can do.

Example capability names:

```text
read:library
write:proposal
apply:approvedProposal
read:repo
run:approvedCommand
generate:image
send:notification
read:calendar
read:web
```

Before enabling a workflow or routine, the UI should be able to show:

```text
This routine can:
- Read Shard Library
- Generate suggestions
- Create proposals

It cannot:
- Modify files
- Run shell commands
- Send cloud requests
```

This is how Archivist stays trustworthy.

## 17.7 Approval Nodes

Human approval should be a first-class workflow step.

Example:

```text
Step 1: Generate proposal
Step 2: Wait for human approval
Step 3: Apply approved changes
```

A workflow can pause in a `waiting_for_approval` state.

No approval means no mutation.

## 17.8 Step Artifacts

Each step should produce artifacts that can be inspected.

Examples:

- RetrievedContextArtifact.
- DraftTextArtifact.
- StyleAuditArtifact.
- ContinuityReportArtifact.
- ImagePromptArtifact.
- CodePatchArtifact.
- CommandOutputArtifact.
- ProposalArtifact.

The UI can render a workflow run like a build pipeline:

```text
✓ Retrieved context
✓ Drafted myth
⚠ Style audit found 3 em dashes
✓ Revised prose
✓ Suggested art
⏸ Waiting for approval to save
```

## 17.9 Reruns and Branches

Because step outputs are stored, the user should eventually be able to rerun only part of a workflow.

Examples:

- Rerun Style Auditor.
- Rerun Codex Writer with stricter tone.
- Rerun image prompts.
- Rerun continuity check.
- Keep retrieval context the same.

This is much better than starting a new chat blob every time.

## 17.10 Agent Hierarchies

Agents should be narrow and role-based.

A good hierarchy:

```text
Director
  decides plan and delegates

Specialists
  perform focused tasks

Reviewer
  critiques output

Archivist
  records, proposes, and preserves

Executor
  performs approved tool calls
```

For code:

- Architect.
- Implementation Agent.
- Test Agent.
- Security Reviewer.
- Diff Summarizer.

For Shard:

- World Director.
- Lore Writer.
- Continuity Keeper.
- Quest Designer.
- Art Director.
- Archivist.

Do not let ten agents freestyle into the same chat.

Make them perform roles in a controlled workflow.

## 17.11 Code Forge Module

A Claude Code-style helper can live inside Archivist as a module, not a separate app.

Capabilities:

- Read repo.
- Explain architecture.
- Propose file edits.
- Generate patches.
- Run tests.
- Summarize errors.
- Create tasks.
- Manage todos.
- Open files in VS Code.
- Use terminal commands with approval.

Rules:

- Never run destructive commands without approval.
- Never edit outside the repo.
- Show diffs before applying.
- Capture command output.
- Summarize changes.
- Record decisions.

This reuses the same diff UI, proposal engine, context compiler, tool registry, and operation ledger as the librarian.

## 17.12 CLI and Local Storage Safety

At the low level, AI tool use often becomes:

```text
parse structured output
validate it
call functions
functions perform filesystem or CLI operations
log result
```

The difference between a toy and a safe product is the rails:

- Path sandboxing.
- Policy engine.
- Zod schemas.
- Dry runs.
- Diff previews.
- Approval nodes.
- Operation ledger.
- Undo plans.
- Command allowlists.
- No shell by default.
- Scoped permissions.

Do not give AI arbitrary shell access early.

Prefer explicit tools:

- `list_files`
- `read_file`
- `write_proposed_file`
- `create_patch`
- `run_npm_test`
- `run_typecheck`
- `open_in_vscode`

Later, a generic `run_command` tool can exist, but only with approval, allowlists, visible command text, and logs.

Command safety levels:

```text
Safe:
  ls, pwd, cat selected file, npm test, npm run typecheck

Review:
  npm install, git commands, file writes, migrations

Dangerous:
  rm, chmod, curl pipe shell, system config, deleting folders
```

For v1, dangerous commands should be denied.

## 17.13 Leaked Tool Architecture Boundary

It is acceptable to learn general architecture patterns from public discussion, personal research, and observed behavior of coding agents.

It is not acceptable to ship leaked proprietary code, secrets, prompts, or private implementation details.

Safe patterns to recreate independently:

- Tool hierarchies.
- Planner/executor separation.
- Subtask delegation.
- Confidence and risk scoring.
- Review loops.
- Memory compaction.
- Diff-first editing.
- Policy gates.
- Typed tool schemas.

Use the pattern. Do not copy the leaked material.

# 18. Proactive Routines, Daily Briefs, and Novelty Engine

## 18.1 Core Idea

Archivist should not merely respond to commands.

It should eventually run bounded, scheduled, user-approved routines that maintain awareness of selected Libraries and surface useful observations, questions, and opportunities.

This is proactive AI, not uncontrolled autonomy.

Rules:

- Scoped.
- Permissioned.
- Logged.
- Non-destructive.
- Reviewable.
- User-configurable.
- Quiet by default.

## 18.2 Routines and Watchers

User-facing names should be friendly.

Avoid making normal users feel like their computer is haunted by a swarm.

Good names:

- Daily Brief.
- Lore Watcher.
- Code Watcher.
- Opportunity Watcher.
- News Watcher.
- Momentum Coach.
- Library Health Check.
- Morning Codex.

Under the hood, these can be scheduled workflows or agent graphs.

## 18.3 Example Routine: Shard Lore Watcher

```text
Routine:
  Shard Lore Watcher

Runs:
  Every night at 2 AM

Scope:
  Shard Library only

Allowed:
  Read lore files
  Read decisions.md
  Search Deprecated material
  Generate summary
  Create suggestions
  Create proposals requiring approval

Not allowed:
  Modify files
  Delete files
  Contact external services unless enabled
```

Output:

```text
Morning brief:
- 3 possible contradictions found
- 1 new connection between Cradles and Miregate bell myths
- 2 files look ready to consolidate
- 1 morning myth snippet generated
- 1 question for Zach
```

## 18.4 Routine Output Inbox

Routine outputs should not directly become canon, tasks, or schedule changes.

They should land in a Routine Output Inbox.

Possible output states:

- new.
- read.
- saved.
- dismissed.
- snoozed.
- converted_to_task.
- converted_to_proposal.
- converted_to_lore_fragment.
- converted_to_decision.

This prevents proactive AI from becoming noise.

## 18.5 Daily Brief

The Daily Brief can synthesize multiple routines.

Example sections:

- Shard / creative work.
- Code / Archivist progress.
- Career / opportunities.
- Tech and world news.
- Obligations.
- Momentum nudge.
- Morning lore snippet.

Example tone:

```text
Good morning.

Shard:
The Miregate bell myth is starting to rhyme with your Cradle rules. Worth exploring.

Code:
Archivist’s next clean step is implementing the Tool Registry skeleton.

Career:
Two contract leads match your remote/full-stack/AI preference.

Momentum:
You have not touched Shard in 4 days. Generate a 20-minute lore session?
Also, go walk. You goblin.
```

## 18.6 Novelty Engine

The Novelty Engine suggests interesting connections, contradictions, analogies, and unexplored directions from existing context.

Useful questions:

- What does this remind me of?
- What idea is underdeveloped?
- What contradiction is secretly interesting?
- What forgotten note deserves revival?
- What old Deprecated idea could be reincarnated?
- What philosophical theme is emerging?
- What would make this more playable?
- What would make this more mythic?

Example output:

```text
Observation:
Your Cradles and Deprecated Void are conceptually similar. Both preserve what is dead but not gone.

Suggestion:
Shard could contain an in-world archive cult that believes discarded timelines become spiritual compost.

Possible use:
Faction, dungeon theme, or resurrection doctrine.
```

The Novelty Engine should not rewrite the Library. It should surface sparks.

## 18.7 Parallel Finder

A specialized novelty tool can draw parallels between the Library and:

- Mythology.
- Philosophy.
- Religion.
- History.
- Other fiction.
- Game mechanics.
- AI theory.

Guardrail:

```text
Do not copy. Use parallels to clarify themes and generate original directions.
```

Example:

```text
The Cradles rhyme with:
- resurrection myths
- backup systems
- baptism
- reincarnation
- save points
- womb/tomb symbolism
- data recovery

Original Shard angle:
A soul is not restored. It is recompiled from corrupted memory.
```

## 18.8 Local Overnight Mode

A future personal-power feature:

```text
Overnight Run
```

Before bed:

```text
Run Shard Lore Watcher
Run Archivist Code Watcher
Run Opportunity Watcher
Generate morning brief
Use local models only
Do not send cloud requests
Do not modify files
```

Morning:

```text
Here’s what your machine dreamed.
```

This feature should begin as a manual button, then become schedulable later.

## 18.9 Trigger Types

Routines can eventually run by:

- Manual trigger.
- Timer.
- File change.
- New note added.
- New chat decision.
- Calendar event.
- Inactivity.
- Goal drift.
- Weekly review.

Start with manual runs.

Then add scheduled runs.

Then add file-change triggers.

## 18.10 Notifications

Notification layers:

- Passive dashboard card.
- Routine Inbox item.
- App notification.
- Desktop notification.
- Text/email/push only if explicitly configured.

Each routine should support:

- Notify only if important.
- Include in daily brief only.
- Ask before texting.
- Quiet hours.
- Max pings per day.

Avoid becoming Clippy with a caffeine addiction.

# 19. Model Router and Local AI Workbench

## 19.1 Core Idea

Archivist should support many models without forcing the user to bounce between many apps.

Model routing should eventually allow:

- Local/private.
- Balanced.
- Strongest.
- Cheapest.
- Custom per task.

## 19.2 Routing by Task Type

Suggested routing:

```text
Local model:
  summaries
  first-pass scans
  classifications
  ratings
  routine brief drafts
  simple code review
  novelty brainstorming
  overnight batch jobs

Cloud model:
  hard reasoning
  final prose polish
  deep architecture
  complex code plans
  high-stakes decisions
  premium image/text generation
```

The user-facing version can be simple:

```text
Private / Local
Balanced
Strongest
Cheapest
Custom
```

The power-user version can map specific workflows to specific providers/models.

## 19.3 Local Model Value

Local models make routine loops economically viable.

Cloud swarms can get expensive quickly.

A strong local machine can run lower-cost background work overnight:

- Scan all lore.
- Re-summarize changed files.
- Find contradictions.
- Draft Daily Brief.
- Generate novelty suggestions.
- Review code for obvious issues.

Cloud models can be reserved for final, difficult, or premium steps.

## 19.4 Provider Layer

The provider layer should support:

- OpenAI.
- Anthropic.
- Gemini.
- Ollama.
- LM Studio.
- Local embedding providers.
- Image generation providers.
- Mock provider.

Each provider should declare capabilities:

- text generation.
- streaming.
- structured outputs.
- embeddings.
- image generation.
- image editing.
- vision.
- audio.

## 19.5 Cost and Privacy Awareness

Every workflow should know whether it is using:

- local model only.
- cloud model.
- mixed mode.

The UI should show:

- provider used.
- estimated cost/tokens where available.
- whether context may leave the machine.
- which context snippets were sent.

# 20. Momentum Planner and Personal OS Layer

## 20.1 Core Idea

Archivist can eventually include a lightweight planning assistant focused on obligations, health, creative momentum, and project continuity.

This is not a rigid scheduling system.

It should help the user keep promises to themselves without becoming an annoying tyrant.

## 20.2 Planning Style

Not this:

```text
Plan every minute of my day.
```

More like:

```text
Help me keep momentum across the few things I said mattered.
```

Suggested lanes:

- Body.
- Money.
- Craft.
- Home.
- Relationships.
- Admin.
- Rest.

For Zach-like use:

- Gym / walk / health.
- Archivist build.
- Shard worldbuilding.
- Job/contract search.
- Startup ideas.
- House/family obligations.
- Rest/recovery.

## 20.3 Lazy Time Blocking

The planner should suggest loose blocks, not a prison schedule.

Example:

```text
Today has three real blocks:
- Move body.
- Build one small Archivist piece.
- Touch Shard for 20 minutes.
```

## 20.4 Tone Configuration

Tone matters.

Options:

- Gentle.
- Coach.
- Drill Sergeant Lite.
- Goblin Wrangler.
- Silent Dashboard Only.

Example nudges:

```text
You’ve been in planning mode for three days. Build one tiny thing.

You have not moved much today. Walk first, wizard later.

Shard is getting dusty. Want a 15-minute myth prompt?

You are doom-looping architecture. Pick one rep and ship it.
```

## 20.5 Consent and Boundaries

This feature gets personal quickly.

The app must ask:

- What can I track?
- What can I remind you about?
- How blunt should I be?
- When should I shut up?
- Can I use your calendar?
- Can I use local activity signals?
- Can I text or email you?

Default should be conservative.

No creepy tracking.

No shame-based productivity.

# 21. Shard Lab and Game Workflow Integration

## 21.1 Core Idea

Shard Lab is the personal wizard build of Archivist.

It uses Archivist Core primitives to become a full creative and game-development pipeline.

The public product can remain clean while Shard Lab contains experimental modules.

## 21.2 Shard Workflow Loop

Possible loop:

```text
Morning Codex generates myth
  ↓
User likes the relic
  ↓
Archivist extracts Relic entity
  ↓
Art Director generates visual prompt
  ↓
Image tool creates concept art
  ↓
UE5 Bridge creates placeholder asset
  ↓
Quest Builder creates quest hook
  ↓
NPC Lab creates character reactions
  ↓
Codex updates published myth clue
  ↓
Game later rewards discovery
```

The key is shared context.

The UE5 bridge should know:

- Artifact lore.
- Visual art direction.
- Gameplay role.
- Quest context.
- Naming conventions.
- Folder conventions.

because Archivist already knows them.

## 21.3 UE5 Bridge Safety

The UE5 bridge should not freestyle the game project.

It should create proposals for:

- New asset folder.
- DataAsset metadata.
- Placeholder mesh reference.
- Icon/concept prompt.
- Test level placement.
- Naming and folder conventions.

Mutations require approval.

Game project generated folders and critical code should remain no-touch unless a specific workflow explicitly requests and previews changes.

## 21.4 Transmedia Canon Loop

Shard can use books, codices, myths, and game content as a loop.

Example:

```text
Read myth book.
Find clue.
Enter game.
Discover artifact/power/cosmetic.
Reinterpret myth.
Return to book with new meaning.
```

Published books can become canon artifacts, but not every claim inside them needs to be objective truth.

Unreliable narrators are useful.

A bard’s account can be canon as an object in the world while still being wrong, biased, poetic, or incomplete.

## 21.5 Thematic Mirror

Shard lore and Archivist implementation mirror each other.

Shard in lore:

```text
a novelty-seeding intelligence
that generates worlds
and preserves useful patterns
```

Archivist in reality:

```text
a novelty-seeding workspace
that generates ideas
preserves useful fragments
and accretes meaning over time
```

This theme is allowed to inform the personal build’s flavor, naming, and rituals.

The public product should keep the metaphor subtle.

# 22. Core Backend Systems

## 16.1 Library Manager

Responsible for:

- Creating libraries.
- Registering libraries.
- Loading library metadata.
- Creating `.archivist/` folder.
- Creating default AI wiki files.
- Reading/writing policy files.
- Listing libraries.
- Removing library registration without deleting files.

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
10. Archivist proposes initial soul, structure, glossary, style, art direction, and policies.

## 16.2 Library Wiki Service

Responsible for:

- Creating default AI wiki files.
- Reading wiki files into context.
- Updating wiki files through approved diffs.
- Validating wiki structure.
- Keeping policies machine-readable.
- Keeping meaning human-readable.
- Making the Library understandable to future AI sessions.
- Managing style guide, art direction, and taste memory.

The wiki should not be edited silently.

AI-assisted updates to the wiki require proposal and approval.

## 16.3 Policy Engine

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

## 16.4 Scanner

Responsible for:

- Walking Library folders.
- Respecting ignore rules.
- Detecting file types.
- Detecting no-touch folders.
- Hashing files.
- Recording metadata.
- Detecting changed/new/missing files.
- Updating catalog records.

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
- 3D file metadata

The scanner should catalog first. Parsing and indexing can happen as jobs.

## 16.5 Parser

Responsible for extracting text and structure from supported files.

For markdown:

- Extract title.
- Extract headings.
- Preserve heading hierarchy.
- Preserve links.
- Detect frontmatter.
- Split into sections.

For text:

- Read content.
- Normalize encoding.
- Chunk by size or semantic boundaries.

Later parsers can support:

- PDF text extraction.
- DOCX extraction.
- OCR.
- Image metadata.
- Audio transcription.
- Video metadata.
- 3D metadata.

## 16.6 Catalog

The catalog is the local structured record of files.

It stores:

- File path.
- Normalized path.
- File name.
- Extension.
- Size.
- Hash.
- Created/modified timestamps.
- Last scan time.
- File status.
- Touch policy.
- Summary.
- Detected topics.
- Related files.
- Indexing state.
- Current/deprecated/archive state.

The catalog lets Archivist know what exists without constantly rereading every file.

## 16.7 Chunker

Responsible for splitting file content into retrievable chunks.

Initial chunking strategy:

- Prefer markdown headings.
- Keep related sections together.
- Use fallback chunk size for long text.
- Store file path and heading metadata.
- Avoid chunks so large they pollute retrieval.
- Avoid chunks so small they lose context.

Each chunk should track:

- Chunk ID.
- File ID.
- Library ID.
- Text.
- Heading.
- Chunk index.
- Token estimate.
- Vector ID.
- File status.
- Current/deprecated/archive state.
- Created/updated timestamps.

## 16.8 Embedding Provider

Responsible for converting chunks and queries into embeddings.

Provider options:

- Local embedding model.
- Ollama embedding endpoint.
- Cloud embedding provider with user key.
- Mock embeddings for tests.

Embeddings should be provider-agnostic.

The app should not assume OpenAI, Pinecone, or any hosted service.

## 16.9 Vector Store

Responsible for local semantic search.

Stores embeddings and metadata.

Search should support filters like:

- Library ID.
- File path.
- Folder.
- File status.
- Current/canon/draft/deprecated/archive.
- Topic.
- Modified date.
- User-selected scope.

Initial requirement:

- Search chunks semantically.
- Return ranked results.
- Include source file paths.
- Include snippets.
- Include metadata.

## 16.10 Search Service

Responsible for combining retrieval methods.

Search should eventually support:

- Vector search.
- Keyword search.
- Metadata filters.
- Path-aware search.
- Glossary-aware search.
- Recent files.
- User-pinned files.
- Decision-aware search.
- Deprecated-aware search.
- Chat-history search.
- Profile-aware search.
- Style/taste-aware retrieval for creative tools.

RAG should not blindly retrieve similar chunks.

It should retrieve with intent.

Example:

User asks:

```text
Find inconsistent Thresh lore.
```

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

```text
What is the current Thresh origin?
```

Search Service should:

- Prefer Canon.
- Include decisions.
- Include glossary.
- Avoid Deprecated unless needed for explanation.

## 16.11 Context Compiler

The Context Compiler assembles the context packet for each AI call.

Inputs:

- User message.
- Active Library.
- Active chat.
- Active AI profile(s).
- Current mode/task.
- Policies.
- Relevant AI wiki files.
- Relevant retrieved chunks.
- Recent chat messages.
- Relevant chat chunk summaries.
- Current proposal state.
- Allowed tools.
- Recent decisions.
- Operation history if needed.
- Deprecated state if relevant.
- Style guide/art direction/taste memory if creative output.

Outputs:

- AI system/task prompt.
- Context snippets.
- Profile instructions.
- Tool constraints.
- Safety instructions.
- Source metadata.
- Optional context preview for user.

The Context Compiler is one of the most important systems in the app.

It lets the app stay generic while each Library becomes specialized through its AI wiki.

## 16.12 AI Harness

Responsible for:

- Calling AI providers.
- Passing compiled context.
- Managing tool schemas.
- Parsing structured responses.
- Validating responses with Zod.
- Retrying/repairing malformed responses where safe.
- Tracking token usage.
- Showing what context may leave the machine.
- Supporting local/cloud provider choices.

The AI Harness should not directly change files.

It can produce:

- Answers.
- Summaries.
- Classifications.
- Proposed operations.
- Conflict reports.
- Draft content.
- Structured proposal data.
- AI wiki update proposals.
- Rich chat blocks.
- Codex drafts.
- Illustration suggestions.
- Ratings prompts.

Actual file changes go through proposals, policy, diffing, human approval, operation service, and ledger.

## 16.13 Proposal Engine

The Proposal Engine creates structured plans for possible changes.

Proposal types:

- OrganizationProposal.
- RenameProposal.
- MoveProposal.
- IndexGenerationProposal.
- ContentEditProposal.
- CanonUpdateProposal.
- ConflictReportProposal.
- ArchiveProposal.
- DeprecatedMoveProposal.
- WikiUpdateProposal.
- StyleGuideUpdateProposal.
- TasteUpdateProposal.
- ArtDirectionUpdateProposal.
- CodexSaveProposal.
- ProfileUpdateProposal.

Every proposal should include:

- ID.
- Goal.
- Affected files.
- Proposed operations.
- Reasons.
- Risk level.
- Confidence.
- Preview data.
- Diff data.
- Deprecated destination data.
- Approval state.
- Undo plan.
- Created timestamp.

The Proposal Engine does not apply changes directly.

It creates plans.

The user approves plans.

The Operation Service applies approved plans.

## 16.14 Diff Engine

Responsible for showing before/after changes.

For file moves/renames:

```text
From:
  /old/path/thresh messy idea.md

To:
  /new/path/02_Races/Thresh/Drafts/Thresh - Swamp Origin Draft.md
```

For Deprecated moves:

```text
From:
  /Races/Thresh/old origin.md

To:
  /Deprecated/Races/Thresh/old origin.md

Reason:
  Superseded by Canon/Races/Thresh.md
```

For content edits:

```text
- The Thresh were created directly by the Maw.
+ The Thresh appear to be a people transformed by long exposure to Maw-corrupted wetlands.
```

The UI should allow:

- Approve.
- Reject.
- Modify destination.
- Modify content.
- Skip item.
- Approve selected.
- Save proposal for later.

## 16.15 Operation Ledger

The Operation Ledger records every filesystem change.

Each operation should record:

- Operation ID.
- Proposal ID.
- Library ID.
- Timestamp.
- Action type.
- Original path.
- New path.
- File hash before.
- File hash after.
- Approved by user.
- Reversible status.
- Reason.
- Result.
- Error if failed.

Example action types:

- create_file.
- modify_file.
- move_file.
- rename_file.
- archive_file.
- deprecate_file.
- create_folder.
- update_memory_file.
- update_wiki_file.
- save_codex_entry.
- update_profile.
- update_style_guide.
- update_taste_memory.

The ledger enables:

- Undo.
- Audit trail.
- User trust.
- Future grandma git.
- Future Git integration.
- Future semantic history.

## 16.16 File Operation Service

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

---

# 23. Local API Design

## 17.1 API Principles

The local API should be:

- Simple.
- Typed.
- Validated with Zod.
- Bound to localhost.
- Protected by startup token eventually.
- Thin over core services.
- Easy to test.
- Reusable for future MCP bridge.

Routes should not contain business logic.

Routes call controllers or services.

## 17.2 Initial Route Groups

Suggested route groups:

- `/libraries`
- `/wiki`
- `/scans`
- `/search`
- `/chat`
- `/profiles`
- `/proposals`
- `/operations`
- `/files`
- `/deprecated`
- `/codex`
- `/exports`
- `/settings`
- `/providers`
- `/tools`
- `/workflows`
- `/routines`
- `/briefs`
- `/notifications`
- `/planner`
- `/modules`

## 17.3 Example API Routes

Libraries:

```text
GET    /libraries
POST   /libraries
GET    /libraries/:libraryId
PATCH  /libraries/:libraryId
POST   /libraries/:libraryId/scan
```

Wiki:

```text
GET    /libraries/:libraryId/wiki
GET    /libraries/:libraryId/wiki/:fileName
POST   /libraries/:libraryId/wiki/propose-update
```

Search:

```text
POST   /search
POST   /libraries/:libraryId/search
```

Chat:

```text
GET    /libraries/:libraryId/chats
POST   /libraries/:libraryId/chats
GET    /libraries/:libraryId/chats/:chatId/messages
POST   /libraries/:libraryId/chats/:chatId/messages
POST   /libraries/:libraryId/chats/:chatId/stream
GET    /libraries/:libraryId/chats/:chatId/chunks
POST   /libraries/:libraryId/chats/:chatId/chunks/archive
```

Profiles:

```text
GET    /libraries/:libraryId/profiles
POST   /libraries/:libraryId/profiles
GET    /libraries/:libraryId/profiles/:profileId
PATCH  /libraries/:libraryId/profiles/:profileId
```

Proposals:

```text
POST   /proposals
GET    /proposals/:proposalId
PATCH  /proposals/:proposalId
POST   /proposals/:proposalId/approve
POST   /proposals/:proposalId/apply
```

Operations:

```text
GET    /operations
GET    /operations/:operationId
POST   /operations/:operationId/undo
```

Deprecated:

```text
GET    /libraries/:libraryId/deprecated
POST   /libraries/:libraryId/deprecated/propose
```

Files:

```text
GET    /files/:fileId
POST   /files/open
POST   /files/reveal
POST   /files/open-in-editor
```

Codex:

```text
POST   /libraries/:libraryId/codex/generate
GET    /libraries/:libraryId/codex
GET    /libraries/:libraryId/codex/:codexId
POST   /libraries/:libraryId/codex/:codexId/rate
POST   /libraries/:libraryId/codex/:codexId/propose-save
```

Exports:

```text
POST   /libraries/:libraryId/exports/pdf
GET    /libraries/:libraryId/exports/:exportId
```

Settings:

```text
GET    /settings
PATCH  /settings
```

Providers:

```text
GET    /providers
PATCH  /providers/:providerId
POST   /providers/:providerId/test
```

Tools:

```text
GET    /tools
GET    /tools/:toolId
POST   /tools/:toolId/preview
POST   /tools/:toolId/run
```

Workflows:

```text
GET    /workflows
POST   /workflows
GET    /workflows/:workflowId
POST   /workflows/:workflowId/run
GET    /workflow-runs/:runId
POST   /workflow-runs/:runId/cancel
POST   /workflow-runs/:runId/rerun-step
```

Routines:

```text
GET    /routines
POST   /routines
PATCH  /routines/:routineId
POST   /routines/:routineId/run-now
GET    /routine-runs/:runId
GET    /routine-inbox
PATCH  /routine-inbox/:itemId
```

Briefs:

```text
POST   /briefs/daily/generate
GET    /briefs/daily
GET    /briefs/:briefId
```

Notifications:

```text
GET    /notifications/settings
PATCH  /notifications/settings
POST   /notifications/test
```

Planner:

```text
GET    /planner/momentum
POST   /planner/momentum/generate
PATCH  /planner/preferences
```

Modules:

```text
GET    /modules
PATCH  /modules/:moduleId
GET    /editions
PATCH  /editions/current
```

---

# 24. Data Model Sketch

## 18.1 Library

```text
Library
- id
- name
- rootPath
- archivistPath
- status
- createdAt
- updatedAt
```

## 18.2 LibraryPolicy

```text
LibraryPolicy
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
```

## 18.3 WikiFile

```text
WikiFile
- id
- libraryId
- fileName
- path
- type
- summary
- lastReadAt
- lastUpdatedAt
- hash
```

## 18.4 FileRecord

```text
FileRecord
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
```

Possible library states:

- current.
- canon.
- draft.
- maybe.
- inspiration.
- archive.
- deprecated.
- rejected.
- ignored.
- no_touch.
- needs_review.

## 18.5 ChunkRecord

```text
ChunkRecord
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
```

## 18.6 ChatSession

```text
ChatSession
- id
- libraryId
- title
- type
- purpose
- isMainTimeline
- activeProfileIds
- retrievalPreferencesJson
- createdAt
- updatedAt
- lastMessageAt
```

Session types:

- main_timeline.
- lore_riffage.
- quest_workshop.
- character_chamber.
- canon_review.
- proposal_review.
- research_thread.
- codex.
- custom.

## 18.7 ChatMessage

```text
ChatMessage
- id
- libraryId
- sessionId
- role
- profileId
- content
- blocksJson
- tokenEstimate
- createdAt
- parentMessageId
- metadataJson
```

Roles:

- user.
- assistant.
- system.
- tool.
- profile.

## 18.8 ChatChunk

```text
ChatChunk
- id
- libraryId
- sessionId
- startMessageId
- endMessageId
- messageCount
- summary
- topicsJson
- decisionsJson
- sourceRefsJson
- tokenEstimate
- createdAt
- archivedAt
```

## 18.9 ChatBlock

Conceptual block schema:

```text
ChatBlock
- id
- messageId
- type
- order
- dataJson
```

Block types:

- markdown.
- code.
- file.
- image.
- pdf.
- docx.
- model3d.
- sourceList.
- diff.
- proposal.
- operationResult.
- audio.
- contextPacket.
- toolCall.
- exportArtifact.
- ratingPrompt.

## 18.10 AIProfile

```text
AIProfile
- id
- libraryId
- name
- type
- role
- voice
- tone
- prioritiesJson
- forbiddenJson
- contextPreferencesJson
- permissionsJson
- avatarPath
- profilePath
- createdAt
- updatedAt
```

## 18.11 Proposal

```text
Proposal
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
```

Proposal statuses:

- draft.
- ready_for_review.
- approved.
- partially_approved.
- applied.
- rejected.
- failed.

## 18.12 ProposedOperation

```text
ProposedOperation
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
```

Operation statuses:

- pending.
- approved.
- skipped.
- applied.
- failed.

## 18.13 OperationLedgerEntry

```text
OperationLedgerEntry
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
```

## 18.14 DeprecatedRecord

```text
DeprecatedRecord
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
```

## 18.15 CodexEntry

```text
CodexEntry
- id
- libraryId
- sessionId
- title
- type
- status
- content
- blocksJson
- sourceRefsJson
- ratingSummaryJson
- styleProfileId
- artDirectionUsed
- createdAt
- updatedAt
```

Codex statuses:

- maybe.
- draft.
- canon_candidate.
- canon.
- inspiration.
- rejected.
- deprecated.
- style_reference.
- character_voice_reference.

## 18.16 Rating

```text
Rating
- id
- libraryId
- targetType
- targetId
- toneScore
- canonFitScore
- originalityScore
- usefulnessScore
- characterVoiceScore
- keepability
- tagsJson
- notes
- createdAt
```

## 18.17 ExportJob

```text
ExportJob
- id
- libraryId
- type
- status
- template
- outputPath
- sourceRefsJson
- optionsJson
- createdAt
- completedAt
- error
```

---

## 24.18 ToolDefinition

```text
ToolDefinition
- id
- moduleId
- name
- description
- inputSchemaJson
- outputSchemaJson
- riskLevel
- requiresApproval
- requiredCapabilitiesJson
- enabled
- createdAt
- updatedAt
```

## 24.19 WorkflowDefinition

```text
WorkflowDefinition
- id
- moduleId
- name
- description
- inputSchemaJson
- outputSchemaJson
- stepsJson
- requiredCapabilitiesJson
- riskLevel
- enabled
- createdAt
- updatedAt
```

## 24.20 WorkflowRun

```text
WorkflowRun
- id
- workflowId
- libraryId
- chatId
- status
- inputJson
- outputJson
- startedAt
- finishedAt
- error
```

Possible statuses:

- queued
- running
- waiting_for_approval
- succeeded
- failed
- cancelled

## 24.21 WorkflowStepRun

```text
WorkflowStepRun
- id
- workflowRunId
- stepId
- name
- kind
- status
- inputJson
- outputJson
- artifactIdsJson
- startedAt
- finishedAt
- error
```

## 24.22 Routine

```text
Routine
- id
- name
- description
- workflowId
- scheduleJson
- triggerJson
- scopeJson
- permissionsJson
- notificationPolicyJson
- enabled
- createdAt
- updatedAt
```

## 24.23 RoutineOutput

```text
RoutineOutput
- id
- routineId
- workflowRunId
- libraryId
- type
- title
- summary
- body
- status
- confidence
- riskLevel
- createdAt
- updatedAt
```

Possible statuses:

- new
- read
- saved
- dismissed
- snoozed
- converted_to_task
- converted_to_proposal
- converted_to_lore_fragment
- converted_to_decision

## 24.24 DailyBrief

```text
DailyBrief
- id
- date
- title
- summary
- sectionsJson
- sourceRoutineRunIdsJson
- createdAt
```

## 24.25 ModuleRecord

```text
ModuleRecord
- id
- name
- description
- enabled
- edition
- settingsJson
- createdAt
- updatedAt
```

## 24.26 MomentumPlan

```text
MomentumPlan
- id
- date
- lanesJson
- suggestedBlocksJson
- nudgesJson
- tone
- createdAt
- updatedAt
```

# 25. Key User Workflows

## 19.1 Add Library

1. User clicks “Add Library.”
2. Electron opens native folder picker.
3. User selects folder.
4. Frontend sends path to API.
5. API validates folder.
6. LibraryManager creates `.archivist/`.
7. Default AI wiki files are created.
8. Library is registered.
9. Default Main Timeline is created.
10. User is invited to describe the Library.
11. AI helps fill or refine `soul.md`, `structure.md`, `context-cheatsheet.md`, `policies.json`, `style-guide.md`, and `art-direction.md`.

## 19.2 Scan Library

1. User starts scan.
2. Scanner walks folder.
3. Ignore rules are applied.
4. No-touch detection runs.
5. Supported files are cataloged.
6. Hashes and metadata are stored.
7. Changed/new/missing files are detected.
8. UI shows scan summary.

Example scan output:

```text
214 files scanned.
163 markdown files indexed.
12 files ignored by policy.
7 possible code-project folders skipped.
39 files need analysis.
```

## 19.3 Analyze Library

1. Parser extracts text from supported files.
2. Chunker splits files.
3. AI or local heuristics summarize files.
4. Topics/entities are detected.
5. Embeddings are generated.
6. Vector index is updated.
7. Catalog records are updated.
8. Deprecated/current/canon/draft/maybe state is stored.

## 19.4 Chat With Library

1. User sends message.
2. Context Compiler loads relevant AI wiki files.
3. Search Service retrieves relevant chunks.
4. Search respects current/draft/deprecated intent.
5. Context Compiler includes recent messages and relevant old summaries.
6. AI Harness sends compact context to selected model.
7. Response includes sources and structured blocks.
8. UI displays answer, sources, files, proposals, and actions.

## 19.5 Infinite Scroll Chat History

1. User opens Library Main Timeline.
2. App loads recent messages.
3. User scrolls upward.
4. App fetches older messages before current oldest loaded message.
5. UI preserves scroll position.
6. Older chunks can be searched, summarized, and retrieved.
7. Important conclusions can be promoted to AI wiki memory through proposals.

## 19.6 Create Alternate Chat

1. User creates new chat inside a Library.
2. User gives it a purpose.
3. User selects active AI profiles.
4. User chooses retrieval preference.
5. Chat persists and can be reopened later.

Examples:

- Lore Riffage.
- Quest Workshop.
- Character Chamber.
- Canon Review.
- Morning Codex.

## 19.7 Create AI Profile

1. User creates a profile.
2. User defines role, voice, priorities, forbidden behaviors, and context preferences.
3. Profile is stored locally.
4. Profile can join Library chats.
5. Profile can later be edited through approved profile updates.

## 19.8 Organize Library

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

## 19.9 Replace or Consolidate Files

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

## 19.10 Find Inconsistencies

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

## 19.11 Update the AI Wiki

1. User says something like:
   “Going forward, organize this Library concept-first and preserve old drafts.”
2. Archivist proposes updates to wiki files.
3. Diff mode shows changes to `soul.md`, `structure.md`, or `context-cheatsheet.md`.
4. User approves.
5. Wiki files update.
6. Ledger records update.
7. Future AI behavior changes.

## 19.12 Generate Morning Codex

1. User asks for morning reading.
2. Archivist asks lightweight questions or uses defaults.
3. Context Compiler pulls Library context, active style guide, art direction, taste memory, and relevant files.
4. Writer profile drafts the piece.
5. Continuity Keeper checks it.
6. Style Auditor checks it.
7. Art Director suggests illustration moments.
8. Archivist displays the artifact in Read/Codex mode.
9. User rates it.
10. User can save as Maybe, revise, reject, or promote selected parts through proposals.

## 19.13 Export Beautiful PDF

1. User selects export type.
2. User selects source files/artifacts.
3. User selects style/template.
4. Archivist compiles content.
5. Optional AI pass polishes structure.
6. Optional images/diagrams are included.
7. User previews export.
8. App generates PDF.
9. Export is saved locally.
10. Export job is recorded.

## 19.14 Undo

1. User opens operation history.
2. User selects operation or proposal.
3. App validates reversibility.
4. App previews undo.
5. User approves.
6. File Operation Service reverses changes.
7. Ledger records undo operation.
8. Catalog/index updates.

---

## 25.10 Run Daily Brief

1. User clicks “Generate Daily Brief,” or a scheduled routine starts later.
2. Routine Scheduler creates a workflow run.
3. Workflow reads permitted Library summaries, recent messages, recent file changes, and routine settings.
4. Local model is preferred when configured.
5. System generates brief sections.
6. Outputs land in the Routine Inbox.
7. User can save, dismiss, snooze, convert to task, convert to proposal, or ask follow-up questions.

## 25.11 Run Novelty Engine

1. User triggers Novelty Engine manually or it runs as part of Daily Brief.
2. Context Compiler loads selected Library context, decisions, open questions, Deprecated material if allowed, and taste/style guidance.
3. Novelty Engine searches for parallels, contradictions, underdeveloped ideas, and forgotten fragments.
4. Output appears as suggestions, not canon.
5. User can save suggestions as Maybe, convert to lore fragment, create open question, or dismiss.

## 25.12 Code Forge Workflow

1. User selects a repo or code Library.
2. Code Forge inspects project structure in read-only mode.
3. AI proposes an implementation plan.
4. User approves the plan.
5. AI generates patch proposal.
6. Diff UI shows changes.
7. User approves selected changes.
8. File Operation Service applies patch.
9. Tool Registry runs approved tests/typechecks.
10. Workflow stores command output and summary.
11. Operation Ledger records changes.

## 25.13 Shard Lab UE5 Asset Workflow

1. User likes a generated artifact, relic, creature, or location.
2. Archivist extracts a structured entity.
3. Art Director creates visual prompt.
4. Game workflow creates a UE5 asset proposal.
5. Proposal previews folders, metadata, placeholder assets, and optional level placement.
6. User approves.
7. File Operation Service or UE5 Bridge applies only approved operations.
8. Ledger records what was created.
9. Shard Library links the entity, art, quest hook, and game asset record.

# 26. MVP Scope

## 20.1 V1 Must Have

V1 is done when Archivist can:

- Launch as a desktop app.
- Start a local Express API.
- Create/register one or more Libraries.
- Create `.archivist/` AI wiki files.
- Create a persistent Main Timeline per Library.
- Store chat messages locally.
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

## 20.2 V1 Should Not Include

Do not build these in v1:

- Whole-computer auto-cleaner.
- File deletion.
- Code project reorganization.
- Full image understanding.
- Full OCR.
- Full MCP bridge.
- Cloud database.
- Hosted accounts/auth.
- Collaboration.
- Git integration.
- Mobile app.
- Complex plugin system.
- Fully autonomous multi-agent swarm.
- Full illustrated PDF book engine.
- Full 3D UI.
- Full voice system.

## 20.3 V1.5 Candidates

These are good next features after core v1 works:

- Rich markdown rendering.
- Code block copy buttons.
- Source cards.
- Context preview.
- Basic AI profiles.
- Alternate chats.
- Infinite scroll message pagination.
- Basic Codex/Morning Reading generation.
- Basic PDF export from markdown.
- Entity avatars.
- Image generation for profile pictures.
- Open/reveal local file actions.

---

# 27. Future Features

## 21.1 MCP / ChatGPT Bridge

Future feature:

Expose a safe local tool bridge so ChatGPT, Claude, or other AI systems can query approved Libraries.

Safe tools:

```text
list_libraries()
search_library()
get_relevant_context()
get_file_summary()
create_proposal()
preview_proposal()
request_local_approval()
```

Unsafe tools should not be exposed remotely:

```text
write_any_file()
read_any_file()
delete_file()
run_shell_command()
```

External bridge operations should remain Library-scoped and approval-driven.

## 21.2 Discovery Mode

Future feature:

Scan common folders and suggest possible Libraries.

Example:

```text
Potential Libraries Found:

1. Shard Notes
2. Startup Notes
3. Homestead Planning
4. Downloads Inbox
```

The app should ask the user which folders to add as Libraries.

Do not start by scanning the whole machine automatically.

## 21.3 Image Knowledge

Future feature:

Support images and visual search.

Example user query:

```text
Where is that picture of Johnny with the big fish?
```

This requires:

- Image indexing.
- Metadata extraction.
- Possibly local vision model.
- Thumbnails.
- Face/object/event search.
- Strong privacy controls.

Not v1.

## 21.4 Grandma Git

Future feature:

A simple human-friendly versioning layer.

User-facing concepts:

- Snapshots.
- Restore points.
- Undo.
- Before cleanup.
- After cleanup.
- Named versions.

Not shown as raw Git to normal users.

Power-user future:

- Git init.
- Semantic commits.
- Semantic branches.
- Compare versions.
- Merge branches.
- Export patch.

Example semantic commit:

```text
Organized Thresh notes and deprecated old origin drafts.
```

## 21.5 Central Server / Sync

Possible future path:

- Optional central server.
- Sync between machines.
- Collaborative Libraries.
- Hosted backups.
- PostgreSQL server backend.
- User accounts.
- Permissions.

Not v1.

Keep v1 local-first.

---

## 27.5 Tool Registry and Workflow Engine

Future feature:

Build a reusable internal workflow runner that can support librarian tasks, creative tasks, code tasks, routines, and game-engine tasks.

Start with deterministic workflows and typed tool calls.

Later allow AI planning agents to propose workflows, but only after the workflow runner, tool registry, validation, approval nodes, and logs are solid.

## 27.6 Proactive Routines and Daily Briefs

Future feature:

Run bounded routines on a timer or trigger.

Examples:

- Morning Daily Brief.
- Shard Lore Watcher.
- Code Watcher.
- Opportunity Watcher.
- Library Health Check.
- Momentum Coach.

Routines produce reviewable outputs. They do not silently change files.

## 27.7 Momentum Planner

Future feature:

A lightweight life/project planner that reminds the user of obligations, health, creative momentum, and neglected goals.

It should use loose time blocks, not rigid minute-by-minute scheduling unless explicitly requested.

Tone should be configurable.

## 27.8 Shard Lab and UE5 Bridge

Future personal feature:

Use Archivist as the context engine for Shard development.

A generated myth can produce a relic. A relic can produce an entity file, image prompt, quest hook, NPC reactions, and eventually a UE5 placeholder asset proposal.

This is the private wizard build, not the first public product.

# 28. Development Philosophy

The app should be built with UX and DX as north stars.

Good architecture means:

- Easy to understand.
- Easy to debug.
- Easy to test.
- Easy to extend.
- Easy for users to trust.

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
- Rich chat as artifact stream.
- Profiles as controlled voices, not rogue agents.
- Creative tools as workflows on top of the librarian foundation.

## Philosophical Design Notes

Archivist is a practical app, but it is also allowed to have a soul.

Useful phrases to preserve:

```text
Coding is executable philosophy.

Mysticism outside. Machinery inside.

The tool is becoming an artifact from the world it is meant to create.

Generate, experience, judge, preserve, mutate, re-seed.

A novelty engine with a memory ledger.

A sacred machine for sorting signal from noise.

A digital altar for forging fiction and organizing an ADHD life.

Forgotten technical prowess mistaken for mysticism.

The Archive does not create truth. It preserves the path by which truth accreted.
```

These are flavor notes, not excuses for sloppy engineering.

The mystical surface only works if the hidden machinery is rigorous.

The app can feel like a workshop, library, shrine, command center, and creative forge, but underneath it still needs boring schemas, boring tests, boring logs, boring permission checks, boring migrations, and boring filesystem safety.

The gods are just old engineers whose comments got deleted.

## Shard / Archivist Thematic Mirror

The Shard in lore is a novelty-seeding intelligence that generates worlds and preserves useful patterns.

Archivist in reality is a novelty-seeding workspace that generates ideas, preserves useful fragments, and accretes meaning over time.

This mirroring is useful for the personal Shard Lab build and for naming/flavor, but the public product should keep the metaphor subtle.

## Design Guardrail

Do not let the poetry make the implementation sloppy.

Ritual is polish.

Safety is architecture.

Approval is law.

The user owns the Library.

## Pushback Rules

When making development decisions, prefer:

- Simple before clever.
- Mock provider before real provider.
- Local first before cloud.
- Read-only before mutation.
- Proposal before operation.
- Markdown before custom editor.
- SSE before WebSockets for AI text streaming.
- SQLite before PostgreSQL for local v1.
- Rich message blocks before complex agent workflows.
- Foundation before witchcraft.
- Deterministic workflows before autonomous swarms.
- Tool Registry before agent chaos.
- Routine Inbox before notifications.
- Manual Daily Brief before scheduled background jobs.
- Explicit capabilities before broad permissions.
- Local model first for cheap recurring work.
- Public Core before personal wizard modules.

The witchcraft is allowed.

It just has to sit on a stable table.

---

# 29. Logical Todo List

## 1. Project Foundation

### 1a. Root workspace

Set up:

- Root `package.json`.
- Workspaces for `frontend` and `backend`.
- `.nvmrc`.
- `.npmrc`.
- Node version check.
- One-command dev script.

### 1b. Desktop shell

Electron should:

- Launch app window.
- Load Vite dev server during development.
- Use HMR inside Electron.
- Provide native folder picker through preload/IPC.
- Shut down cleanly.

### 1c. Local API skeleton

Add:

- Express app.
- Health route.
- Local auth token middleware later.
- Error handler.
- Zod validation helper.
- Basic logging.

## 2. UI Shell

### 2a. Port visual shell from old Vue app to React

Build:

- Navbar.
- Sidebar.
- SidebarButton.
- ChatWindow.
- CSS variable theme system.
- Mock data.

### 2b. Archivist vocabulary

Convert old TavernTalk concepts:

```text
TavernTalk -> The Archivist
NPCs -> Libraries / Profiles / Entities depending on context
Chats -> Timelines / Sessions
Create NPC -> Add Library or Create Profile
Select an NPC -> Select a Library
Message TavernTalk -> Message Archivist
```

### 2c. Electron-first development

Develop in Electron window with HMR.

Browser is optional debugging fallback.

## 3. Rich Chat Basics

### 3a. Message model

Create message/block types:

- ChatMessage.
- ChatBlock.
- MarkdownBlock.
- CodeBlock.
- SourceListBlock.
- FileBlock.
- ProposalBlock.

### 3b. Renderer

Create:

- MessageRenderer.
- MarkdownBlockRenderer.
- CodeBlockRenderer with copy.
- SourceCardRenderer.
- ProposalPreviewRenderer.

### 3c. Composer

Support:

- Text input.
- Enter to send.
- Shift+Enter newline.
- Disabled/loading states.
- Attach button placeholder.

## 4. Shared Types and Schemas

### 4a. Define core schemas

Create Zod schemas for:

- Library.
- LibraryPolicy.
- WikiFile.
- FileRecord.
- ChunkRecord.
- ChatSession.
- ChatMessage.
- ChatChunk.
- ChatBlock.
- AIProfile.
- Proposal.
- ProposedOperation.
- OperationLedgerEntry.
- DeprecatedRecord.
- SearchResult.
- ContextPacket.
- AIProviderConfig.
- CodexEntry.
- Rating.

### 4b. Define API contracts

Create request/response schemas for:

- createLibrary.
- scanLibrary.
- searchLibrary.
- createChat.
- sendMessage.
- getMessages.
- createProfile.
- createProposal.
- approveProposal.
- applyProposal.
- undoOperation.
- proposeWikiUpdate.
- proposeDeprecatedMove.

### 4c. Set up frontend API client

Create a typed client used by React.

The frontend should not call fetch randomly throughout components.

## 5. Library Creation and AI Wiki

### 5a. Native folder picker

Use minimal Electron IPC only for folder selection.

### 5b. Create Library API

Build endpoint:

```text
POST /libraries
```

It should:

- Validate path.
- Create Library record.
- Create `.archivist/`.
- Create default AI wiki files.
- Create default Main Timeline.
- Store Library in global SQLite.

### 5c. Default AI wiki files

Create:

- `soul.md`
- `policies.json`
- `structure.md`
- `context-cheatsheet.md`
- `glossary.md`
- `decisions.md`
- `deprecated.md`
- `style-guide.md`
- `art-direction.md`
- `taste.md`
- `ignore.md`

### 5d. Library dashboard

UI should show:

- Library name.
- Root path.
- Scan status.
- Policy summary.
- AI wiki status.
- Main Timeline button.
- Scan/search/settings buttons.

## 6. SQLite Persistence

### 6a. Choose SQLite driver

Recommended candidates:

- better-sqlite3.
- drizzle.
- kysely.

Keep it simple.

### 6b. Create global DB

Store:

- Libraries.
- App settings.
- Provider settings.
- Recent activity.

### 6c. Create per-library DB

Store:

- File catalog.
- Chunks.
- Chat sessions.
- Messages.
- Message chunks.
- AI profiles.
- Proposals.
- Operations.
- Deprecated records.
- Wiki metadata.
- Codex entries.
- Ratings.

## 7. Main Timeline and Chat Persistence

### 7a. Store messages

Save every message locally.

### 7b. Load recent messages

Load recent 50 messages by default.

### 7c. Infinite scroll upward

Fetch older messages before current oldest.

Preserve scroll position.

### 7d. Archive/summarize chunks

Summarize older chunks later for retrieval and memory.

## 8. Scanner and Catalog

### 8a. Implement file scanner

Start with:

- `.md`
- `.txt`
- `.json`
- `.yaml`
- `.yml`

Respect:

- `.archivist/`
- ignore rules.
- no-touch patterns.

### 8b. Store file records

For each file, store:

- Path.
- Name.
- Extension.
- Size.
- Hash.
- Modified timestamp.
- Status.
- Library state.

### 8c. Add scan UI

Show:

- Files scanned.
- Files ignored.
- Files changed.
- Errors.
- No-touch warnings.

## 9. Parser and Chunker

### 9a. Markdown parser

Extract:

- Title.
- Headings.
- Body text.
- Frontmatter if present.

### 9b. Chunk files

Use heading-aware chunks first.

Fallback to size-based chunks.

### 9c. Store chunks

Save chunks in SQLite with file metadata and Library state.

## 10. Local Vector Index

### 10a. Pick local vector DB

Start with the simplest viable local option.

Likely:

- LanceDB.

But keep a VectorStore interface.

### 10b. Add embedding provider

Start with one provider:

- OpenAI user key.
- Ollama local.
- Mock provider for tests.

Even if OpenAI is first, storage remains local.

### 10c. Index chunks

Generate embeddings and upsert to local vector DB.

## 11. Search

### 11a. Semantic search endpoint

Build:

```text
POST /libraries/:libraryId/search
```

Return:

- Matching chunks.
- File paths.
- Snippets.
- Scores.
- Metadata.
- Current/draft/deprecated state.

### 11b. Search UI

Show:

- Search box.
- Results.
- Source file path.
- Snippet.
- Open/reveal buttons.
- Current/deprecated badge.

### 11c. Deprecated-aware search

Search should treat Deprecated material differently depending on intent.

Default answers should prefer current/canon material.

Contradiction/history/recovery tasks may include Deprecated.

## 12. AI Harness and Context Compiler

### 12a. Provider settings

Let user configure:

- OpenAI API key.
- Local provider URL.
- Model choice.

### 12b. Context Compiler v1

For a chat request, include:

- User message.
- Relevant AI wiki files.
- Retrieved chunks.
- Recent messages.
- Source metadata.
- Allowed behavior.
- Current/deprecated search intent.
- Active AI profile.

### 12c. Chat endpoint

Build:

```text
POST /libraries/:libraryId/chats/:chatId/messages
```

Return answer plus sources/blocks.

Streaming can come later.

## 13. AI Profiles

### 13a. Profile schema

Implement AIProfile.

### 13b. Profile CRUD

Create/list/edit profiles.

### 13c. Use profile in chat

Let a chat have one active profile first.

Multi-profile mode comes later.

## 14. Proposal Engine

### 14a. Proposal schema

Implement:

- Proposal.
- ProposedOperation.
- Statuses.
- Risk levels.

### 14b. Simple organization proposal

Given selected files, generate proposed moves/renames.

Start rule-based before going fully AI-driven if needed.

### 14c. Deprecated move proposal

Support proposals that move superseded files to Deprecated.

This must include reason, replacement path, and undo plan.

### 14d. Save proposal

Store proposal in SQLite and `.archivist/proposals/` if useful.

## 15. Diff and Preview UI

### 15a. Move/rename preview

Show before/after paths.

### 15b. Content diff preview

For generated or modified markdown files, show text diff.

### 15c. Deprecated preview

Show what files are being moved to Deprecated and why.

### 15d. Approval controls

Allow:

- Approve all.
- Approve selected.
- Skip.
- Edit destination.
- Reject proposal.
- Save for later.

## 16. Apply and Undo

### 16a. File Operation Service

Apply only approved operations.

Validate through Policy Engine.

### 16b. Operation Ledger

Record every change.

### 16c. Undo basic operations

Support undo for:

- Move file.
- Rename file.
- Create file where safe.
- Create folder where safe.
- Move to Deprecated.

## 17. Library Wiki Editing

### 17a. View wiki files

UI can show:

- `soul.md`
- `structure.md`
- `context-cheatsheet.md`
- `glossary.md`
- `decisions.md`
- `deprecated.md`
- `style-guide.md`
- `art-direction.md`
- `taste.md`
- `ignore.md`
- `policies.json`

### 17b. Edit wiki files

User can edit manually.

### 17c. AI-assisted updates

User can say:

```text
Going forward, organize this Library concept-first and preserve old drafts.
```

Archivist proposes an update to wiki files.

User approves via diff.

## 18. Codex and Creative Tools

### 18a. Basic Codex generator

Start as chat artifact, not full PDF.

### 18b. Ratings

Allow user to rate generated material.

### 18c. Taste/style updates

Propose updates to `taste.md` and `style-guide.md`.

### 18d. PDF export later

Start simple, then make beautiful.

## 19. Tool Registry and Workflow Engine

### 19a. Tool Registry skeleton

Create internal tool definitions for:

- search_library
- read_wiki_files
- create_proposal
- read_file_summary
- run_safe_command mock
- generate_codex mock

Each tool should have:

- id
- input schema
- output schema
- risk level
- required capabilities
- handler

### 19b. Workflow Run model

Add data structures for:

- WorkflowDefinition
- WorkflowRun
- WorkflowStepRun
- StepArtifact

### 19c. Simple linear runner

Start with linear workflows before graph workflows.

First non-AI workflow can be Scan Library.

First AI workflow can be Generate Daily Brief or Generate Lore Fragment.

### 19d. Approval step

Support a workflow step that pauses until the user approves a proposal.

## 20. Proactive Routines and Daily Brief

### 20a. Manual Daily Brief button

Create a manual workflow that summarizes:

- recent Library changes
- recent chat decisions
- open questions
- suggested next actions
- optional lore snippet

### 20b. Routine Output Inbox

Add UI and storage for routine outputs.

Actions:

- save
- dismiss
- snooze
- convert to proposal
- convert to task later
- convert to lore fragment

### 20c. Scheduler later

After manual runs work, add scheduled runs.

Start with daily.

Do not build always-on haunted cron demons before the Library system is stable.

## 21. Model Router

### 21a. Provider capability metadata

Track what each provider supports:

- text
- embeddings
- streaming
- structured output
- vision
- image generation
- local/private

### 21b. Routing modes

Add simple routing modes:

- local/private
- balanced
- strongest
- cheapest
- custom later

### 21c. Cost/privacy display

Show whether a workflow used local, cloud, or mixed providers.

## 22. Momentum Planner

### 22a. Preferences only first

Define planning preferences:

- lanes
- tone
- quiet hours
- nudge frequency
- goals

### 22b. Lazy time block generator later

Generate loose daily suggestions, not minute-by-minute schedules.

### 22c. Tone presets

Support:

- Gentle
- Coach
- Drill Sergeant Lite
- Goblin Wrangler
- Silent Dashboard Only

## 23. Code Forge and Shard Lab Later

### 23a. Code Forge design only

Document the code assistant module before implementation.

### 23b. UE5 bridge design only

Document the UE5 bridge and safety model before implementation.

### 23c. Do not build too early

These are personal power modules. They come after core Library, chat, proposal, workflow, and tool systems.

## 24. Hardening

### 19a. Safety tests

Test:

- Cannot delete files.
- Cannot move files outside Library.
- Cannot touch ignored paths.
- Cannot modify code project folders.
- Cannot apply unapproved proposal.
- Cannot move files to Deprecated without approval.
- Cannot silently update AI wiki files.
- Cannot silently promote generated lore to canon.

### 19b. Error handling

Make failed operations recoverable and clearly reported.

### 19c. Backup/restore sanity

Ensure operation ledger can recover from partial failures.

## 25. Future Bridge

### 20a. Design safe tool API

Expose only Library-scoped tools.

### 20b. Add local bridge server

Optional stable port or user-started bridge.

### 20c. Add MCP-style compatibility

External AI can query but not directly mutate without local approval.

---

# 30. Final North Star

Build the smallest clear system that gives us real powers:

- Create a Library.
- Teach Archivist what it is through an AI wiki.
- Explain the directory structure so AI understands context, not just filenames.
- Scan and index files locally.
- Maintain a persistent Main Timeline.
- Chat with the Library.
- Search it.
- Use AI profiles for different voices and roles.
- Generate structured artifacts.
- Propose changes.
- Preview diffs.
- Show what gets replaced.
- Move superseded files to Deprecated instead of deleting them.
- Approve changes.
- Record history.
- Undo mistakes.
- Eventually generate beautiful codex-style reading material and exports.
- Eventually run bounded routines that prepare daily briefs and novelty suggestions.
- Eventually host a Tool Registry and Workflow Engine so new modules can share the same UI house.
- Eventually route work across local and cloud models without forcing the user to bounce between apps.
- Eventually support personal power modules like Code Forge, Momentum Planner, and Shard Lab without bloating the public core.

Everything else comes later.

The Archivist is a local AI librarian with memory, manners, taste, tools, routines, and a paper trail.

It is also allowed to become a creative engine, a personal command center, and a small local AI operating layer, but only after the librarian foundation is solid.

One workspace. Many tools. Shared context. Human approval. Local ownership.

1. Bend / Redmond, OR
2. Boise, ID
3. Chattanooga, TN
4. Greenville, SC / Asheville, NC
5. Bellingham, WA
6. Burlington, VT
7. Duluth, MN
8. Bozeman / Missoula, MT
9. Reno, NV
10. Traverse City / Marquette, MI
