# Observatory

Observatory keeps a local, structured record of Archivist development builds,
so a change's effect on build time can be compared run to run. This is the
pieces so far: build capture, the UI Lab (scripted UI scenarios timed in a
real, isolated app instance), and an import of the existing
`archivist.wells.timing` runtime logs, plus a local dashboard to browse and
compare the records, baselines that flag regressions, and controls that
let people (the Test Lab) and agents (`--json`) run tests and hand the
results to an AI.

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

The dashboard's **Baselines** page shows each category's baseline, what it
was compared with, every metric's verdict, slowest-step changes and a trend
chart with the baseline runs marked. It is read-only: baselines are set from
the command line.

## Controls: Test Lab, agents, Copy for AI

People and agents drive the same controller (`control/operations.mjs`):
people through the dashboard's **Test Lab** and its buttons, agents through
the CLI with `--json`. Both start the same jobs, read the same records and
get the same reports.

### For people

- **In Archivist:** open the Observatory panel and go to **Test Lab**. Its
  controls are on already.
- **In a browser:** run `npm run observatory:dashboard -- --control` and open
  the link it prints. Without `--control` the dashboard stays read-only.

From the Test Lab you can:

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
every run page, on Test Lab results, and on every category of the Baselines
page. **Export evidence** writes a redacted folder and `.zip` for an agent
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
- **Build tests stay on the command line.** The Test Lab runs UI
  scenarios; builds are still recorded with `npm run observatory:build`.
- **Slightly different redaction in the page.** The page doesn't know where
  the checkout is, so its reports hide repository paths under your home
  folder too. The CLI keeps them as `<repo>/…`.

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
| `dashboard/server.mjs` | The browser dashboard's local, read-only server |
| `dashboard/records.mjs` | Reading runs and artifacts from disk for that server |
| `dashboard/public/` | The page: plain JavaScript, no dependencies |
| `dashboard/public/data.js` | The page's data layer over the host requests |
| `dashboard/public/normalize.mjs` | One run's summary into the flat record the page uses |
| `dashboard/public/compare.mjs` | Baseline vs candidate: blockers, warnings, deltas |
| `dashboard/public/regression.mjs` | Baseline comparison rules and the report (shared by the CLI and the page) |
| `dashboard/public/diagnostic.mjs` | The Copy for AI report (shared by the page and the CLI) |
| `dashboard/public/redact.mjs` | Redaction for reports and exports |
| `dashboard/public/lab.js` | The Test Lab page and the Copy for AI / Export buttons |
| `dashboard/public/dom.js` | The page's DOM and formatting helpers |
| `dashboard/public/filter.mjs` | Run filters |
| `dashboard/public/preview.mjs` | Paging and searching a log |
| `frontend/src/Core/Domains/Observatory/` | The same requests, served inside Archivist; `observatory_control.*` hands control requests to the CLI |
| `frontend/qml/App/Workbench/Observatory/` | The Observatory panel |

## Testing the collector

```bash
npm run test:observatory
```

Uses Node's built-in test runner with small temporary subprocesses and a
stand-in app (`test/fixtures/fake-app.mjs`); it needs no Qt build. The
dashboard tests use the made-up runs in `test/fixtures/dashboard-runs.mjs`.
