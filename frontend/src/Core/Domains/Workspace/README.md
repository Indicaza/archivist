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
- `validate()` checks the invariants: each view in exactly one anchor (or
  minimized in exactly one Gravity Well entry, never both), each
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

## Magnets: picking things up (`workspace_magnets`)

A magnet is the handle a person drags: a view (one tab) or an anchor (the
whole group). It is never a copy; resources are never touched by a drag.

- **Preview is not commit.** `Magnets::preview` judges a drop on a copy of
  the model: `canDrop` first (locks, invalid targets), then the result
  itself. It refuses `NO_CHANGE` (it would land where it is) and
  `TOO_SMALL` (a panel would end up under the minimum size, unless the
  window was already that cramped), each with a message for the ghost.
- **Floating by default, docking on aim.** A drag only docks where the
  person is clearly aiming (`Magnets::dockTargetAt`): a tab strip (at
  once), a band along a panel's edge or the window border (once held there
  for `dockDwellMs`, 220 ms), or anywhere in an empty panel. Anywhere else,
  and outside every window, the drop floats it as a panel inside the main
  window (`SurfaceRecord::inMainWindow`, geometry in the main window's content,
  no screen), at the size it had (capped), its top-left corner just past
  the pointer (it is always held by that corner).
  Floating panels are ordinary surfaces: tabs, splits, docking and Return
  all work in them, and there can be any number. A magnet already alone in
  a floating panel just moves it. Windows of their own still open from the
  tab and panel menus.
- **One drag state.** The store publishes `Docking.dragState`: `phase`
  (`idle`, `pickup`, `free`, `aiming`, `invalid`, `preview`, `detached`),
  `floats` and `floatRect`, the zone being `aim`ed at and `dwellMs`, what is
  carried and its origin rect, the target, `valid` and the reason, the
  landing rect, `reflow` (every panel in the target window, committed rect
  `from` -> proposed rect `to`; a panel the drop creates has a provisional
  id), and `previewSerial`, which bumps on every target change. Motion and
  physics read this; nothing in it is committed.
- **One drag, one copy.** `beginDrag` refuses while a drag is on, so nothing
  under the pointer can start a second one. What is carried is lifted out of
  its place (drawn at opacity 0, never hidden or rebuilt, since it holds the
  pointer) while its picture travels. If it loses the pointer anyway, the
  workspace's drag shield takes the pointer over and carries the same drag
  to the release (`DockDragShield`); a floating panel's bar never hands its
  drag to anything underneath.
- **One commit.** `finishDrag` checks the target again if the layout
  changed during the drag, applies it as one operation and records one
  undo step. `dragFinished(outcome, details)` reports `committed`,
  `cancelled` or `refused`.
- **Return to Previous Position.** At pickup the store remembers where the
  magnet was (`Magnets::placeOf`): a tab's panel and position, or for a
  panel, the neighbour or window edge it sat against, its share of the
  split, and its floating window or panel if it had one to itself. `planReturn`
  turns that into a drop (or a new floating window) against the layout as
  it is now, or refuses (`GONE`, `NO_CHANGE`). Returning again goes back.
  The memory is per project and per session.
- **Steady aim.** `dockTargetAt` takes the zone already chosen (`held`):
  it stays chosen while the pointer is within `DockZones::hold` (14 px) of
  it, so two zones that meet (a panel's edge and the window border, two
  panels' edges) never flicker between each other, and jitter never
  restarts a preview (`previewSerial` holds).
- **What the pointer means** (in this order; every zone is hit-tested on
  the committed layout, never the animated preview):
  1. An empty sidebar spot under it: dock there.
  2. A zone already shown (a split preview, a tab join) holds while the
     pointer stays within `hold` of it (`Magnets::tabStripAt`,
     `dockTargetAt` with `held`).
  3. A tab strip, exactly where it is drawn (the panel's top
     `editorTabHeight`): join that panel at the slot under the pointer, at
     once. A strip doesn't reach beyond itself to claim the pointer; once
     joined it keeps it `hold` px above and below and across the divider
     beside it. A strip too long to show scrolls while a tab is held near
     either end.
  4. The window border (`windowBand`): dock along that edge (held for
     `dockDwellMs`).
  5. A panel's edge band: split it (held for `dockDwellMs`); an empty panel
     anywhere: fill it. On a divider, the nearest edge beside it.
  6. Anywhere else: float.
- **The caret is the slot.** A tab drop's `index` is a slot between the
  tabs as they are now, which is what the caret shows; within its own panel
  the tab leaves its slot first (`applyDrop`). A whole panel's tabs go in at
  the slot too (`mergeAnchor` with an index). Joining previews move no
  panels (the strip stays under the pointer); the layout settles on drop.
- **A carried panel isn't a target.** While a floating panel's only panel is
  carried, that surface is skipped when finding what is under the pointer
  (`DockWorkspace.liftedSurfaceId`), so what lies beneath it can be docked
  into.

### Motion (QML only)

Every magnet animation is presentation: transforms, opacity and the
carried picture, read from `Docking.dragState` and `dragFinished`. None of
it writes to the model, so the committed state is always exactly what the
store published; a preview never adds an undo step, and a cancel restores
everything with no history entry.

- **Pickup** (`DockCarry`): wherever it was grabbed, the picture is held
  by its top-left corner, `magnetHoldOffset` px past the pointer's tip, so
  the pointer never hides what it aims at (it slides there as it lifts).
  It lifts to `magnetPickupScale` around that corner, with a deeper
  two-layer shadow and an accent edge. It is carried
  small (at most `magnetCarryMaxWidth` x `magnetCarryMaxHeight`) so it is
  easy to aim; over open space a full-size outline shows where it would
  float, and on release it grows into it. The pointer always drives its
  position directly (no smoothing), so it never lags.
- **Momentum**: the picture hangs from the corner it is held by and swings
  back against the way it moves (`Theme.magnetSwingAngle`: a rotation
  about that corner, `magnetTiltPerSpeed`, at most `magnetTiltMax`) on a
  loose spring (`magnetSwing*`), so it rocks past level and settles when
  the pointer stops. A
  flick on release glides a floating drop on (`magnetThrowTime` of its
  speed, at most `magnetThrowMax`). The glide goes into the frame the drop
  commits, so what lands is exactly what is stored.
- **Floating panels** (`DockFloatingPanel`): holding one panel, a floating
  panel has no bar of its own; the panel's header is its one handle and
  close. Holding a split, a slim bar moves the group: it shrinks to
  `magnetPanelMoveScale` toward its top-left corner, that corner springs
  to the pointer (held like a carried picture), it swings as it moves and
  a flick throws it on. Opened on its own (not dropped there, where the
  carried picture lands in its place) it pops in from `magnetAppearScale`. A floating panel may hang off the
  window's sides and bottom; `magnetKeepVisible` px of it and its header
  (`magnetKeepHeader`) always stay in reach.
- **Magnetic pull** (`DockDropOverlay`, `magnet.pull`): while free, the
  nearest edge of the panel or window border under the pointer glows,
  stronger the closer it is (reach: `magnetProximityReach` times the dock
  band), with a pixel-sized tremor once close. It hides as soon as a zone
  is aimed at or a preview shows.
- **Making room**: panels spring to the preview's rects
  (`magnetReflowSpring`/`Damping`) in a ripple: the spring is stiffest for
  the panel nearest where the change was asked for (the pointer,
  `DockSurfaceArea.rippleOrigin`) and up to `magnetRippleSpread` softer
  for the farthest, so the near ones move first, the landing area to its rect
  (`magnetLanding*`). In a tab strip the tabs after the insertion point
  step aside (`magnetTabGap`, a `Translate`; the strip's layout is not
  touched) and the caret sits in the gap.
- **Release**: a committed drop lands the picture on the committed rect
  (the anchor's `rectX/Y/Width/Height`, not where its spring is now, or the
  floating frame) on an `OutBack` curve (`magnetReleaseOvershoot`), so it
  overshoots a little and ends exactly there; then it fades over the real
  panel, which shows again (`carry.landed`) and gives a small nudge
  (`DockAnchor.receive`). Cancelled or refused, it flies back
  (`magnetReturnDuration`).
- **Tokens**: all timings live in `Theme.qml` (`magnet*`). With reduced
  motion every one of them jumps: no springs, tremor, overshoot or nudge.

## Gravity Wells (`workspace_wells`)

A Gravity Well keeps work within reach without keeping it on screen: views
put away (minimized) and resources pinned for quick access. It owns nothing.

- **References, not copies.** A minimized view is still a view of the model,
  with the same id, resource and view state; it sits in a Well entry instead
  of a panel (`WorkspaceModel::minimizeView`). Its resource is untouched, so
  a buffer keeps its unsaved edits, a terminal keeps its process, a chat and
  its agent run keep going. The store keeps publishing it (hidden, in no
  window), so its live content stays parked like a background tab's.
- **One entry per resource.** Minimizing a pinned resource joins its pinned
  entry; two minimized views of one resource share an entry (the latest
  comes back first). An entry with no resource (a welcome page) is that
  view's alone. What a click does follows the entry's state
  (`Wells::entryState`, `Wells::activate`):

  ```text
  minimized (pinned or not)   restore its latest minimized view
  pinned, shown in a panel    bring that view forward
  pinned, nothing shows it    open it again (its pinned view type)
  ```

  Recent resources and running agents are shown by the UI from what it
  already knows; they aren't entries.
- **Where it goes back.** Minimizing records the view's place the way a
  Magnet pickup does (`Magnets::placeOf`), and the place is saved with the
  entry, so restore works after a restart. `Wells::restore`: its own panel
  if it's still there, at its old tab position (`original`); else where that
  panel sat, beside its old neighbour or along the window edge with its old
  share, or as its own floating panel or window, the way Return to Previous
  Position does it (`returned`); else a panel in the main window showing the
  same kind of content, or the panel new content goes to (`fallback`).
  Never nowhere.
- **Closing, minimizing, suspending.** Closing a minimized view closes it
  (the usual unsaved-work guard first); a pinned entry stays. Minimizing
  never suspends anything. A process doesn't outlive the app: after a
  restart a minimized terminal comes back the way a terminal tab does.
- **History and persistence.** Wells are part of the model's data, so
  minimize, restore and pin are ordinary workspace steps (undo and redo put
  the whole state back, Wells included) and they are saved in the
  Collection's snapshot (`session.wells`). Minimized views are also written
  with the session's views, in no panel, so an older Archivist opens them
  as tabs instead of losing them. Applying a layout and putting an
  arrangement back leave what is put away put away.
- **Placement.** One Well per project for now, the main window's
  (`WorkspaceModel::kMainWellId`), at a position stored as a share of the
  window (bottom centre by default) so it stays put as the window resizes.
  `WellForm` and `orientation` leave room for vertical, arc, circular and
  collapsed forms; only the linear one is drawn.
- **Drawing it.** `DockWell.qml` draws a Well over the main window's layout
  (never in it, so nothing reflows) as a few small spheres
  (`DockWellIcon.qml`, soft lights from `WellGlow.qml`), pinned first, then a
  faint seam, then what's only minimized. There is no bar or grip. At rest it
  fades to almost nothing but a small locator; as the pointer comes near it
  fades in and lifts; over it, the Dock wave: the sphere nearest the pointer
  grows to about 1.6x, its neighbours to about 1.3x, and the rest step aside
  (`WellGeometry.magnify`). Waking and engaging use separate in and out
  distances so it never flickers on the edge (`WellGeometry.proximity`).
  `WellGeometry.js` places the spheres in fixed slots that pointer input is
  tested against; the wave, the lift and the trip into the Well are
  transforms on top, so the slot under the pointer never moves. None of this
  writes to the model while the pointer moves. Reduced motion turns the wave
  and springs off. A tab's menu has Minimize to Well and Keep in Well; a
  sphere's menu restores, pins, unpins or closes. The Well floats above
  floating panels (below the drag picture) and moves anywhere over the
  layout when dragged by the space around its spheres, snapping to the
  centre line and edges. Its place is a share of the main layout's area,
  not the window, so it moves with the workspace frame by frame as the
  sidebar opens, closes or resizes (only a drop settles with a spring), and
  it rises clear of the floating composer when that's in its way
  (`DockWorkspace.composerRect`), easing back down when it goes.
- **Keeping it quick.** Minimizing and restoring change the model in well
  under a millisecond; the cost is in what the screen rebuilds. So a tab
  strip changes only the tab that came or went (`TabStrip.js`), menus are
  built the first time they open, and panels borrow their Monaco page from
  a small warm pool in `DockWorkspace` (two kept, let go after ten idle
  minutes): a panel put in the Well gives its page back, and the panel
  coming back takes it, so the editor doesn't start again. Set
  `QT_LOGGING_RULES="archivist.wells.timing=true"` to log each minimize and
  restore, and where publishing spent its time.
- **The Genie.** Minimizing, tucking and restoring play a Genie between the
  panel and its sphere (`DockGenie.qml`, `shaders/genie.vert`/`.frag`). It
  warps a picture, never the live panel: on minimize the panel's area is
  captured once on the GPU (a `ShaderEffectSource` with `live: false`; a
  floating panel uses `grabToImage`, since its window goes away), the model
  is changed at once, and the picture flies on a grid mesh: it pinches
  toward the sphere from the first frame, slides down the funnel, lit in
  the accent as it's drawn into the neck, over a soft shadow
  (`genie_shadow.frag`). The sphere swells open as it comes, then squashes,
  flashes and ripples as it lands; on a click it dips and pops as the panel
  pours out. Restoring changes the model on the click, hides the panel while
  its picture flies out and follows the panel each frame so the trip ends
  exactly on it; the real panel shows as the picture lands, so there are
  never two. One trip per entry: a click during a minimize restores when it
  lands, a click during a restore is ignored. The Well stays revealed while
  a trip plays. A floating panel tucking itself away asks first
  (`setTuckAnimated`, `tuckRequested`), so its picture is taken before it
  goes, then the UI calls `tuckAway`. Every entry keeps a small copy of its
  picture (at most 480px across before the screen's scale, ~2-3 MB) within
  a 48 MB budget, oldest dropped first, so restores pour out the real panel
  at once. If the picture is late or missing, a plain card flies instead
  (never nothing); with reduced motion it's a short fade and shrink; if the
  shader can't load it falls back to the plain path. The shaders are drawn
  once at start so the first trip doesn't stutter. Undo and redo don't play
  it. The shaders are baked for every Qt Quick backend (Metal, Vulkan,
  OpenGL, Direct3D); after editing them run `scripts/bake-genie-shaders`.
  `QT_LOGGING_RULES="archivist.wells.genie.info=true"` logs each trip.
- **Whole panels.** A panel's menu or its header button puts the whole panel
  away as one entry (`Wells::minimizeAnchor`): its name, flags, active tab and
  views in tab order. A click brings it back as it was, at its old place
  (`Wells::restore` dispatches to the panel path). The main window's last
  panel stays, empty, to take its tabs back. Locked panels stay put.
- **Dropping on the Well.** While dragging a tab or panel, a pointer over the
  Well aims at it (`WorkspaceStore::updateDragOverWell`); letting go puts it
  away in one step, the same as the menu.
- **Tucking away.** A floating panel in the main window goes back into the
  Well when focus moves elsewhere, as one history step, unless it is marked
  Leave on Top (`SurfaceRecord::leaveOnTop`, saved with the surface, not a
  history step). Sidebar panels and separate windows never tuck.

## The sidebar

Tabs and panels dock into the sidebar too: into one of its tabs (any rail
tab but the Navigator, including the person's own), or at its bottom, under
every tab. Each spot holds one surface (`SurfaceRecord::sidebarSlot`: the
rail tab's id, or `sidebar.bottom`), with its own tabs and splits like any
other; `setSurfaceSidebarSlot` refuses the main layout (`PRIMARY_SURFACE`)
and a spot already taken (`SLOT_TAKEN`).

- **Dropping in.** Over an empty spot a drag docks there on release, with
  no dwell (`updateDragOverSlot`; `dragState.slot` says which). One drop,
  one undo step ("Docked … in the sidebar"). A spot that holds something is
  an ordinary surface: drops dock into its panels as anywhere else.
- **Taking out.** Dragged out (or Float from the menu), it floats; Return
  puts it back in its spot if that is still free.
- **A tab deleted.** The shell tells the store which spots exist
  (`setSidebarSlots`). Deleting a tab from the rail sends what was in it
  back to the main layout; anything left in a spot that no longer exists
  (restored from an older save, or by undo) floats instead. Nothing is
  closed.
- **The bottom spot** keeps its height as the surface's geometry.
- **Layouts** arrange the windows, not the sidebar: applying one keeps what
  is docked there.

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
  every applied agent change (compose, show, arrange), every put back, and
  every drag that lands (and every Return to Previous Position). Up to 20
  steps are kept, and a new step clears redo.
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
