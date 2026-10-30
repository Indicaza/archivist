# Observatory

Observatory keeps a local, structured record of Archivist development builds,
so a change's effect on build time can be compared run to run. This is the
pieces so far: build capture, the UI Lab (scripted UI scenarios timed in a
real, isolated app instance), and an import of the existing
`archivist.wells.timing` runtime logs, plus a local dashboard to browse and
compare the records.

## Run a capture

```bash
npm run observatory:build
npm run observatory:build -- --label "split chat_store.cpp"
npm run observatory:build -- --follow      # also stream the full build output
```

It runs the normal development build, one phase at a time, using the same
scripts `npm run dev` uses:

| Phase          | Command                                   | What it records                          |
| -------------- | ----------------------------------------- | ---------------------------------------- |
| `ide-bundle`   | `node scripts/build-ide.mjs`              | time; whether the cached bundle was used |
| `configure`    | `scripts/qt-configure`                    | time; whether a stale cache was reset    |
| `build-plan`   | `cmake --build build/frontend-debug -- -n`| Ninja dry run: steps the build will run  |

**Bookkeeping steps.** CMake re-checks its source globs, and Qt's AUTOMOC
refreshes its timestamps and metatype lists, on every build, even when
nothing changed. Observatory counts these apart from real work
(`plannedBookkeepingSteps`, `bookkeepingSteps`), so a build that runs only
them is `no-change`. The patterns are in `ninja.mjs`.
| `native-build` | `cmake --build build/frontend-debug`      | time; Ninja progress; `.ninja_log` edges |

`scripts/qt-build` is configure followed by `cmake --build`, so the last two
phases are the same build split in two. Nothing is deleted or touched to
force a clean build. The command exits with the build's own exit code, and a
failed or interrupted (Ctrl+C) build still writes its report.

The terminal shows one line per phase and a short summary, with the failing
phase and its first error lines when something breaks.

## Where records live

```
<observatory>/runs/<runId>/summary.json   structured record
<observatory>/runs/<runId>/build.log      full stdout/stderr of every phase
```

`<observatory>` is `$ARCHIVIST_OBSERVATORY_DIR` if set, otherwise
`~/Library/Application Support/Archivist/Observatory` on macOS
(`$XDG_DATA_HOME/archivist/observatory` elsewhere). It is outside the repo and
build tree, so cleaning a build never erases history. Run ids start with a UTC
timestamp, so they sort by time; every run gets a new directory and earlier
runs are never rewritten. Nothing is uploaded.

```bash
npm run observatory -- show            # the latest run's summary
npm run observatory -- show <runId>
```

## summary.json (schemaVersion 1)

| Field | Meaning |
| ----- | ------- |
| `schemaVersion` | `1`. Bumped when a field's meaning changes. |
| `kind` | `build`, `runtime-log` or `ui-scenario` (fields below are for `build`; UI runs are described under UI Lab). |
| `runId`, `label` | Unique id; your `--label` or `null`. |
| `createdAt`, `finishedAt` | ISO 8601 wall-clock times. |
| `status` | `running` (still going, or the collector was killed), `success`, `failure`, `interrupted`, `error` (collector bug). |
| `exitCode` | What the command exited with: the failing phase's code, or 128 + signal. |
| `totalDurationMs` | Monotonic time from the first phase's start to the end. |
| `source` | `commit`, `branch` (`null` when detached), `dirty`, `changedFiles`. |
| `environment` | `platform`, `osVersion`, `architecture`, `cpuModel`, `logicalCpus`, `totalMemoryBytes`, `nodeVersion`. |
| `toolchain` | From the configured build directory: `buildType`, `generator`, `cmakeVersion`, `ninjaVersion`, `compiler` (`id`, `version`, `path`), `qtVersion`, `compilerCache` (`launcher` configured, `ccacheInstalled`, `sccacheInstalled`). |
| `buildConditions.before` | Whether the build directory, CMake cache and `.ninja_log` existed before the run. |
| `buildConditions.classification` | `value`: `clean` (no `.ninja_log` before), `no-change` (Ninja ran only bookkeeping steps), `incremental` (it ran real compile, moc or link steps), or `null` when the native build didn't run; `basis` says how it was decided. A failed build uses the dry run instead, since Ninja doesn't log failed steps. |
| `phases[]` | `name`, `command`, `status` (`success`/`failure`/`interrupted`/`skipped`), `exitCode`, `signal`, `startOffsetMs`, `durationMs`, `details`, and `error` (`message`, `excerpt`) when it didn't succeed. |
| `errors[]` | One `{ phase, message }` per problem; the details are in `build.log`. |
| `artifacts` | Paths relative to the run directory. |

Any value that could not be read is `null`, never a guess. The summary is
rewritten atomically (temp file + rename) after every phase, so a killed
capture still leaves the phases it finished.

### What is measured and what is inferred

- **Measured:** phase and total durations (Node's monotonic clock), exit
  codes, and `native-build.details.ninjaLog`: the start/end times Ninja
  itself wrote to `.ninja_log` for the steps it ran in this build
  (`stepsRecorded`, `workSteps`, `bookkeepingSteps`, `sumOfStepDurationsMs`,
  `slowestSteps`). A step is one compile, moc, qmlcache or link command, so
  `slowestSteps` are real per-file times. Paths are relative to the build
  directory. If
  Ninja recompacted its log during the build, this is marked `unavailable`.
- **Inferred from output** (labelled with a `basis` field): IDE cache reuse
  (from `build-ide.mjs`'s "using cached build" message), `ninjaSteps` (the
  `[n/total]` progress lines) and `plannedSteps` (Ninja dry run). The build
  classification comes from `.ninja_log` when the build succeeded, and from
  the dry run when it failed.

### What it cannot tell you yet

- Whether a compiler cache actually hit (only whether one is configured).
- Clean-build times, unless the build directory happened to be empty. A
  deliberate clean-build experiment is future work.
- `sumOfStepDurationsMs` is not CPU time: steps run in parallel and include
  waiting. Wall-clock build time is `native-build.durationMs`.
- Durations from one run are noisy; compare several runs on the same machine.
- Anything about the running app other than imported wells timings.

## UI Lab (UI Torture Lab v0)

```bash
npm run observatory:ui                              # build, then every scenario
npm run observatory:ui -- --scenario sidebar
npm run observatory:ui -- --scenario workspace --skip-build --label "after spring tweak"
```

Each scenario runs in a fresh Archivist window (it's visible; leave the
mouse alone while it runs) and gets its own run directory:

```
<observatory>/runs/<runId>/summary.json    kind "ui-scenario": status, stats, build, isolation
<observatory>/runs/<runId>/ui-events.json   every action, raw (what the app's driver wrote)
<observatory>/runs/<runId>/scenario.json    the exact definition that ran
<observatory>/runs/<runId>/runtime.log      the app's own output, wells timing included
```

Unless `--skip-build` is given it first runs the build capture and links
that build run (`build.links`); `build.sha256` identifies the binary that ran.

### Isolation

The runner makes a temporary profile directory and starts the app with
`ARCHIVIST_PROFILE_DIR` (database, QSettings as an INI file, unsaved-edit
recovery and themes all go inside it; see `frontend/src/Core/Support/app_profile.h`).
Before the database is opened the app checks those paths are inside the
profile and refuses to start otherwise (status `refused`, exit 3). The
driver records the same check in `app.isolation`. Your chats, libraries,
layouts, settings and open documents are never touched. Inherited
`ARCHIVIST_*` variables are dropped. The profile is deleted afterwards
(`--keep-profile` keeps it). No network or AI service is used.

Not isolated: macOS Keychain reads (an API key, only if something asks),
and LibreOffice preview caches in the user cache directory (not used by
these scenarios). A dev instance you already have open keeps running; the
lab instance takes another IDE port if 3334 is busy.

### How actions run and what is timed

The scenario runs inside the app (`frontend/qml/App/UiLab/UiLabDriver.qml`,
helped by `src/App/Services/ui_lab.cpp`), so timings come from the app's
own monotonic clock and the window's real frame swaps. Each action records
its `method`:

- `input`: synthetic mouse press/move/release sent to the window, going
  through Qt Quick's normal hit-testing and handlers (rail clicks, dragging
  the side panel's edge).
- `ui-function`: the QML function a control calls (minimize to the Gravity
  Well with its Genie, restore from the Well).
- `store-call`: the same store call a control makes (open a view, switch
  layout, activate a tab). These skip the control itself.

Per action, all in ms from dispatch:

| Field | Meaning |
| ----- | ------- |
| `dispatchMs` | The input or call returned. |
| `stateMs` | The step's expected UI state was first seen (checked about every millisecond). A state, not a frame. |
| `settledMs` | The last frame swapped before rendering went quiet for `settleQuietMs` (120 ms). `null` if nothing was drawn. `settleTimedOut` when something never stopped animating. |
| `frames` | Frames swapped during the action: `count`, `firstMs`, `lastMs`, `maxIntervalMs` (longest gap between two frames). Measured from `QQuickWindow::frameSwapped`, timed on the render thread. |

Summary stats (`stats.measured`, `stats.steps[]`) cover measured iterations
only; warm-up, setup and teardown are counted apart. p95 is `null` under 20
samples. There are no performance thresholds: only functional failures
(an expectation not met within `actionTimeoutMs`, a crash, a timeout) fail a
run. Statuses: `passed`, `failed` (exit 1), `refused` (3), `crashed` /
`error` (70), `timeout` (124), `interrupted` (130).

### Scenarios

Definitions live in `ui-scenarios/*.json`: `id`, `version` (bump it when the
workload changes), `initialConditions`, `warmupIterations`, `iterations`,
timeouts, and `setup` / `steps` / `teardown`, each step an `op`, a `label`
and the UI state it must `expect`. No randomness (`seed` is null).

- **sidebar** (v1): open/close the side panel from the rail 4 times, drag
  its edge to 260/420/560/310, visit all five rail sections, close. Input
  driven.
- **workspace** (v1): open four Preview views (Lab A-D) on files that don't
  exist, so they show their empty state; switch Code Review, Creative and
  Research layouts; activate tabs; minimize two views into the Gravity Well
  and restore them; verify all four are open. Layouts, tabs and opening are
  store calls; minimize/restore go through the workspace's own functions.

### What it can't tell you yet

- `settledMs` is when drawing stopped, not when pixels reached the screen
  (no vsync/compositor timing), and frame swaps are only seen in the one
  main window.
- Drags are sent as a press, 8 moves and a release in one go, not spread
  over frames like a hand.
- Tab clicks, the layout picker and Magnet drag-and-drop are not driven by
  input yet; view contents are empty-state Previews, not real documents.
- Wells timing lines have no timestamps, so they are attached to the run
  (`runtimeTiming`, same process), not to individual actions.
- The runner's own logic is tested with a stand-in app; the in-app driver is
  only exercised by real runs.

## Runtime timings (archivist.wells.timing)

The Workspace store already logs minimize, restore and publish timings when
`archivist.wells.timing` is enabled. To record them:

```bash
QT_LOGGING_RULES="archivist.wells.timing=true" npm run dev
# minimize and restore some panels, then quit
npm run observatory -- ingest-runtime backend/data/runtime/logs/frontend.log
npm run observatory -- ingest-runtime <log> --label wells --build-run <runId>
```

This makes a `runtime-log` run with an unchanged copy of the log
(`runtime.log`) and a summary listing each recognized event (`publish` with
its stage times, `minimize`, `restore`) with its line number, plus per-operation
count, mean and max. Only lines with the `archivist.wells.timing: ` prefix and
the exact shapes in `runtime-log.mjs` are read; other timing lines are listed
in `unrecognizedLines`. View titles are not stored. A runtime log is linked to
a build run only when you pass `--build-run`, recorded as
`relationship: "declared-by-user"`.

## Dashboard

```bash
npm run observatory:dashboard                 # opens http://127.0.0.1:4317/
npm run observatory:dashboard -- --port 4400 --no-open
```

A small local web page over the records above. It reads them and never
writes, renames or deletes anything. Press Ctrl+C to stop it.

- **Overview**: latest build, latest run of each UI scenario, build and UI
  trends, recent failures, the machine, and "since the previous comparable
  run" changes.
- **Builds**: phases per build, cached vs rebuilt IDE bundle, build kind,
  status, and each build's log.
- **UI performance**: settled and state latency (median, p95, max) per
  scenario version, per-step tables for the latest run, failures. This is
  action latency, not frame time; frame gaps are shown separately.
- **Compare**: any two runs as baseline and candidate. Shows Git, build
  configuration, machine and scenario for both, then every metric with the
  absolute and percentage change and better / worse / about the same
  (within 2%) / not measured in both. Runs of different kinds, different
  scenarios or different scenario versions are refused with the reason.
  Different hardware, OS, build type, compiler, Qt, build kind, IDE cache,
  dirty trees and failed runs are warned about. It makes no claim of
  statistical significance: each side is one run.
- **Runs**: every run, filterable by type, result, scenario, branch, commit,
  date and text; tick two to compare. A run's page has its metadata,
  phases or actions, and a searchable, paged preview of its log files with a
  link to the full file.

Runs that never finished show as `incomplete`, summaries that can't be parsed
as `unreadable`, and summaries from a newer `schemaVersion` are listed but not
interpreted. Missing measurements show as `—` and are never filled in.

Safety: the server listens on 127.0.0.1 only, answers GET only, rejects
requests whose Host isn't `127.0.0.1`/`localhost` (so another website can't
read it through DNS tricks), and serves only files a run's own summary lists
under `artifacts`, inside that run's folder (symlinks out are refused). Log
text is rendered as text, never as HTML, under a strict Content Security
Policy.

To preview the dashboard without real records, write the test fixtures to a
scratch folder (they are made-up runs labelled `fixture:` on a `fixture/main`
branch):

```bash
node scripts/observatory/test/fixtures/dashboard-runs.mjs /tmp/observatory-fixtures
ARCHIVIST_OBSERVATORY_DIR=/tmp/observatory-fixtures npm run observatory:dashboard
```

## Code

| File | Responsibility |
| ---- | -------------- |
| `cli.mjs` | Commands, assembling and saving the summary |
| `capture.mjs` | Running phases as subprocesses, timing, signals |
| `build-phases.mjs` | The Archivist build phases and build classification |
| `metadata.mjs` | Git, machine and toolchain facts |
| `ninja.mjs` | Ninja progress output and `.ninja_log` |
| `runtime-log.mjs` | Parsing `archivist.wells.timing` lines |
| `storage.mjs` | Record location, run ids, atomic writes |
| `report.mjs` | The build terminal summary |
| `ui-lab.mjs` | Starting the isolated app for a scenario, timeouts, classifying the run |
| `ui-stats.mjs` | Median, p95 and max over UI actions |
| `ui-report.mjs` | The UI Lab terminal summary |
| `ui-scenarios/` | Scenario definitions |
| `dashboard/server.mjs` | The dashboard's local, read-only server and JSON API |
| `dashboard/records.mjs` | Reading runs into one flat record shape |
| `dashboard/compare.mjs` | Baseline vs candidate: blockers, warnings, deltas (also used by the page) |
| `dashboard/filter.mjs` | Run filters (also used by the page) |
| `dashboard/public/` | The page: plain JavaScript, no dependencies |

## Testing the collector

```bash
npm run test:observatory
```

Uses Node's built-in test runner with small temporary subprocesses and a
stand-in app (`test/fixtures/fake-app.mjs`); it needs no Qt build. The
dashboard tests use the made-up runs in `test/fixtures/dashboard-runs.mjs`.
