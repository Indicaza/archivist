# Home

Home is the personal layer above every project: a conversation up front and,
under it, modules the person chooses. No Qt Quick dependency; covered by
`tests/core_home_test.cpp`.

```text
Home → Project/Space → Anchors → Magnets → Resources
```

Home is not a workspace. It has no split tree and no Anchors, so it doesn't use
the docking engine; it is an ordered list of modules, each full width or half.
The workspace stays loaded underneath (see `Core/Domains/Shell`), so going Home
and coming back never resets work.

## What lives where

| State | Owner |
|---|---|
| Home or a project, the rail | `Shell` (`shell_navigation`) |
| Modules, order, width, presets | `HomeLayout` (`home_modules`) |
| Pins | `Pins` (`home_items`) |
| Activity events | `ActivityLog` (`home_items`) |
| Conversations, projects, layouts | `ChatStore`, `CollectionStore`, `Docking` |

Pins and events are references, never content. An event has its own id; the
conversation or file it is about is its `target`, and a lasting result it
produced is its `artifact`. Dismissing an event never touches either; pinning
from an event pins the target.

## Modules (`home_modules`)

`ModuleDescriptor` is the contract a plugin will use, the Home counterpart of
`ViewTypeDescriptor`:

- `typeId` (`vendor.name`), `contractVersion`, title, description, icon,
  default width;
- `resourceKind` and `expandViewType`: the resource the module presents and the
  workspace view type that shows it in full. A plugin supplies one resource and
  gets both presentations (compact on Home, full in an Anchor) without a
  bespoke Home integration;
- `permissions`: what the module reads, shown to the person before it's added.
  Built-ins only read what Archivist already has, so theirs is empty. Nothing
  collects in the background for a module that isn't on Home.

Built-ins: `archivist.home.resume` (Resume work), `archivist.home.pinned`,
`archivist.home.activity`, `archivist.home.layouts`.

Presets: **Balanced** (default: Resume, Pinned + Activity side by side),
**Focused** (Resume only), **Overview** (everything, Layouts included). Any edit
makes the layout `custom`; Restore default goes back to Balanced.

Loading is forgiving, like workspace snapshots: malformed and duplicate entries
are dropped, a module whose plugin is missing is kept (shown as unavailable)
so it comes back with the plugin, and built-ins a saved layout doesn't mention
are added hidden.

## Activity

Events today: `reply.finished` and `reply.failed`, written by
`RecentActivityTracker` when a reply ends. Events with the same `key` replace
each other (one line per conversation). A reply the person watched in the
workspace is recorded already read; one that finished while they were on Home
or elsewhere is unread. Stopped replies aren't recorded. At most 40 are kept.

## App side

`App/Domains/Home/home_store` is the `HomeStore` QML singleton. It saves
`home/layout`, `home/pins` and `home/activity` (compact JSON) in QSettings and
publishes only real changes. `registerModuleType` is where plugin modules will
register.

Resume work reads `Docking.savedWorkspaceSummary(collectionId)`: how many tabs
the project's saved workspace has, a few of their titles and its layout, read
from the snapshot without loading it. A project with no saved workspace says
so ("Opens a fresh workspace") instead of guessing.
