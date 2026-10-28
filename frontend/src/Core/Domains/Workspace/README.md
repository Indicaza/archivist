# Workspace docking

Archivist's workspace is a set of windows (surfaces), each holding a recursive
split tree of Anchors. An Anchor is a dockable tab group that can host any
registered view type: Chats, Monaco editors, terminals, previews, diffs, and
later Agent Theater and plugin views.

This folder is the model and its rules. It has no Qt Quick dependency and is
covered by `tests/core_workspace_test.cpp`.

## Identities

Four identities with separate lifecycles:

```text
resource   document buffer, Chat, terminal session, diff   owned by its domain store
view       one presentation of a resource (a tab)          owned by the workspace
anchor     ordered views + active view                     owned by the workspace
surface    a native window with a split tree               owned by the workspace
```

Layout operations only change view → anchor → surface membership. They never
touch resources, so moving, splitting, detaching or switching layouts cannot
lose an unsaved buffer, restart an AI run, kill a terminal, or reset a Chat.

A resource's worktree is part of its identity (`ResourceRef::libraryId`, and
document ids are `collectionId:libraryId:fileId`). Views never pick up the
worktree of whichever window has focus.

Two views may show the same resource (`duplicateView`). They share the
resource; neither owns it.

## Model rules (`workspace_model`)

- Every mutation is a transaction: it succeeds and leaves the model valid, or
  fails and leaves it byte-for-byte unchanged.
- `validate()` checks the invariants: each view in exactly one anchor, each
  anchor in exactly one leaf, splits with ≥2 children and sizes summing to 1, no
  same-orientation nesting, exactly one primary surface that is never empty.
- Only `closeView` / `closeAnchor` remove views. Callers guard those against
  unsaved work. Closing a floating window moves its views into the main window.
- Anchors created on purpose (toolbar, palette, preset) are pinned and survive
  losing their last tab. Incidental anchors made by a split collapse when
  emptied.
- `previewDrop` simulates a drop on a copy and returns every anchor's rect, so
  the UI can ease panels into their proposed positions without touching state.
- `recoverSurfaces` moves windows off monitors that disappeared and returns them
  to their preferred monitor when it comes back.

## Persistence (`workspace_codec`)

- **Layout**: windows, split tree, sizes, anchors and what each prefers to hold
  (`roles`, plus `affinity` to resources for captured layouts). Presets and named
  layouts are layouts. Applying one re-homes the open views; it never opens or
  closes content.
- **Session**: views, their resources and view state, and which anchor each
  view is in.
- **Snapshot**: one layout plus one session, for restart and crash recovery.

Loading is forgiving. Views whose anchor is gone land in the target anchor.
Unknown view types are kept, so a missing plugin never costs the user their
tabs. A snapshot from a newer Archivist is refused instead of half-read.

## View contract (`view_type_registry`)

`ViewTypeDescriptor` is the internal, versioned contract a future plugin SDK
will expose: type id (`vendor.name`), contract version, state version with a
migration hook, resource kind, duplicate policy, whether it is worktree bound,
and how a live instance may move (`TransferPolicy`).

## Moving live views

View content items (terminals, previews, the live chat) are keyed by view and
reparent themselves into whichever window shows their view, including from one
native window to another. Qt WebEngine Quick renders through the scene graph,
so a page survives the move with its state (a terminal keeps its scrollback and
session, no reload). Editors share their Anchor's Monaco page, so an editor that
moves to an Anchor in another window is rebuilt there from the shared buffer and
its view state (cursor, scroll).

```text
same window      reparent the live item (no reload)
another window   reparent the live item; editors rebuild from buffer + view state
```

### Floating windows (`App/Domains/Workspace/workspace_store`, `Dock/DockFloatingWindow.qml`)

- Dragging a tab or a panel past every window shows a miniature of the window
  it would become under the pointer; releasing opens it there. Tabs and panels
  also have "Move to New Window" and "Return to Main Window".
- Drags report the pointer in global coordinates, so they cross windows: the
  window under the pointer previews the drop, others stay still.
- A window remembers its frame and monitor in the Collection's snapshot and
  reopens there. When a monitor goes away (at launch or while running), its
  windows move onto a screen that exists, sized to fit; they go back when the
  monitor returns.
- Closing a floating window moves its tabs into the main window. Nothing closes.
- Coordinates are Qt's device-independent virtual desktop, so mixed-DPI
  monitors need no special handling.

Platform compromises:

- Wayland does not let apps place windows, so a torn-off window opens where the
  compositor puts it and saved positions are only a hint there.
- The tear-off miniature is a translucent tool window; without a compositor
  (some X11 setups) its corners draw opaque.

This is safe because canonical state lives natively. Document text is in
`Core/Domains/Documents/document_buffers` (versioned, shared by every view of a
document). Terminal processes live in `TerminalSessions` and replay scrollback
when a view reattaches. Chats and AI runs live in their stores and the AI
runtime.

## Anchors as workstations

An anchor can be configured, and the configuration belongs to the anchor, so
it survives docking, splitting, floating, layouts and restarts.

- **Name.** `renameAnchor`; an empty name goes back to the automatic one
  ("Editor", "Chat", "Terminal 2", "Shelf"). Anchors that hold views are
  numbered before empty ones.
- **Layout lock.** Nobody (person, automation or agent) can move, close,
  resize, merge, float or dock beside the anchor, take tabs out of it, or apply
  a layout over it. Freed or needed space goes to unlocked siblings, so a
  locked anchor keeps its size. Its content stays usable.
- **Content lock.** Automation and agents can't replace, retarget or add to
  what the anchor holds (a locked "Architecture" chat never becomes another
  chat). The person still can.
- **Shelf** (`switchable`). The anchor shows one view and a picker instead of
  tabs. The items are ordinary views, so switching never closes, reloads or
  restarts anything.
- **Worktree binding** (`libraryId`). New terminals and other new resources
  started from the anchor use it. Without one, `resolveLibrary` tries the
  active view's Library, then the Explorer's, then the project default. A
  resource keeps the Library it was created with.

Each mutating operation takes an `Origin` (`User`, `Automatic`, `Agent`). The
rules are checked in the model, so the UI, the store and the command layer all
get the same answers. Refusals carry a code (`LAYOUT_LOCKED`,
`CONTENT_LOCKED`, `UNLOCK_REFUSED`) and a sentence for the person. Agents and
automation may lock an anchor but never unlock one. `anchorCapabilities`
answers what a caller may do, with a reason for each refusal.

`workspace_commands` exposes these as named commands (`anchor.list`,
`anchor.describe`, `anchor.rename`, `anchor.setLayoutLock`,
`anchor.setContentLock`, `anchor.setSwitchable`, `anchor.setWorktree`,
`anchor.addResource`, `anchor.removeResource`, `anchor.selectResource`,
`anchor.reorderResource`, `view.open`, `view.move`) for a future agent tool.
`WorkspaceStore.runCommand` runs them from QML, as an agent by default.

All of this is optional in saved snapshots; older snapshots read with
defaults.

## Agents and the workspace

People ask for arrangements in plain words ("open these files beside our
conversation", "make the code bigger", "put everything back"). Seven AI tools
carry those requests; none of them mention anchors or ids.

### Composing a workspace

`workspace_compose` (`workspace_compose.h`) lets the model say what the
workspace should look like instead of how to get there:

- **Spec.** A template (`focus_references`, `chat_code`, `code_diff`,
  `research_desk`, `agent_overview`) or a spatial primitive, up to 6 named
  panels with what they hold (`code`, `docs`, `chat`, `terminal`, `diff`,
  ...), a position for each, relative weights and up to 8 items each (16 in
  all), a panel to focus, and `save_as`.
- **Primitives, not trees.** The model never builds a split tree. It picks
  one primitive and names positions; the tree comes from native code:
  - `grid` (rows x columns, up to 3 x 3 and 6 cells): positions such as
    `top-left`, `bottom-right`, `r2c1`. Cells nobody names become empty
    panels, which take over existing panels (or one tab each from the
    busiest one) so what was open stays in view. Columns line up from row
    to row. "4 panes, chat bottom-left" is `grid 2 x 2` with the chat at
    `bottom-left`.
  - `columns` (`left`, `center`, `right`) and `rows` (`top`, `middle`,
    `bottom`) are one-row and one-column grids.
  - `split`: the `primary` panel large (about 62%), the rest stacked on one
    side of it (the first other panel's `right`/`left`/`top`/`bottom`).
  - `stack`: everything as tabs in one panel.
  - No primitive: a grid for rows/columns or four or more panels, a split
    when one panel asks for more room, otherwise columns.
- **Patch or replace.** The model only states the desired result: a recipe
  (or nothing) and which items go in which named panel. `mode: patch` (the
  default) keeps the current panels where they are: a slot named like an
  existing panel gets its items there, a new name adds one panel at its
  position (left, right, top, bottom), and panels nobody named are left
  alone. It runs as ordinary `show`/`add` steps in one transaction.
  `mode: replace` builds a new arrangement from the recipe as described
  below. Naming a recipe, template or grid size always means replace.
- **Built-in grid commands.** "4 panes, chat on bottom left", "2x2 grid",
  "three columns" and "6 panels" are parsed natively and composed without
  a model call.
- **The bottom chat.** When a composition gives this conversation a panel,
  the bottom chat (the same conversation) folds away; undo brings it back.
- **Templates** are versioned JSON recipes inside `workspace_compose.cpp`.
  They're starting points: the model fills their panels by name, may change
  weights, and extra panels go in a column at the side.
- **Planning** checks the whole spec first and refuses it with a plain
  message if it's too big or inconsistent. It turns the spec into an
  ordinary layout document. Panels that already show a slot's content keep
  their ids, so their editors and terminals aren't rebuilt and the change
  animates (springs on the panels, a short staggered fade for new ones,
  nothing with reduced motion).
- **Applying** is the Intents `compose` action: the layout and every slot's
  content go into one working copy, which is checked and committed once.
  That's one publish to QML, one undo step, and one temporary arrangement
  (Put back), unless `save_as` keeps it as a named layout. Layout and
  content locks refuse the whole change. Views nobody placed stay open in a
  panel of their type. Floating windows join the main window.
- **Geometry is solved natively** for the real panel area (after the
  sidebar and the bottom panel), not by the model. Weights are relative and
  capped at 1.8x a sibling, so "chat dominant with three images stacked on
  the right" gives chat about 64% and a 36% column. Every panel keeps at
  least 300 x 180 pixels; when that can't fit, the lightest neighbouring
  panels share one panel as tabs, and each merge is reported (`adjusted`).
- **Resources are resolved in order:** views already open (by name, path or
  title, in the chat's worktree first), then the Library catalog. Each item
  is reported as `reused`, `opened`, `not_found`, `ambiguous` (with the
  candidates) or `unsupported`. A missing or unclear file is never replaced
  by another one.
- **Verified before it's shown.** The composition is tried on a copy of the
  model first; the store checks every panel exists, is a usable size, holds
  its items, focus is where it was asked, and no view was lost. Only then
  is it applied (one publish, one undo step, one notice). A failed check
  changes and announces nothing (`VERIFY_FAILED`), so the workspace and the
  reply can't disagree.
- **Panels keep what they show.** A reused panel keeps its views; only views
  of removed panels move (to a panel of their type, never a chat's).
- **Follow-ups** ("give the images a little more room") compose again from
  the last arrangement, which describe reports as `lastArrangement` while
  its panels still exist. Same names keep the same panels, so views stay
  put and the change animates.
- **Result.** It's compact: the panels with their sizes and what they show,
  `resources` and `adjusted`.
- **Background chats** get a staged suggestion, as with every other tool.

### How a turn is routed

- **Routing comes before retrieval.** The runtime reads the message with
  the turn before it and the layout names (`classifyWorkspaceIntent`):
  - a workspace request ("switch to Research", "Research please")
  - a follow-up to one ("try another", "switch back", "do that again")
  - a mixed request that also needs files found ("open the files that
    explain X beside the chat")
- **Pure layout requests skip Library search** and the Library tools. They
  keep only the recent conversation, and the model gets a short text
  summary of the panels and layouts. The target is one tool call.
- **Routing never authorises anything.** The tools are offered on every
  turn, and the workspace decides what applies:
  - the person's message in the conversation in front applies right away
  - background conversations are staged
  - drags in progress and locks refuse
  - "Don't rearrange unless I ask" stages what didn't read as a request
- **Replies are checked against results.** If a reply claims a change
  ("done", "switched", "created") but no workspace tool reported `applied`,
  the runtime adds a line saying the workspace didn't change and why.
- **Results are checked against the committed model.** Compose results are
  verified before they're reported, and so is the current layout after a
  switch.
- **Built-in commands skip the model.** "Switch to Research", "put
  everything back" and "undo that" run natively (`workspaceCommand`); the
  model is only asked if the workspace refuses.
- **No round just to confirm.** When a layout-only turn's tool calls all
  applied (or were staged), the reply is built from their results
  (`workspaceAcknowledgement`) instead of another model round.
- **Timings.** `model.started` carries `workspaceIntent`, `retrievalSkipped`
  and `prepareMs`; `model.completed` carries the tool and round counts plus
  `workspaceApplied`, `workspaceStaged`, `workspaceFailed`,
  `workspaceAcknowledged` and `workspaceNative`.

Each model round trip takes seconds, so a request should take as few as
possible. The runtime puts a description of the workspace (panels and
layouts) in the instructions, so the model doesn't have to ask first, and
`workspace_arrange` takes a whole layout in one call. If a turn uses up its
tool rounds (8 for workspace requests), the model gets one last round
without tools to say what's done and what's left, instead of the turn
failing.

| Tool | Does |
|---|---|
| `workspace_view` | Describes the workspace in words: areas, what each shows, where it sits, its size and locks. |
| `workspace_show` | Shows files, a chat, a terminal or an open view, reusing what is already open. |
| `workspace_compose` | Describes the whole workspace at once: named panels with what they hold, their relative weights and their files, arranged by a template or a strategy. Native code builds it and applies it as one change (see below). |
| `workspace_arrange` | Runs a list of steps as one change (all or nothing, one undo): focus, resize, move, keep (lock in place), label, add (an empty named panel) or remove (tabs move elsewhere). `save_as` saves the result as a named layout in the same call. |
| `workspace_restore` | Puts back the arrangement before the last change, before the whole task, or a saved layout by name. |
| `workspace_preference` | Remembers, forgets or lists how the person likes the workspace. |
| `workspace_layout` | Lists, switches to, saves, renames or deletes named layouts. Saving over a name needs `replace`; built-in layouts can't be changed. |

The layers, from the model up:

- `workspace_resources`: resource identities for files, terminals and chats,
  and which view type shows a file. They match what the QML builds, so an
  agent and the person open the same view.
- `workspace_placement`: where a new view goes (`chooseAnchor`) and whether a
  view of that resource is already open (`existingView`). The same rules
  serve the Explorer, commands and agents.
  - Files go to a panel that can hold files: empty, already showing files,
    or meant for them by its roles. Never into a chat or terminal panel
    while another fits, and never into a panel bound to a different
    worktree.
  - Among those, the person's target panel wins, then the focused one, then
    the panels they worked in most recently, then panels meant for the type
    or bound to the file's worktree.
  - Chats and terminals go to a panel meant for them, else one already
    showing that type (in the same worktree when there's a choice).
  - Content-locked panels are skipped unless nothing else exists.

A new collection starts with one editor area. Chat and Terminal live in the
bottom panel, so the default never shows a second copy of either; layouts
with more panels are in the layout picker.
- `workspace_intents`: `describe` and `perform`. Vague destinations ("the
  right", "beside the chat", "the docs panel", "this panel") resolve against
  names, contents and positions (`resolveArea`). `perform` works on a copy
  and commits only if every step worked, so a refused request changes
  nothing. Focus stays where it was unless the request asks for it.
- `workspace_arrangements`: `restore` puts an earlier arrangement back while
  keeping everything that happened since: views that still exist, panel
  names and locks, window positions. Only views the agent opened, are clean
  and aren't terminals are closed. A layout-locked panel that would move
  refuses the restore.
- `workspace_gateway` (Core) and `WorkspaceAgentBridge` (App): the AI runtime
  runs on its own thread; tools call the gateway, and the bridge runs the
  request on the UI thread with a timeout. A request that times out is
  dropped before it runs.
- `WorkspaceStore` (`workspace_store_agent.cpp`): decides what a request may
  do right now.
  - Applied at once only when the person asked for a workspace change in the
    conversation that's in front, and isn't dragging a panel.
  - Otherwise prepared as a suggestion (`agentSuggestion`) the person can
    apply or dismiss. Background agents never restore on their own.
  - Focus only moves when asked for, and never within 1.5 s of the person
    typing or clicking.
  - Each applied request is one step in the workspace history (below).
  - Everything is logged in `agentActivity`.
- Temporary arrangements. The first agent change in a task remembers the
  arrangement before it (`temporaryArrangement`).
  - It is kept per collection under
    `workspace/collections/<id>/docking/temporaryArrangement`. That's apart
    from named layouts, which agents never write.
  - It is stored without focus, drag or animation state, and survives a
    restart.
  - "Put everything back" (`before_task`, or the notice's Put back) restores
    it in one step, closing only clean, non-terminal tabs that agents opened.
  - Keep (`keepArrangement`) makes the current arrangement the normal one.
    Undoing the task's first change also ends it.

Workspace history (`WorkspaceStore::undo` / `redo`, `canUndo`, `canRedo`,
`undoSummary`, `redoSummary`). There is one list of steps, grown from the
agent undo stack; the old one-step layout undo is folded into it.

- A step is recorded for every layout switch (picker, shortcut or agent),
  every applied agent change (compose, show, arrange) and every put back.
  Dragging panels by hand is not recorded. Up to 20 steps are kept, and a
  new step clears redo.
- Each step stores the model data before it, the views it opened, the views
  it closed, the bottom panel, the current layout and the temporary
  arrangement record. Undo and redo both go through
  `Arrangements::restore`, which only moves panels and tabs:
  - views that still exist keep their state;
  - undo closes the clean, non-terminal views the step opened, and never
    one with unsaved work;
  - redo brings back the views the undo closed, with the same ids and
    resources (`reopen`), so buffers and sessions attach again;
  - locked panels refuse a step that would move them.
- Resources, buffers and document edits are never touched. Document edits
  have their own undo (AI edit proposals).
- Agents use `workspace_restore` with `last_change` (undo), `redo` or
  `before_task`. "undo" and "redo" typed on their own run natively.
- `undoLayout` and `undoAgentChange` remain as names for `undo` (the
  notices call them).

Preferences (`workspace_preferences`) are a few plain settings:

| Key | Values | Effect |
|---|---|---|
| `chat_location` | bottom, docked | `bottom`: the conversation is shown in the bottom panel, never docked |
| `main_editor` | large, normal | `large`: side panels open at 28% and "larger" stops at 45% unless a size is given |
| `rearranging` | only_when_asked, when_helpful | `only_when_asked`: unasked requests only suggest, and nothing new is added for content that wasn't placed |
| `area_purpose` | a purpose, for one panel | Material for that purpose opens there ("Use for Documentation" in the panel's More menu) |

- **Duration.** Each preference is for this session or always. Lasting ones
  are stored as an indented JSON list under `workspace/preferences`.
- **When an agent may store one.** Only when the message states a standing
  preference ("always", "from now on", "I like my…", "don't … unless").
  Otherwise it is kept for the session, so a one-off never becomes
  permanent.
- **Precedence.** A session preference overrides a stored one; the newer of
  equals wins. Explicit destinations and sizes always win over preferences.
- **Revoking.** Each is listed, with Forget, in the Layouts popup.
- **Who may set them.** Background conversations can't set preferences.

The panel menu keeps the common actions on top (Rename, Lock in Place, Tabs
or Shelf, Split, Maximize, Close). The rest (automatic name, content lock,
documentation panel, new-file target, worktree, windows) is under More.

The bottom panel (Chat, Terminal, Problems) stays the main place for the
conversation. It can be folded to its tabs, and "beside our conversation"
unfolds it rather than docking another chat. The shell reports its state
with `setCommandSurfaceState`; agents ask for it through
`commandSurfaceRequested`.

The runtime offers the workspace tools only when the person's message asks
for a workspace change. Locks and refusals come back to the model as
`permission_denied` with the person-facing sentence.

## Document buffers (`Core/Domains/Documents`)

Monaco pages hold copies of a document and sync through versioned edits.
An edit against a stale version is refused and the view resyncs; one person
types into one view at a time, so there is no operational transform. Dirty
buffers outlive their last view, and unsaved text is written to the crash
snapshot.
