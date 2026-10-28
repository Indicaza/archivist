# Shell

The app around the workspace: where the person is (Home or a project's
workspace), the left-edge rail, and what they opened lately. No Qt Quick
dependency; covered by `tests/core_shell_test.cpp`.

## Destinations (`shell_navigation`)

```text
home        the chat-first start page
workspace   the project: panels, editors, the bottom panel
```

Going Home never changes the workspace. It stays loaded underneath, the
same size, with its panels, editors, terminals and running chats, and
coming back shows it exactly as it was.

The first launch (nothing saved) starts on Home. After that the app reopens
where the person left it.

## Rail ids

Each rail item has a stable id. Saved state, future Spaces, plugins and the
AI's UI guidance refer to these, never to positions.

| Id | Item | Old index |
|---|---|---|
| `navigation.home` | Home | none |
| `navigation.navigator` | Workspace Navigator | 0 |
| `navigation.archive` | Archived Libraries | 1 |
| `navigation.search` | Library Search | 2 |
| `navigation.plugins` | Plugins | 3 |
| `navigation.tools` | Tools | 4 |

The side panel's state is still saved per project under
`workspace/collections/<id>/shell/activeViewIndex`, so the old index is kept
for reading it back.

A click (`activate`):

- Home: go Home; on Home already, back to the workspace.
- A panel while on Home: back to the workspace with that panel open.
- The open panel: close the side panel.
- Another panel: open it.

## Recent items (`recent_items`)

One list, newest first, one entry per kind and id, at most 48. Entries are
references (kind, id, project, library, title, a few scalar details) and
never content, so the list can't compete with the stores that own the
resources. Forgetting an entry doesn't touch the resource.

Kinds written today: `chat`, `document`, `collection`. Home reads them; the
Orbit will too.

## App side

`App/Domains/Shell/shell_store` is the `Shell` QML singleton. It holds the
state, saves `shell/destination`, `shell/recents` (compact JSON) and
`shell/homeDraft` in QSettings, and publishes only real changes.

Home's draft belongs to Home, not to the text field: leaving Home, switching
projects or restarting keeps it. `ChatStore.startChat` creates the
conversation and sends the first message once it exists; the draft is
cleared only after that, so a failure never loses it and a message is never
sent twice.
