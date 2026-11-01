# Observatory

Observatory keeps a local, structured record of Archivist development builds,
so a change's effect on build time can be compared run to run. This is the
pieces so far: build capture, the UI Lab (scripted UI scenarios timed in a
real, isolated app instance), and an import of the existing
`archivist.wells.timing` runtime logs, plus a local dashboard to browse and
compare the records, baselines that flag regressions, and controls that
let people (the Record page) and agents (`--json`) run tests and hand the
results to an AI.

**Architecture in one paragraph.** Every record is a folder under
`<Observatory folder>/runs/<time>-<kind>-<hex>/` with a `summary.json` and
the files it lists; there is no database or index, and nothing is uploaded.
The Node CLI (`cli.mjs`) writes build, UI test and runtime-log runs, and
holds every control operation (`control/operations.mjs`). The app writes live
recordings itself (`frontend/src/App/Services/live_recorder.*`) and serves the
dashboard page from its loopback IDE host at `/observatory/`; for test
controls it runs `node cli.mjs control <operation>` on two worker threads of
its own. The same page also runs in a browser from `dashboard/server.mjs`.
Analysis code under `dashboard/public/*.mjs` is shared by the page and the
CLI, so both read a run the same way.

## When to use Observatory

- **A build feels slower:** `npm run observatory:build` a few times before and
  after, then `baseline compare`. It says regression, improvement, no
  meaningful change, or not enough evidence.
- **A scripted UI interaction got slower (sidebar, wells, tabs):** run the UI
  Lab scenario three or more rounds and compare with its baseline.
- **Something felt janky while using the app:** make a live recording of the
  same interaction, then `live spikes` and `live compare` with a recording
  made before the change. One recording each is a hint, not proof.
- **Handing a problem to an agent:** Copy for AI or Export evidence on the run.
- **Not for:** profiling (no stack samples: it says *when* the UI thread was
  busy, not *what* ran), GPU or compositor timing, production telemetry,
  automatic diagnosis, or comparing a debug build with a release build.

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
| `configure`    | `scripts/qt-configure --if-needed`        | compatible cache reuse vs actual configure |
| `build-plan`   | `cmake --build build/frontend-debug -- -n`| Ninja dry run: steps the build will run  |

**Bookkeeping steps.** CMake re-checks its source globs, and Qt's AUTOMOC
refreshes its timestamps and metatype lists, on every build, even when
nothing changed. Observatory counts these apart from real work
(`plannedBookkeepingSteps`, `bookkeepingSteps`), so a build that runs only
them is `no-change`. The patterns are in `ninja.mjs`.
| `native-build` | `cmake --build build/frontend-debug`      | time; Ninja progress; `.ninja_log` edges |

`scripts/qt-build` checks for an existing compatible Ninja graph before `cmake --build`, so the last two
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

### Fast everyday Qt builds

The standard development path now checks whether `build/frontend-debug` has an
existing Ninja **Debug** graph for the current `frontend` source directory.
If so, `scripts/qt-build` skips the redundant explicit CMake configure
command. Ninja still verifies the generated graph and automatically
regenerates CMake when tracked build inputs change (including
`CMakeLists.txt` and the configured resource globs). A fresh, incompatible
or missing graph still gets an explicit CMake configure.

For a fast day-to-day build including cached IDE web assets use
`npm run build:fast`. `npm run dev` and `npm run dev:detached` also
use the optimized Qt build automatically. For an intentional manual
reconfiguration (new Qt install, toolchain, CMake settings or other cache
variables), use `npm run qt:configure` before the next build.
`npm run observatory:build` runs the same conditional configuration
phase and records `explicitConfigureSkipped`. A skipped *explicit*
configuration does not mean Ninja skipped checking its CMake dependencies.

No production compiler flags, Qt caching settings, native build graph,
or release packaging are changed by this optimization.

## What is measured and what is inferred

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

### Build evidence: every job, and why it rebuilt

Each build also writes `build-evidence.json` (every job, up to 20,000) and
a compact `evidence` block in `summary.json`. All of it comes from Ninja's
own records and tools; nothing is guessed:

| Field | Source | What it says |
| ----- | ------ | ------------ |
| `jobs` in `build-evidence.json` | `.ninja_log` (this build's lines) | Each job's outputs, start and end (Ninja's clock, ms from Ninja's start), duration, `category` (compile, link, archive, autogen, qml, resources, cmake, other, from the Ninja rule) and `source` file (`ninja -t compdb`) |
| `evidence.timing` | the jobs | `wallMs` (first start to last end), `summedJobMs` (parallel work added up: **not** wall time), peak and average parallel jobs, the longest job |
| `evidence.criticalPath` | `ninja -t query` | The longest chain of jobs that ran where each depends on the previous one. **An approximation**: it ignores time a job waited for a free slot and doesn't follow edges through jobs that didn't run |
| `evidence.rebuild` | `ninja -d explain -n` (the plan phase, before the build) | Why Ninja rebuilt: `causes` are changed inputs Ninja named (header, source, generated, cmake), each with how many outputs and compiles it triggered; an input that was itself rebuilt is a link in a chain, not a cause. `propagated` counts outputs that were dirty only because an input of theirs was rebuilt. `commandLineChanged` and `missingOutputs` are listed apart |
| `cause.includedBy` | `ninja -t deps` | How many objects include a changed header, by the compiler's own dependency records |
| `evidence.headers.mostIncluded` | `ninja -t deps` | The repository headers included by the most objects (fan-out) |
| `evidence.compilerCache` | ccache `--print-stats` / sccache `--show-stats` before and after | Hits, misses and uncacheable compiles **observed during this build**, only when the build is set up with a launcher; `configured: false` otherwise |
| `evidence.notes` | | What couldn't be established and why |

`build-evidence.json` has `schema: "archivist.observatory.build-evidence"`,
`schemaVersion: 1`.

**Fast targeted compiler profiling (opt-in):** `npm run
observatory:cpp-profile` profiles `main.cpp`, `ai_tools.cpp`,
`chat_store.cpp`, `ai_runtime.cpp` and `library_store.cpp` in sequence.
You can select individual files with `npm run observatory:cpp-profile --
ai_tools.cpp`. It reads Ninja's current compilation database and reuses
the already-configured Debug compiler flags and PCH without configuring a
second build. Clang writes the temporary object, depfile and
`-ftime-trace` JSON **outside** the active build directory; the script
does not modify existing native objects, linked executables or source files.
It prints ranked headers and templates for each translation unit and saves
`build/frontend-debug/cpp-profile-report.json`. This is only for a
configured and already-built Clang Debug graph; run
`npm run build:fast` beforehand if the graph or PCH does not exist.
Profiling executes compiler processes and uses extra CPU/memory, but it
does not perform a full Ninja rebuild. Measurements from the compiler trace
are diagnostic (and header timings inclusive), not performance baselines.

**Full compiler profiling (opt-in):** `npm run observatory -- build
--time-trace` builds in its own directory (`build/frontend-time-trace`,
configured like `scripts/qt-configure` plus Clang's `-ftime-trace`), so the
normal build's flags never change. The first run is a full build there. It
writes `time-trace.json` (`archivist.observatory.time-trace` v1): frontend,
backend and template-instantiation totals, and the headers and templates
that cost the most across the compiles that ran. Clang only records events
over 500 µs, and header and template times are inclusive (a header's time
contains what it includes), so they rank, they don't add up. Each compile
also gets slower and writes a JSON file next to its object. It needs Clang
(Apple's Clang on macOS); other compilers are refused. Profiled builds are
never compared with normal ones.

### What it cannot tell you yet

- Compiler-cache numbers when no launcher is configured (Archivist doesn't
  set one by default).
- Clean-build times, unless the build directory happened to be empty. A
  deliberate clean-build experiment is future work.
- `sumOfStepDurationsMs` and `summedJobMs` are not CPU time: jobs run in
  parallel and include waiting. Wall-clock build time is
  `native-build.durationMs`.
- The critical path is an approximation (above), and why a CMake
  regeneration or AUTOGEN step ran is only as specific as Ninja's explain
  output (often "output older than most recent input").
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

- **chat** (v1): seeds two deterministic conversations into the test
  profile's database (`UiLab.seedChats`, seed 7: "Lab Big" with 600
  messages, "Lab Small" with 20; long paragraphs, Markdown lists and code
  blocks from `UiLab::fixtureMessage`), opens a chat view, then switches
  between them. `chatShown` waits until the conversation is selected and its
  first page (160 messages) is in the store; `settledMs` then covers the
  rendering that follows. Store calls, as the side panel makes them.
- **ai-pipeline** (v1): seeds a Library of 80 synthetic notes (indexed, so
  retrieval has something to search) and a short and a long conversation,
  then sends chat messages in each against the local mock provider
  ("fast": it answers at once). It measures Archivist's own side of an AI
  turn: preparation, retrieval, context compilation, database work, saving
  and showing the reply. No API calls.
- **ai-turn** (v1): one scripted full turn against the mock: a refused
  request (503) and its retry, a tool call, a slow stream with a long pause
  and reported usage, and a long reply that the scenario cancels part way.
  No API calls.
- **ai-provider** (v1, opt-in): three short requests to the real provider
  (synthetic prompts), through a local proxy that refuses after 6 requests
  and caps output at 64 tokens. Only runs with `--allow-provider-calls`; it is
  in no preset but `provider`, and `tests run --scenario all` skips it.
- **startup** (v1): a fresh profile started until the workspace is ready
  (`readyMs`), with the stages main() reports on the operations timeline.
  The operating system's file cache is not flushed, so this is a fresh
  profile, not a first-launch-after-boot cold start.

### Operations inside each action

The app reports its own work through `LiveEvents` (the same spans and
markers the live recorder sees). In a UI Lab instance, `UiLab` collects
them from the start, on the same clock the driver times actions with
(`LiveEvents::nowNs`), and tags each with the action that was running when
it began. It also runs an event-loop probe (every 50 ms; 50 ms or more late
is a stall). The live recorder refuses to start in a UI Lab instance, so
the two never compete for the stream.

The runner turns that into `ui-operations.json`
(`archivist.observatory.ui-operations` v2) and `summary.operations`:

- **per action:** the spans nested by time (a span inside another is shown
  as its child; spans from other threads can nest this way without one
  calling the other, so the tree means "during", not "called by"), each
  with `selfMs` (its time minus the union of its children), the stalls
  during it, markers, and its time split up (below), plus spans that began
  in it and ended after it (`outlived`);
- **per step:** each operation's median and max over measured iterations,
  and the median response window and unattributed time, from the same
  actions;
- **startup:** `qt-init`, `profile`, `core-runtime` (database and
  migrations), `ai-runtime`, `ide-host`, `qml-load`, entering the event
  loop, the first frame after the lab attached, and `readyMs`, all in ms
  since main() was entered. Time before main() isn't measured. `phases`
  splits the same time without overlap: native initialization, QML load
  (synchronous; `qmlLoad` splits it at the `qml-root-created` marker),
  event loop to first frame (work QML deferred past the event loop), first
  frame to usable UI, background work after ready, rendering settling, and
  the intentional idle observation. A phase with no evidence is `null`.
- **trace:** AI turns and database jobs, built from span ids (see
  [Traces](#traces-ai-turns-and-database-jobs)).

#### An action's time, split up (v2)

Every time is measured from the action's start:

| Field | Ends when |
| --- | --- |
| `timings.dispatchMs` | the call or input returned |
| `timings.stateMs` | the expected UI state was first seen |
| `timings.lastFrameMs` | the last frame swapped before rendering went quiet |
| `timings.settledMs` | max(state, last frame): the response is complete |
| `timings.renderSettleMs` | settled minus state: rendering catching up |
| `timings.quietWaitMs` | elapsed minus settled: the driver watching for quiet |
| `timings.elapsedMs` | the action's whole window (`durationMs`) |

The **response window** (`window`) is input to settled (`basis:
"settled"`), to state when nothing was drawn, or the whole elapsed time for
a failed or never-quiet action. `coveredMs` is the union of span time inside
the window and `unattributedMs` the rest, so `coveredMs + unattributedMs =
window.ms` exactly and unattributed time can never exceed its window. Span
time after the window is `afterWindowMs` (before it, `beforeWindowMs`),
never clipped away silently.

v1 measured unattributed time against the whole elapsed window (which
holds the 120–300 ms quiet wait) while audits compared it with settled
time, which is how "2.33 s unexplained of 2.01 s" could happen. Audits
(rules version 2) only use v2 numbers.

Steps whose op is `noop` (the idle after startup) are `kind:
"observation"`: they watch the app and are never counted as slow
interactions.

Instrumented today: startup stages; chat (`open-conversation` from the
selection to the first page handed to QML, with `load-message-page` on the
database thread, `map-messages` and `publish-messages`); AI turns, database
jobs, retrieval and context compilation (see Traces); the workspace, wells,
sidebar and shell events the recorder already had. Explorer and
Monaco/WebEngine are not instrumented yet.

### Traces: AI turns and database jobs

`LiveEvents` spans carry an `id`, a `parent` and the `thread` they were made
on (`main`, `ArchivistAI`, `ArchivistCoreWriter`, `pool`). A `Scope` or
`Step` is the parent of what its thread does while it is open; work handed
to another thread names its parent (`ParentScope`, or the id). So a trace
nests by cause, across threads. A child may outlive its parent (work it
started); that is reported as `outlivedMs`. `trace.mjs`
(`archivist.observatory.trace` v1) reads any events with ids, from the UI
Lab or the live recorder, into trees and into:

**An AI turn** (`ai.run`, root `ai.start-chat-run`):

```
ai.start-chat-run            the call from the UI thread
├─ ai.queue-wait             posted until the AI thread ran it
├─ ai.run                    runId, provider, model, outcome, rounds, tool calls,
│  │                         run-event writes (count, total, max), provider-reported tokens
│  ├─ ai.model-catalog       only when the model list is refreshed
│  ├─ ai.prepare
│  │  ├─ ai.conversation-load        messages
│  │  ├─ ai.workspace-describe       when a workspace is attached
│  │  ├─ ai.attachments              sources, warnings
│  │  ├─ ai.retrieval                sources, files, skipped
│  │  │  ├─ retrieval.library-search
│  │  │  │  ├─ retrieval.fts-query   rows (retrieval.index-repair if it rebuilt)
│  │  │  │  └─ retrieval.rank        candidates
│  │  │  └─ retrieval.select         included, omitted, tokens
│  │  └─ ai.context-compile          compiler, included, omitted, estimatedTokens
│  │     └─ context.strategy
│  │        ├─ context.chat-search   candidates (auto-balanced compilers)
│  │        └─ context.assemble
│  ├─ ai.provider-round      round, endpoint, attempts, events, deltas, bytes, longest gap,
│  │  │                      gaps of 1 s or more, tool calls, tokens
│  │  ├─ ai.request-serialize        bytes
│  │  ├─ ai.http-attempt             status, bytes sent/received, error (timeout,
│  │  │  │                           connection-refused, http-status, ...), retry, cancelled
│  │  │  └─ ai.first-byte (marker)
│  │  ├─ ai.retry-wait               waitMs
│  │  └─ ai.first-delta (marker)
│  ├─ ai.tool-round          round, calls, resultBytes
│  │  └─ ai.tool             tool, callId, round, argument/result bytes, serializeMs, errorCode
│  └─ ai.persist
└─ ai.callback-delay         result posted until the UI thread ran the callback
chat.ai-first-delta-received, chat.ai-first-text-shown (markers), chat.ai-presentation
                             on the UI thread, matched by runId: flushes and their time
```

`aiTurns()` reports, per turn: queue wait, preparation and its stages,
each round with its attempts (status, retry, error), retry waits, first
byte and first delta from the round's start, stream statistics, tools,
persistence, and three different firsts from the run's start: first network
byte, first model delta and first text shown. Tokens are only what the
provider reported; no price is computed. `unattributedMs` is the run's time
no direct child covers, and `missing` names expected stages the trace
doesn't have (an uninstrumented gap is never shown as zero).

**Database jobs** (`db.write`, `db.read`, `db.run`): `op` is the name the
caller passed or its class name, never SQL or parameters; `queue-wait`,
`execute` (on the thread that ran it) and `callback-delay` are children, and
`rows` is set when the result is a list. `CoreRuntime::read()` runs on the
calling thread, so an `execute` on `main` is the UI thread blocked;
`dbJobs()` totals those as `uiThreadSync`. The AI thread's own database
connection (run events, chat messages) is measured as the run's event
writes and `ai.persist`, not as `db.*` jobs.

What is never in a trace: API keys, headers, prompts, model output, file
names or paths, tool arguments or results (only their sizes), SQL or its
parameters. Each event holds at most 12 short scalar fields; there is no
per-token event (the stream is counted, not logged). DNS, TCP and TLS times
are not reported: Qt doesn't expose them, and they are not guessed.

### What it can't tell you yet

- `settledMs` is when drawing stopped, not when pixels reached the screen
  (no vsync/compositor timing), and frame swaps are only seen in the one
  main window.
- Drags are sent as a press, 8 moves and a release in one go, not spread
  over frames like a hand.
- Tab clicks, the layout picker and Magnet drag-and-drop are not driven by
  input yet; workspace view contents are empty-state Previews, not real
  documents. There are no Large Library or IDE (Monaco) scenarios yet, and
  chat scrolling and streaming replies aren't exercised.
- Operations are only what the app reports through `LiveEvents`: time in
  QML layout or painting shows up as unattributed or as frames, not as named
  work.
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

**Inside Archivist:** open a panel's menu and pick **Observatory Here**, or
press **Ctrl+Alt+Shift+O** (⌘⌥⇧O on a Mac). The dashboard opens as a panel
you can move, split and minimize like any other, and **Refresh** reads the
runs again. The app serves it from its own loopback host at
`/observatory/`; nothing extra runs.

**In a browser**, without starting Archivist:

```bash
npm run observatory:dashboard                 # opens http://127.0.0.1:4317/
npm run observatory:dashboard -- --port 4400 --no-open
```

Both show the same page over the records above. They read them and never
write, rename or delete anything. Press Ctrl+C to stop the browser version.

The page has three places, picked from the switch in the top bar:

- **Record**: a big **Record** button with the length (15, 30, 60 or 120 s),
  an optional name, and the latest runs beside it. **Run a scripted UI
  test** folds open underneath for the repeatable UI scenarios.
- **History**: every run as one list, with a chip per kind (recordings,
  builds, UI tests, runtime logs), a search box (name, branch, commit) and
  trend charts for the kind shown. Tick two runs of the same kind and press
  **Compare**.
- **Compare**: pick a **Before** and an **After** (or swap them). Both runs'
  Git, build and machine details sit side by side, then a count of better /
  worse / about the same (within 2%) and paired bars for each measure, with
  the change. Recordings, builds and UI tests compare with their own kind;
  different kinds, scenarios, scenario versions, or recordings with a
  different measurement version or sampling interval are refused with the
  reason. Different hardware, OS, build type, compiler, Qt, build kind, IDE
  cache, dirty trees and failed runs are warned about. Each side is one run,
  so no claim of statistical significance is made. **Regression check**
  folds open at the bottom (see Baselines).

Every run has its own page: tiles with the headline numbers, the timeline
for a recording, and folds for phases, steps, details and a searchable,
paged preview of each log. Its action bar has **Compare with previous**,
**Compare with…**, **Copy for AI** and **Export evidence**.

The layout follows the panel's shape: side by side when wide, one column when
narrow, a tighter header when short, and icons only in the switch below
about 440 px. Old links (`#/builds`, `#/runs`, `#/baselines`, `#/live`,
`#/lab`, …) still open the matching place.

Runs that never finished show as `incomplete`, summaries that can't be parsed
as `unreadable`, and summaries from a newer `schemaVersion` are listed but not
interpreted. Missing measurements show as `—` and are never filled in.

How it's put together: the page (`dashboard/public/`) does all the reading,
comparing and filtering itself. A host answers three read-only requests, and
Archivist's IDE host (`frontend/src/Core/Domains/Observatory`) answers them
exactly like the Node server (`dashboard/server.mjs`):

| Request | Answer |
| ------- | ------ |
| `GET api/runs` | `{ root, runs: [{ runId, summary, error }] }`, newest first; `summary` is `summary.json` as stored, or `null` with the reason in `error` |
| `GET api/runs/<runId>/artifacts/<name>` | The text of an artifact the run's summary lists under `artifacts` (up to 32 MB) |
| `GET api/baselines` | `{ baselines, baselinesError, budgets, budgetsSource, budgetsError }`: `baselines.json` and the budgets in effect, as stored |
| `POST api/control/<operation>` | Only with controls on (see Controls below): a control operation's response envelope |

Safety, in both hosts: they listen on 127.0.0.1 only, answer GET only (apart from the guarded controls), reject
requests whose Host isn't `127.0.0.1`/`localhost` (so another website can't
read them through DNS tricks), serve only the page's own listed files, and
serve only artifacts a run's own summary lists, inside that run's folder
(symlinks out are refused). Log text is rendered as text, never as HTML,
under a strict Content Security Policy. In the app, the panel can't navigate
away from the dashboard, and "Open full …" links open in your browser.

Release builds without repository paths (`ARCHIVIST_USE_REPOSITORY_PATHS=OFF`)
need the page copied to `Archivist.app/Contents/Resources/observatory`, or
`ARCHIVIST_OBSERVATORY_PAGE` pointing at it.

To preview the dashboard without real records, write the test fixtures to a
scratch folder (they are made-up runs labelled `fixture:` on a `fixture/main`
branch):

```bash
node scripts/observatory/test/fixtures/dashboard-runs.mjs /tmp/observatory-fixtures
ARCHIVIST_OBSERVATORY_DIR=/tmp/observatory-fixtures npm run observatory:dashboard
```

## Baselines and regressions

A baseline is a group of successful runs you choose as "how fast this was".
Later runs of the same kind are compared with it, and each metric gets a
verdict. Nothing is ever promoted to a baseline automatically.

```bash
# Pick runs (ids are in the dashboard or runs/). Three or more is best.
npm run observatory -- baseline set <run-id> <run-id> <run-id> --note "main before the sidebar rewrite"
npm run observatory -- baseline list
npm run observatory -- baseline compare <run-id> [<run-id> ...] [--json]
npm run observatory -- baseline remove <category>

# Collect UI runs (3 rounds by default), then compare every baseline.
npm run observatory:check
npm run observatory:check -- --no-collect          # compare only
npm run observatory:check -- --scenario sidebar --repeat 5
npm run observatory:check -- --strict               # exit 5 on a confirmed regression
```

`observatory:build` and `observatory:ui` also compare automatically when a
baseline exists for what they recorded (`--no-compare` turns that off). That
comparison is informational and never changes the command's exit code.
`check` collects UI runs only; build categories are compared from builds you
recorded with `observatory:build`.

**Categories.** Each baseline covers one kind of workload, so a clean build
is never compared with an incremental one:
`build:clean`, `build:incremental`, `build:no-change` (from the build's
classification) and `ui:sidebar`, `ui:workspace`.

**Where it lives.** `<Observatory folder>/baselines.json` holds one entry
per category: its run ids, the commit, branch, machine, build configuration
and scenario version they came from, the metric definitions version, and
your note. Replacing or removing a baseline moves the old entry to the
file's `history`; runs themselves are never changed or deleted. A damaged
or newer-version file is reported and left untouched. Every comparison is
saved to `<Observatory folder>/reports/<id>/report.json` (machine-readable,
schema `archivist.observatory.regression-report` v1) and `report.txt`.

**Budgets.** `scripts/observatory/budgets.json` sets, per category and
metric: which direction is better, the percent and absolute change that
counts (a change must pass both), the minimum runs on each side, which
environment fields must match (`compatibility`), and what a missing metric
means (`missingData`: `insufficient-evidence` or `ignore`). Copy it to
`<Observatory folder>/budgets.json` to use your own, or pass
`--budgets <file>`. Unknown categories, metrics or fields are reported, not
silently ignored. The file is versioned (`schemaVersion`).

Metric keys: `totalMs`, `phase.ide-bundle`, `phase.configure`,
`phase.build-plan`, `phase.native-build` (builds); `settled.median`,
`settled.p95`, `state.median`, `frameGap.median`, `appDurationMs` (UI), plus
`steps` for each UI scenario step's settled median.

**How a verdict is reached.** For each metric, the median of the baseline
runs is compared with the median of the newest compatible successful runs
since the baseline.

| Verdict | When |
| ------- | ---- |
| Regression / Improvement | at least `minSamples` runs on each side, the change passes both budget thresholds, and every candidate run is beyond every baseline run (no overlap) |
| No meaningful change | the change is within the budget |
| Needs more runs (insufficient evidence) | too few runs, the metric wasn't measured, or past the budget but the groups overlap ("over budget, unconfirmed") |
| Incompatible | the machine, build type, scenario/driver version or metric definitions differ from the baseline's |

This is a rule of thumb for noisy local timings, not a statistical
significance test, and the reports say so. Failed runs never count as
performance data; they are listed separately as functional failures.

**Exit codes** (`baseline compare`, `check`): 0 ok (including regressions
and inconclusive results, unless `--strict`), 1 a functional failure or an
error, 2 usage, 5 a confirmed regression under `--strict`.

The **Regression check** on the dashboard's Compare page shows each category's baseline, what it
was compared with, every metric's verdict, slowest-step changes and a trend
chart with the baseline runs marked. It is read-only: baselines are set from
the command line.

## Controls: UI tests, agents, Copy for AI

People and agents drive the same controller (`control/operations.mjs`):
people through the dashboard's **Record** page and its buttons, agents through
the CLI with `--json`. Both start the same jobs, read the same records and
get the same reports.

### For people

- **In Archivist:** open the Observatory panel and open **Run a scripted UI
  test** on the Record page. Its controls are on already.
- **In a browser:** run `npm run observatory:dashboard -- --control` and open
  the link it prints. Without `--control` the dashboard stays read-only.

From there you can:

- Pick All, Sidebar Stress or Workspace Stress, set the number of rounds
  (1–10), and choose whether to build first. Building is off by default, so
  the binary already in `build/frontend-debug` is tested. The page shows
  that binary, when it was built, and the branch and commit, and it warns
  when the working tree is dirty.
- Press **Run test**, then watch the progress: the scenario and round, the
  phase, actions done out of planned (read from the app's own records, not
  estimated), elapsed time, the last action and any error. **Cancel** stops
  the test.
- Read the results when the test finishes: status per run, settled latency,
  p95, frame gaps, the slowest steps, the baseline verdict, and links to
  each run.

**Copy for AI** puts a short Markdown report on the clipboard. It is on
every run page, on UI test results, and on every category of the
Regression check. **Export evidence** writes a redacted folder and `.zip` for an agent
that can't read your Observatory folder.

### For agents

Every command below accepts `--json`. With it, stdout carries exactly one
JSON document and nothing else, and progress text goes to stderr.

```bash
npm run observatory -- capabilities --json          # operations, inputs, side effects, error codes, the test binary
npm run observatory -- scenarios list --json
npm run observatory -- tests run --scenario sidebar --repeat 3 --json     # returns at once with a job id
npm run observatory -- tests status <job-id> --json # progress, then results + baseline comparison
npm run observatory -- tests cancel <job-id> --json
npm run observatory -- tests run --scenario sidebar --wait --json         # or block until it finishes
npm run observatory -- ui --scenario sidebar --skip-build --json          # the plain runner, in the foreground
npm run observatory -- runs list --kind ui-scenario --limit 10 --json
npm run observatory -- show <run-id> --json         # summary, compact record, artifact list
npm run observatory -- runs artifact <run-id> log --tail 200 --json       # redacted unless --raw
npm run observatory -- runs compare <run-id> ... --json
npm run observatory -- report <run-id>              # the Copy for AI Markdown (--json adds the facts)
npm run observatory -- export <run-id> [--raw-logs] --json
echo '{"scenario":"sidebar"}' | node scripts/observatory/cli.mjs control observatory.tests.run
```

Every response uses the same envelope:
`{ schema: "archivist.observatory.response", schemaVersion: 1, operation, ok, result }`.
A failure is `{ ..., ok: false, error: { code, message, details? } }`. The
error codes are `invalid-input`, `unknown-operation`, `not-found`,
`unreadable-record`, `busy`, `not-running`, `no-binary`,
`controls-disabled`, `unauthorized`, `forbidden-origin` and `internal`.

Exit codes:

- 0: ok.
- 1: failed.
- 2: invalid input.
- 4: another UI test is running.
- `tests run --wait`: 0 completed, 1 failed, 70 interrupted, 124 timed
  out, 130 cancelled.

Other record formats:

- Jobs: `archivist.observatory.job` v1, in `<root>/jobs/<job-id>/job.json`.
- Reports: `archivist.observatory.diagnostic` v1.
- Exports: `archivist.observatory.evidence` v1.

### Jobs

```
queued → starting → running → completed | failed | cancelled | timed-out
(any active state) → interrupted     when the job's process is gone
```

- **How a job runs.** A test runs as a background job: one process
  (`cli.mjs job-run <job-id>`) runs the scenarios through the ordinary UI
  Lab runner, so every run keeps the disposable profile and isolation
  checks. The job writes its state to `job.json`.
- **One at a time.** Only one UI test runs at a time, whether it was
  started by a job, by `observatory:ui` or by `check`
  (`<root>/jobs/ui-lock.json`). A second request gets `busy`.
- **Cancelling.** Cancel first checks that the process is that job's
  runner (by its command line, not just the pid), then sends it SIGINT. The
  runner stops only the app it started, the run is recorded as
  `interrupted`, and the job as `cancelled`.
- **Lost jobs.** A job whose process disappeared (killed, sleep, restart)
  shows as `interrupted`, never as running or passed, and its lock is
  freed. Closing the dashboard doesn't stop a job. Jobs don't survive a
  restart.

### Reports and evidence

The **diagnostic report** (`dashboard/public/diagnostic.mjs`, shared by the
page and the CLI) contains:

- **Identity:** commit, dirty tree, binary and hash, build config, machine.
- **Experiment:** scenario, version, plan and initial conditions.
- **Measured results.**
- **Baseline comparison.**
- **Evidence:** the slowest steps and actions, errors, error lines from the
  log, and runtime timing events.
- **Observed together:** slow actions that coincided with long frame gaps,
  labelled as correlations, not causes.
- **Hypotheses:** only how the scenario drives the failed or slowest steps,
  marked as not verified.
- **Missing evidence**, **reproduce commands** and **artifacts**.

It never names a cause, and it is about 3 KB.

An **evidence export** goes to `<root>/exports/<export-id>/` plus a `.zip`
of the same files:

- `report.md` and `diagnostic.json`
- a redacted `summary.json` and `ui-events.json`
- `scenario.json`
- `comparison.json`, when there is a baseline
- `log-excerpt.txt`, the redacted error lines and the last 20 lines
- `manifest.json`, listing every file with its role, schema, size and
  sha256, and which files to read first

Full logs are added under `raw/` only with `--raw-logs` (or the checkbox).
They are marked `redacted: false, sensitive: true`.

**Redaction** (`dashboard/public/redact.mjs`) applies by default:

| Found in the text | Becomes |
| --- | --- |
| The Observatory folder | `<observatory>` |
| The Archivist checkout | `<repo>/`, with paths inside it kept |
| Any other path in a home folder | `~/<path>` |
| E-mail addresses | `<email>` |
| Values after `token`, `password`, `secret`, `api key`, `authorization` or `cookie` | `<redacted>` |
| Known key formats and other long opaque strings | `<redacted>` |

Commits and SHA-256 hashes are kept. Nothing is uploaded anywhere.

Redaction is pattern matching, not a guarantee. It misses a secret in a
format it doesn't know (a short password with no `password=` before it, a
key in an unusual form) and private text that isn't a path, a name in a
path, or an e-mail address. Read a report or export before sharing it. The
manifest says so (`redaction.limits`). Raw logs are left out unless you ask
for them (`--raw-logs`, or the checkbox, which asks again).

### How starting tests is protected

- **Read-only by default.** The browser dashboard stays read-only unless
  it's started with `--control`.
- **Requirements for every control request,** in both hosts:
  - It is a `POST` with `Content-Type: application/json`. A plain form
    can't send that.
  - Its `Origin` is the dashboard's own.
  - `Sec-Fetch-Site`, when sent, is `same-origin`.
  - `X-Observatory-Token` carries this session's random key.
  
  A custom header forces a CORS preflight, which is never granted, so
  another site can't even send the request.
- **Where the key comes from.**
  - Browser: it is in the start-up link's fragment, which never reaches a
    server and is removed from the address bar at once.
  - In the app: only the Observatory panel gets it, from QML.
- **Earlier protections stay.** The Host check, the loopback binding and
  read-only `GET` for records all stay as they were.
- **Nothing in a request names a program, path or shell command.**
  - Operations are a fixed list.
  - Inputs are validated: scenario ids against the scenario folder,
    rounds from 1 to 10, run and job ids by pattern, and unknown fields
    refused.
  - A test always runs this repository's scenarios against
    `build/frontend-debug`.
  - (`ARCHIVIST_OBSERVATORY_APP`, read only from the environment of whoever
    starts the controller, points tests at the stand-in app.)
- **The in-app panel uses the same Node controller.** The panel posts to
  Archivist's IDE host. The host checks the request
  (`frontend/src/Core/Domains/Observatory/observatory_control.*`) and runs
  `node scripts/observatory/cli.mjs control <operation>` with the input on
  stdin. That needs a development build (repository paths) and `node`.
- **Nothing starts on its own.** Opening a page never starts a test; only
  pressing Run does.
- **Your Archivist is left alone.** Tests run a separate app with a
  disposable profile. The Archivist you're using is never signalled or
  restarted. "Build first" rebuilds `build/frontend-debug`, after a
  confirmation.

### What it can't do yet

- **Jobs are tied to their own process.** Nothing restarts a job after a
  reboot. Keeper doesn't exist yet.
- **Progress is reported per action,** taken from the app's own record.
  There is no progress inside a build or during app start-up, and no
  percentage is invented.
- **No downloads in the app.** In the in-app panel, Export shows the
  `.zip` path (with a Copy path button) instead of downloading it.
- **Build tests stay on the command line.** The dashboard runs UI
  scenarios; builds are still recorded with `npm run observatory:build`.
- **Slightly different redaction in the page.** The page doesn't know where
  the checkout is, so its reports hide repository paths under your home
  folder too. The CLI keeps them as `<repo>/…`.

## Audits: run, find, hand to an AI, rerun

An audit is one press that runs a preset through the same job runner as
the scripted UI tests, then turns the runs it made into **findings**: what
is slow or broken, the rule that says so, the measured numbers and run ids
behind it, and what to look at next. Record > **Run an audit** on the page,
or `npm run observatory -- audit run --preset quick --wait` (add `--json`).

| Preset | Runs | Use it for |
| ------ | ---- | ---------- |
| `smoke` | startup, sidebar, workspace, AI pipeline, 1 short round | Fast local check without a build: two measured actions per step, no warm-up. Not a regression test. |\n| `quick` | startup + workspace, 1 round | A first look. One round never calls a regression. |
| `ui` (Full UI) | startup, sidebar, workspace, chat, 3 rounds | The UI loop: enough runs to compare two audits. |
| `build` | one incremental build of `build/frontend-debug` | Why it rebuilt and where the build time goes. |
| `ai` (AI Pipeline Audit) | ai-pipeline, 3 rounds, local mock provider | Archivist's side of an AI turn. Offline, no API calls. |
| `ai-turn` (Full AI Turn Audit) | ai-turn, 3 rounds, local mock provider | Every stage of a turn, retries, tools, a cancel. No API calls. |
| `provider` (Provider Latency Audit) | ai-provider, 1 round, real provider | Provider latency. Refused without `--allow-provider-calls`; the page asks first. |
| `complete` | `build`, then `ui` and the offline AI scenarios against what it built | Everything that makes no API calls. |

**macOS background UI Lab.** By default the Observatory runner launches its
disposable-profile test instances as non-focusing accessory windows using
a test-only AppKit/Qt window policy. It requests that macOS keep the test
window in the active Space without switching you to another desktop.
The test window is layered below ordinary application windows, near desktop
level, so it should not cover your editor or other normal windows. It remains
visible on the desktop when uncovered, and you can move or minimize it
normally if needed; synthetic UI actions do not require window focus.
The window is not hidden or minimized by default because macOS may suspend
scene-graph rendering for fully obscured windows. **Background frame/settling
results are not a substitute for foreground graphics benchmarks**; missing
frames must be reported as missing rather than interpreted as speed.
Background and foreground
runs receive distinct scenario versions so they are not silently compared.
If a foreground, frame-accurate run is needed, opt out for that command with
`ARCHIVIST_UI_LAB_FOREGROUND=1 npm run observatory -- audit run --preset ui --wait`.
Do not use `QT_QPA_PLATFORM=offscreen` for normal graphics profiling.

**Fast local feedback.** Use
`npm run observatory -- audit run --preset smoke --wait` to skip the
build and run one short round of startup, sidebar, workspace and offline
AI pipeline. Its scenario versions differ from full runs because it
disables warm-up iterations, caps repetitions, limits quiet waits to 70 ms and caps waiting for continuous animations to 900 ms after the expected state is reached. State correctness still uses the normal action timeout, and a never-quiet scene reports missing settled-frame evidence. It
is a functional/diagnostic spot check, **not** statistically reliable
regression evidence. To run all scenarios but only once, use
`npm run observatory -- audit run --preset complete --repeat 1 --wait`.
Use the original Complete preset when you really need repeatable comparisons.

Build presets never clean or delete anything: the build is the normal
incremental build (a first-ever build is a full one because nothing exists
yet). `--repeat` changes the rounds. The audit's own record is a run of
kind `audit` (`summary.json` with `preset`, `jobId`, `children` (run ids),
`findings`, `counts`, `highlights`, `blindSpots`, `rulesVersion`;
`findings.json` with the rules). It is written whatever happened, so a
cancelled or failed audit still lists what finished.

**Findings.** Each has a stable `id` (`rule:subject`, e.g.
`ui.slow-operation:chat:chat.publish-messages`), `rule`, `title`,
`subsystem`, `severity` (high, medium, low, info), `impact` (value, unit,
text), `evidence` (run ids, step, operation), `confidence` and `nextStep`.
Confidence is not a score: it says in how many of the rounds the rule's
threshold was met ("in all 3 runs", "in 2 of 3 runs", "one run; rerun to
confirm"). The rules and thresholds are in `dashboard/public/audit.mjs`
(`RULES`), listed on every audit page, in Copy for AI and in
`audit findings --json`; bump `RULES_VERSION` when one changes. A slow
operation whose own child holds most of its time is reported once, as the
child, "inside" the parent.

**Signal quality.** A step with fewer than two-thirds of repeated runs
reporting valid settled-frame latency gets an informational
`ui.sparse-settled` finding rather than a high-severity slowdown. Expected
sidebar/Gravity Well transitions keep their full durations and traces, but
their planned animation/settling waits are not labeled unexplained CPU work.
A detected event-loop stall still remains a finding during these transitions.
A frame gap is expressed as an interval, not proof of a dropped GPU frame.

**Comparing audits.** `audit compare <id>` (and the page's "Against the
previous audit") compares an audit with the previous one of the same
preset: per scenario and build category, the baseline audit's runs against
this audit's, with the regression rules and budgets above (at least
`minSamples` runs on each side, past the budget, no overlap). Different
presets, rules versions, machines or scenario versions are refused, not
compared. Findings are matched by id into new, gone and still there; a
finding near its threshold can flip on noise, and the page says so.

**For agents.** `audit presets | run | status | cancel | list | findings |
compare | report | timeline <run-id> [--from --to] | operation <run-id>
<action> | trace <run-id> [--turn <n>] | export`, all as `observatory.audit.*`
operations with `--json`.
`audit report` is the Copy for AI Markdown; `audit export` writes the audit
report plus every run's own report, summary and operation trees.

**On the page.** Record has the audit card (preset chips, Run audit,
progress with Cancel, the last audit). An audit's page has the finding
counts and headline numbers, the comparison with the previous audit, the
findings list (filter by severity, subsystem or text; each opens to its
impact, rule, evidence and a jump to its operations), "Operations inside
each action" (pick a run: a strip of actions as wide as they took, the
actions table, and the operation tree of the selected action with self
time, unattributed time and stalls), the build diagnostics (jobs, wall vs
summed time, the longest chain, why it rebuilt, slowest compiles, most
included headers, the time trace if recorded), the runs, the rules and the
blind spots. Compare takes two audits too.

### AI audits

The AI presets reuse the UI Lab and the audit machinery; the trace
(`### Traces: AI turns and database jobs` above) is what they read.

**Provider fixture expectations.** The scripted `ai-turn` scenario declares
that one retry and a long stream pause are deliberate parts of the test.
Observatory retains both in its timeline and reports them under
"Expected mock fixture events", but they are not performance warnings.
Other mocked scenarios, injected test delays without declarations, and
real-provider calls still produce normal findings. The `ai-pipeline`
fixture now explicitly requests indexed Library search against 300 synthetic
notes; its scenario version is 2, so old results are not compared as the
same workload.

**AI timing boundaries.** `Turn (engine)` is the duration of `ai.run`.
`Queued` occurs before `ai.run`; first-byte/model-delta/UI-publication
columns are offsets from `ai.run` start. The first UI text publication
occurs after ChatStore emits its QML update signal; it is not actual pixel
presentation, and can occur after the engine run ends. Medians are computed
per column, so never subtract independent medians as though they came from
the same request.

**Providers.** Each AI scenario names its provider in `ai.provider`:

- `offline`: the app is pointed at a closed local port. Nothing can leave the machine.
- `mock` (`ai-provider.mjs`, `startMockProvider`): a local server for
  `GET /models` and `POST /responses`, answering from a script. It has
  named behaviours (`fast`, `slow-first-byte`, `slow-stream`, `retry`,
  `interrupted`, `failure`, `tool-round`, `cancel-me`), or a scenario's own
  `script`: per request a status (with `retryAfterMs`), a first-byte delay,
  a tool call, text in chunks with gaps and a pause, a cut-off stream, and
  usage. It counts requests and bytes and keeps no body. The runner
  removes `OPENAI_API_KEY` from the app's environment and gives it a
  placeholder key, so a mocked audit can't use yours.
- `real`: refused unless the run says `--allow-provider-calls` (the
  `audit.run` input `allowProviderCalls: true`, which is only accepted for
  the `provider` preset). The app keeps its own key (environment,
  `backend/.env` or Keychain); only its base URL is pointed at a local proxy
  (`startProviderProxy`). The proxy forwards only `authorization`,
  `content-type`, `accept` and the OpenAI organization and project headers.
  It refuses past `maxRequests` (6) with a 429 the app won't retry, and caps
  `max_output_tokens` (64). It records counts, statuses, sizes, first-byte
  times and durations, never a header value or a body.

The mock was checked against the app's real provider code: each named
behaviour, served by the Node mock, drives `AIRuntime` to the expected
outcome (completed, retried once, failed with `remote-closed`, failed with
an HTTP status, two rounds with one tool, cancelled), with the attempts and
retry waits traced as the native test expects.

**Findings (rules v4).** Read from each run's trace digest
(`summary.operations.trace.aiTurns` and `.db`):

| Rule | When |
| ---- | ---- |
| `ai.failed-turns` | A turn ended failed or interrupted. A cancel is not a failure. |
| `ai.slow-preparation` | Preparation ≥ 250 ms (≥ 1 s high). Names the largest stage. |
| `ai.slow-retrieval` | Retrieval ≥ 150 ms (≥ 500 ms high). |
| `ai.slow-context-compile` | Context compilation ≥ 100 ms (≥ 400 ms high), with the compiler, message counts and estimated tokens. |
| `db.queue-contention` | A database job's median queue wait ≥ 20 ms and at least its execution time (≥ 100 ms high). |
| `db.ui-thread-sync` | A synchronous read on the UI thread ≥ 16 ms at once (≥ 50 ms high). |
| `ai.slow-tool` | A tool's median ≥ 250 ms (≥ 1 s high). |
| `ai.excess-tool-rounds` | 4 or more tool rounds in a turn. |
| `ai.first-token-latency` | First model text ≥ 1.5 s (≥ 4 s high), split into Archivist's preparation and the wait for the provider. |
| `ai.stream-pauses` | A pause of 1 s or more between provider events. |
| `ai.retries` | A refused and retried request (≥ 1 s of waiting: medium). |
| `ai.slow-presentation` | First text shown ≥ 100 ms after the model text arrived (≥ 250 ms high). |
| `ai.unattributed` | ≥ 50 ms and ≥ 20% of a turn inside no stage. |
| `ai.missing-instrumentation` | A stage a fully instrumented turn reports is missing. |
| `trace.dropped-events` | The recording dropped events. |

Times are medians over a scenario's turns. With a mocked provider, provider-side
findings say "injected by the mock provider", because the scenario put those
waits there. AI spans, retrieval and context spans, and database queue waits
are left out of the generic `ui.slow-operation` rule. Chat sends
(`sendChatMessage`) are left out of `ui.slow-step`. Each cause is reported
once.

**Comparisons.** Three categories, `ui:ai-pipeline`, `ui:ai-turn` and
`ui:ai-provider`, with metrics `ai.turn`, `ai.prepare`, `ai.retrieval`,
`ai.contextCompile`, `ai.firstNetworkByte`, `ai.firstModelDelta`,
`ai.firstVisibleText`, `ai.persist` and `ai.dbQueueWait`. Each is a median over
a run's completed turns; cancelled and failed turns are counted, not timed.
Two compatibility fields keep different workloads apart: `aiProvider`
(provider kind, mock script and upstream host) and `traceSchema`. A mocked
run is never compared with a real one.

**Inspecting a turn.** In "Operations inside each action", an action that
sent a chat message shows its AI turn as tracks, one lane per thread (UI
thread, AI thread, database writer). Each bar sits under the span that
caused it, retry and queue waits are hatched, failed attempts are red, and
ticks mark the first byte, the first model text and when the chat showed
it. Above the tracks are the firsts (queued, prepared, first byte, first
model text, first text shown, saved). Below them are unattributed time
and anything not measured. The full operation tree by time follows.

**Reports.**

- **Concise:** Copy for AI and `audit report` add an "AI turns" table
  (medians per scenario, turns completed, cancelled and failed) and the AI
  blind spots.
- **Selected finding:** "Copy this finding" on the page, or
  `audit findings <audit-id> --finding <finding-id>` (Markdown; `--json`
  for the object), gives one finding with its rule, basis, evidence and the
  command to read its trace.
- **Full bundle:** `audit export` includes each run's
  `ui-operations.json` with its trace.
- **Trace by cause:** `audit trace <run-id> [--turn <n>]` gives every turn's
  summary, or with `--json`, its stage tree with threads, markers, firsts,
  retries, tools, tokens, unattributed time and what wasn't measured, plus
  the run's database jobs.

**Privacy.** These traces never hold prompts, replies, tool arguments,
file contents, SQL or its parameters, keys, headers or paths. They hold
names of stages and tools, counts, sizes, statuses, error kinds and times.
The mock and the proxy keep no bodies or header values. A test reads every
recorded artifact of a mocked audit and checks that a fake key put in the
environment appears in none of them.

## Live recordings (flight recorder)

Press **Start recording**, use Archivist the way that felt slow, and press
**Stop** (or let it stop by itself). The recording opens as a timeline you
can click, plus the same **Copy for AI** and **Export evidence** buttons as
every other run. Agents can do the same through `observatory.live.*`.

### For people

- Inside Archivist: open the Observatory panel; it opens on **Record**.
  Choose a length (15, 30, 60 or 120 s; 30 by default), optionally name it,
  and press **Record**. A red dot shows in the top bar for as long as it
  runs, on every page; clicking it brings you back to **Stop**.
- **Mark** (or Enter in the note box) drops a note at that moment ("felt
  slow here").
- When it stops, the recording opens: a summary sentence, frames per window,
  how late the UI thread answered, CPU, memory, threads, and a timeline with
  one lane per window, the UI thread, CPU, memory and the instrumented
  operations. Highlighted moments are clickable; drag across the timeline to
  see everything in a time range.
- The browser dashboard (`--control`) shows the same page. It can record only
  while **Let agents record** is on in the app, because it goes through the
  same door as an agent.

### For agents

```bash
npm run observatory -- live status --json
npm run observatory -- live record --duration 30 --label "open big project" --json   # start, wait, return the result
npm run observatory -- live start --duration 60 --json
npm run observatory -- live mark "opened the 2k-file project" --json
npm run observatory -- live stop --json
npm run observatory -- live list --json
npm run observatory -- live get <run-id> --json
npm run observatory -- live spikes <run-id> --json
npm run observatory -- live window <run-id> --from 3000 --to 4000 --json
npm run observatory -- live compare <base-run-id> <run-id> --json
npm run observatory -- report <run-id>
npm run observatory -- export <run-id>
```

`status/start/stop/mark/wait/record` reach the running app, and only while
the person has turned on **Let agents record** in the Live panel. The other
commands read saved recordings and work any time. `observatory.live.status`
answers `available: false` (not an error) while access is off.

### How the app is reached

Turning on **Let agents record** writes
`<Observatory folder>/live/agent-access.json` (mode 0600) with the app's
loopback address, a fresh random key and the app's process id. Turning it
off, or quitting Archivist, deletes it. The CLI reads that file and calls
`POST http://127.0.0.1:<port>/observatory/api/live/<status|start|stop|mark>`
with the key in `X-Observatory-Agent-Token`. The app accepts that key only
for those four operations, only from requests without a browser `Origin`,
and only while access is on. Only the panel (same origin, the panel key) can
turn access on or off. The CLI refuses an address that isn't 127.0.0.1, so
the key never goes anywhere else. Nothing is uploaded.

Inside the app, the panel's test controls never send `observatory.live.
status/start/stop/mark/wait` through the Node script: the app refuses them
on its control route (they belong to `/observatory/api/live/`), because the
script would call back into the app and `wait` would hold a worker thread
for minutes. `live wait` is a CLI loop of short `status` calls.

### What is recorded

A `live-session` run (`<timestamp>-live-<hex>`) with:

| File | What |
| ---- | ---- |
| `summary.json` | identity (source, app, Qt, machine), status, `live` (duration, who started and stopped it, windows, statistics, notable moments) |
| `metrics.json` | every frame swap per window, every UI-thread probe, every resource sample (ms since the start) |
| `events.json` | operations (spans with durations), markers and notes, in time order |
| `live-stream.ndjson` | what was flushed every second while recording, for crash recovery |

- **Frames:** `QQuickWindow::frameSwapped` per window, timed on the render
  thread. An interval is not GPU time, compositor latency or when the frame
  reached the screen. Qt draws only when something changes, so long idle
  intervals are normal.
- **UI thread:** a 50 ms timer on the UI thread; how late it fires.
- **Resources (every 500 ms):** process CPU as % of one core, resident
  memory, macOS memory footprint (`phys_footprint`), thread count. Anything
  the platform doesn't provide is recorded as `null` and listed as missing.
  CPU is also `null` for a sample taken less than half an interval after the
  previous reading, which is always the case for the first one.
- **Measurement version** (`live.measurementVersion`, in `summary.json` and
  `metrics.json`): 1 was the first recorder; 2 leaves that first CPU sample
  out (in version 1 it was noise, 181.9 % in the fixture recording, and
  inflated max and mean CPU). Recordings with different versions, or
  different probe or sample intervals, are not compared.
- **Operations:** sidebar open/close/section/resize, workspace publish (with
  the `archivist.wells.timing` stages), layout changes, tab switches, split
  resizes, magnet drags and undocking, wells minimize/restore, chat
  conversation switches. Names and a few small fields only: no content,
  typing, file names or clipboard. `archivist.wells.timing` logging keeps
  working as before.
- **Notable moments:** a frame gap of 50–250 ms between steady frames; a
  longer gap only when it overlaps a UI-thread stall (otherwise it's idle);
  a UI-thread delay of 50 ms or more. Each is listed with the operations it
  *occurred during* or near. That is correlation, never a cause.

### Limits and safety

- One recording at a time; 15/30/60/120 s only; it always stops at the
  length (and on quit). Memory is bounded (200k frames, 20k events, 6k
  probes, 1k samples); anything past that is counted as dropped.
- Writing happens on a single writer thread (the stream every second, the
  final files at the end), with one exception: the first `summary.json` is
  written on the UI thread when recording starts, before anything is
  measured, so a failure to write can be reported to the caller (1.4–8.7 ms
  measured on Linux; not measured on macOS).
- If the app dies while recording, the next launch marks that run
  `interrupted` with `partial: true` and rebuilds what it can from the stream.
  A run still `running` more than 5 minutes past its length is finished the
  same way even if its process id now belongs to another program.
- The Record page itself updates once a second while recording (its
  indicator is static on purpose). With the panel open, those redraws show up
  as a few frames per second in the recording; close or hide the panel for
  the cleanest numbers.
- Live recordings have no baseline (what you do differs each time). Use
  `live compare` on two recordings of the same interaction. Besides refusing
  incompatible recordings, it warns when they come from a debug and a
  release build, different machines, systems, Qt or app versions, the same
  build, uncommitted changes, partial or failed recordings, recordings that
  dropped data, ones where the app left the foreground, very different
  lengths, or different windows.

### Overhead

Measured with the real recorder on Linux (Qt 6.4, offscreen software
rendering, one window redrawing every 16 ms, alternating rounds with
recording off and on):

| | Recording off | Recording on |
| --- | --- | --- |
| Frame interval, median | 20.47 ms | 20.48–20.49 ms |
| Frame interval, p95 | 20.71–20.76 ms | 20.72–20.76 ms |
| Process CPU (% of one core) | 1.6–1.7 % | 1.8–1.9 % |

UI-thread work per recording: `ProcessMetrics::read()` every 500 ms (median
23 µs, p95 39–60 µs), `start()` median 1.4–1.8 ms and p95 2.7–8.7 ms over
two measurements, `stop()` 0.13–0.37 ms.
None of this was measured on macOS or with the real Archivist UI.

The event API itself, before and after span ids (Linux, release build, the
same minimal sink, three alternating runs, ns per call):

| | Not recording | Recording |
| --- | --- | --- |
| `Scope`, before / after | 11–18 / 12–21 | 157–161 / 206–216 |
| `begin` + `end`, before / after | 7–15 / 10–16 | 200–222 / 263–273 |
| `marker`, before / after | 5–6 / 6–9 | 122–133 / 166–182 |

Not recording is one atomic load either way (the spread is noise). While
recording, an id, a parent and a thread label add about 50–60 ns per event.
A mocked AI turn with a retry and a tool round makes 31 events, about
30 µs of recording work with the real session sink (about 1 µs per event
there) against a turn of hundreds of milliseconds.

## Resource limits

| What | Limit | Past it |
| --- | --- | --- |
| One live recording | 15/30/60/120 s, one at a time | refused (`busy`) |
| Recorder memory | 200k frames, 20k events (256 open), 6k probes, 1k samples | counted as dropped; shown in the summary, Copy for AI and `live compare` |
| Stream read on recovery | 256 MB | the rest is ignored |
| Artifacts served or read | 32 MB each | refused |
| Control script answer | 8 MB, 60 s | `internal` error |
| Observatory requests in the app | 2 worker threads, 8 waiting or running | `503 unavailable` |
| Runs sent to the dashboard page | newest 2,000 plus those baselines name | the page says how many it left out; the CLI reads all |
| `live wait` | 300 s | `timed-out` (exit 124) |
| UI tests | one at a time per Observatory folder | `busy` (exit 4) |

Run folders are never deleted automatically; a long history only costs disk
space and slower unfiltered scans.

## Reproducing the measurements

- **History scaling:** write N synthetic runs and time the listings.
  Measured on Linux, median of 5 reads (before → after this change):

  | Runs | Page listing (app) | Page body | `tests status` poll | `runs list` | `live list` |
  | --- | --- | --- | --- | --- | --- |
  | 100 | 22 → 14 ms | 0.27 MB | 18 → 1.4 ms | 16 → 4 ms | 17 → 9 ms |
  | 1,000 | 142 → 154 ms | 2.7 MB | 191 → 1.0 ms | 170 → 5 ms | 161 → 67 ms |
  | 10,000 | 1,557 → 342 ms | 26.8 → 5.4 MB | 1,826 → 1.5 ms | 1,818 → 20 ms | 1,654 → 612 ms |

  A filtered `runs list` (`--kind`, `--status`, `--scenario`) still reads
  every summary (1.8 s at 10,000 runs).
- **Recorder overhead:** a test window redrawing every 16 ms, recorded and
  not recorded in alternating rounds of the same length, comparing frame
  intervals and process CPU; the per-call costs above are timed with
  `QElapsedTimer` around each call.
- **Diagnostic check:** a scripted session with a 90 ms stall during a
  `workspace.publish` span and a 320 ms stall during a
  `chat.switch-conversation` span; `live spikes` lists the 320 ms one first
  and names the span each occurred during.

## What is and isn't verified

- **Verified here (Linux, Qt 6.4, offscreen):** the Node suite, the native
  Observatory, recorder, records and IDE host tests, the lock under real
  competing processes, the control route under saturation, the recorder's
  overhead and the history scaling above.
- **Not verified here:** anything on macOS (Qt 6.8, Metal, `phys_footprint`,
  App Nap throttling), the full Archivist UI, `npm run test:native` as a
  whole, and the UI Lab against a real `build/frontend-debug`. In
  particular the new chat and startup scenarios, the startup and chat spans
  in a real app, and `--time-trace` on the Archivist tree have only been
  compiled and unit-tested here (the UiLab collector and chat seeding run
  for real in `ui-lab-test`; build evidence runs for real against a small
  CMake project in `test/build-evidence.test.mjs`). Audits run end to end
  here with the stand-in app (`test/audit.test.mjs`), including a regression
  from doubled operation times; a `build` or `complete` preset against the
  real Archivist tree has not been run.

## Code

| File | Responsibility |
| ---- | -------------- |
| `cli.mjs` | Commands, assembling and saving the summary |
| `capture.mjs` | Running phases as subprocesses, timing, signals |
| `build-phases.mjs` | The Archivist build phases and build classification |
| `metadata.mjs` | Git, machine and toolchain facts |
| `ninja.mjs` | Ninja progress output and `.ninja_log` |
| `build-evidence.mjs` | Every job, rebuild causes, header fan-out, wall vs summed time, dependency chain, compiler-cache counters |
| `time-trace.mjs` | Summing Clang `-ftime-trace` files (opt-in profiling build) |
| `ui-operations.mjs` | UI Lab actions opened up into the operations and stalls inside them |
| `runtime-log.mjs` | Parsing `archivist.wells.timing` lines |
| `storage.mjs` | Record location, run ids, atomic writes |
| `report.mjs` | The build terminal summary |
| `ui-lab.mjs` | Starting the isolated app for a scenario, timeouts, classifying the run |
| `ui-stats.mjs` | Median, p95 and max over UI actions |
| `ui-report.mjs` | The UI Lab terminal summary |
| `ui-scenarios/` | Scenario definitions |
| `baselines.mjs` | Reading, setting and removing baselines (`baselines.json`) |
| `budgets.json` | Default regression budgets |
| `comparisons.mjs` | `baseline` and `check` commands, saved reports |
| `regression-report.mjs` | The regression terminal summary |
| `paths.mjs` | Fixed repository locations (build folder, scenarios, CLI) |
| `control/operations.mjs` | The control operations: inputs, validation, results, error codes |
| `control/jobs.mjs` | Background UI test jobs, the one-test lock, cancel, progress |
| `control/evidence.mjs` | Evidence exports and their manifest |
| `control/gate.mjs` | Who may call the controls over HTTP |
| `control/human.mjs` | Plain-text output of the control commands |
| `control/zip.mjs` | A small ZIP writer for exports |
| `control/live.mjs` | Reaching the running app's live recorder (agent-access.json, 127.0.0.1 only) |
| `control/live-operations.mjs` | The `observatory.live.*` operations |
| `control/audit.mjs` | Writing the audit record when an audit job ends; the `observatory.audit.*` operations |
| `dashboard/public/audit.mjs` | Presets, findings rules, audit comparison and the audit report (shared by the page and the CLI) |
| `dashboard/public/audit-page.js` | The audit card on Record, the audit page and audit-to-audit Compare |
| `dashboard/server.mjs` | The browser dashboard's local, read-only server |
| `dashboard/records.mjs` | Reading runs and artifacts from disk for that server |
| `dashboard/public/` | The page: plain JavaScript, no dependencies |
| `dashboard/public/data.js` | The page's data layer over the host requests |
| `dashboard/public/normalize.mjs` | One run's summary into the flat record the page uses |
| `dashboard/public/compare.mjs` | Baseline vs candidate: blockers, warnings, deltas |
| `dashboard/public/regression.mjs` | Baseline comparison rules and the report (shared by the CLI and the page) |
| `dashboard/public/diagnostic.mjs` | The Copy for AI report (shared by the page and the CLI) |
| `dashboard/public/redact.mjs` | Redaction for reports and exports |
| `dashboard/public/lab.js` | The scripted UI test panel and the Copy for AI / Export buttons |
| `dashboard/public/live.mjs` | Live recording analysis: moments, context, ranges, comparison, report (shared by the page and the CLI) |
| `dashboard/public/live-page.js` | The recorder and a recording's page with its timeline |
| `dashboard/public/dom.js` | The page's DOM and formatting helpers |
| `dashboard/public/filter.mjs` | Run filters |
| `dashboard/public/preview.mjs` | Paging and searching a log |
| `frontend/src/Core/Domains/Observatory/` | The same requests, served inside Archivist; `observatory_control.*` hands control requests to the CLI |
| `frontend/qml/App/Workbench/Observatory/` | The Observatory panel |
| `frontend/src/Core/Domains/Observatory/live_*.{h,cpp}`, `process_metrics.*` | The recorder's buffers, statistics, files and process metrics |
| `frontend/src/App/Services/live_recorder.*` | The recorder service in the app (windows, probes, writer thread) |
| `frontend/src/App/Services/ui_lab.*` | The UI Lab's hook in the app: clock, frames, input, operations and stalls, chat fixtures |

## Testing the collector

```bash
npm run test:observatory
```

Uses Node's built-in test runner with small temporary subprocesses and a
stand-in app (`test/fixtures/fake-app.mjs`); it needs no Qt build. The
dashboard tests use the made-up runs in `test/fixtures/dashboard-runs.mjs`.
The live tests use `test/fixtures/live/`, a short recording made by the real
recorder (Linux, offscreen), and a stand-in recorder
(`test/fixtures/fake-live-app.mjs`). The native side is covered by
`core-observatory-live-test` and `live-recorder-test` (`npm run test:native`).
