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

Qt Quick items can be reparented inside one window. Qt WebEngine views should
not be moved between native windows. So:

```text
same window      reparent the live item (no reload)
another window   save view state → rebuild in the target window → restore
```

This is safe because canonical state lives natively. Document text is in
`Core/Domains/Documents/document_buffers` (versioned, shared by every view of a
document). Terminal processes live in `TerminalSessions` and replay scrollback
when a view reattaches. Chats and AI runs live in their stores and the AI
runtime.

## Document buffers (`Core/Domains/Documents`)

Monaco pages hold copies of a document and sync through versioned edits.
An edit against a stale version is refused and the view resyncs; one person
types into one view at a time, so there is no operational transform. Dirty
buffers outlive their last view, and unsaved text is written to the crash
snapshot.
