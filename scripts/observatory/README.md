# Observatory

Observatory keeps a local, structured record of Archivist development builds,
so a change's effect on build time can be compared run to run. This is the
first piece: build capture, plus an optional import of the existing
`archivist.wells.timing` runtime logs. There is no dashboard yet.

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
| `kind` | `build` or `runtime-log`. |
| `runId`, `label` | Unique id; your `--label` or `null`. |
| `createdAt`, `finishedAt` | ISO 8601 wall-clock times. |
| `status` | `running` (still going, or the collector was killed), `success`, `failure`, `interrupted`, `error` (collector bug). |
| `exitCode` | What the command exited with: the failing phase's code, or 128 + signal. |
| `totalDurationMs` | Monotonic time from the first phase's start to the end. |
| `source` | `commit`, `branch` (`null` when detached), `dirty`, `changedFiles`. |
| `environment` | `platform`, `osVersion`, `architecture`, `cpuModel`, `logicalCpus`, `totalMemoryBytes`, `nodeVersion`. |
| `toolchain` | From the configured build directory: `buildType`, `generator`, `cmakeVersion`, `ninjaVersion`, `compiler` (`id`, `version`, `path`), `qtVersion`, `compilerCache` (`launcher` configured, `ccacheInstalled`, `sccacheInstalled`). |
| `buildConditions.before` | Whether the build directory, CMake cache and `.ninja_log` existed before the run. |
| `buildConditions.classification` | `clean` (no `.ninja_log` before), `no-change` (dry run planned 0 steps), `incremental`, or `null` when the native build didn't run. Always inferred. |
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
  (`edgesRecorded`, `sumOfEdgeDurationsMs`, `slowestEdges`). An edge is one
  compile or link step, so `slowestEdges` are real per-file compile times. If
  Ninja recompacted its log during the build, this is marked `unavailable`.
- **Inferred from output** (labelled with a `basis` field): IDE cache reuse
  (from `build-ide.mjs`'s "using cached build" message), `ninjaSteps` (the
  `[n/total]` progress lines), `plannedSteps` (Ninja dry run) and the build
  classification.

### What it cannot tell you yet

- Whether a compiler cache actually hit (only whether one is configured).
- Clean-build times, unless the build directory happened to be empty. A
  deliberate clean-build experiment is future work.
- `sumOfEdgeDurationsMs` is not CPU time: steps run in parallel and include
  waiting. Wall-clock build time is `native-build.durationMs`.
- Durations from one run are noisy; compare several runs on the same machine.
- Anything about the running app other than imported wells timings.

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
| `report.mjs` | The terminal summary |

## Testing the collector

```bash
npm run test:observatory
```

Uses Node's built-in test runner with small temporary subprocesses; it needs
no Qt build.
