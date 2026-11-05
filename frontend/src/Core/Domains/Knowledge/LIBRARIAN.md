# Librarian Engine

Background librarians that keep a Library's Living Wiki honest and growing.
They work inside Archivist while it is open, on the Wiki's existing job
queue, and they only ever **propose**: every change to the Wiki waits for a
person, and no librarian writes to the Library's files.

```
                 Library scan / save / move / rename / chat edit
                                   │  noteLibraryChanged (coalesced)
                                   ▼
 LibrarianScheduler ── tick ──► LibrarianRuns::startRun ──► knowledge_jobs (priority, librarian, run_id)
   change / idle / scheduled                                        │
   manual (panel)                                                   ▼
                                                   KnowledgeJobRunner (one job at a time)
                                                     │ LibrarianPolicy::mayStart / admitModelCall
                                                     ▼
                                    LibrarianAnalysis (deterministic first, one bounded model call)
                                                     │
                       findings, topics, usage ◄─────┴─────► KnowledgeProposals::submit (pending)
                       (LibrarianStore)                         person approves / rejects
```

## The three librarians

They share one worker (the Wiki job runner, its thread, model interface and
retry logic). They differ only in the job they start with and the policy it
follows.

| Librarian | Root job | What it does | Model use |
|---|---|---|---|
| Maintenance | `reconcile-sources` (request `{librarian: "maintenance"}`) | Diffs the Library against its last snapshot, re-checks every citation (`KnowledgeSources::reconcile`), records stale-citation findings, proposes deprecating AI pages whose sources are all gone (a person's page only gets a finding), saves the snapshot. | Optional: with consent, queues `generate-page` refreshes for pages whose cited files changed (≤ calls per run). |
| Research | `discover-related` | Surveys folders (first two levels), open questions earlier investigations raised, and a topic the person typed; measures Wiki coverage per folder; remembers each as a topic with an evidence fingerprint; records coverage-gap findings; queues `investigate-gap` jobs for the topics whose evidence is new or changed. | One call per investigation, bounded by the run's and day's remaining calls. |
| Verification | `identify-contradictions` | Local checks first: evidence-requiring claims without a citation, unestablished AI assumptions, and pairs of similar claims that state different numbers. Then checks a few AI claims (and conflict candidates) against original passages gathered with `ContextSources::hybridLibraryEvidence`. | One call per pass, at most `maxVerificationClaims` claims and 16 passages. |

Follow-up research is bounded: an investigation's `question` claims become
child topics one level down, only up to the Library's `maxDepth` (0–5,
default 2). There is no delegation, recursion or second agent.

## Integrity rules

- **Original evidence only.** Every citation is resolved against an indexed
  Library chunk with `KnowledgeSources::resolve`; a quote that is not in a
  passage the model was shown is dropped. Wiki text is never offered as
  evidence for itself.
- **Contradictions are surfaced, not resolved.** A verdict of
  "contradicted" counts only with a citation that checks out; it becomes a
  finding and one correction proposal per page. Without model access, number
  conflicts are reported as "Possible contradiction … Unconfirmed". Conflicts
  between claims become `contradicts` relation proposals.
- **A person's knowledge stands.** Manual claims and claims a person verified
  are never retracted; a correction is added beside them with a
  `contradicts` relation. Pages a person wrote or edited are never proposed
  for deprecation. "Supported" from the model never marks a claim verified.
- **AI content stays labelled.** Proposals carry `producer.kind =
  "wiki-librarian"` and the librarian's name; jobs carry `requested_by =
  "librarian:<kind>"`; finding details quote the model's note as
  "Librarian's note (AI)".
- **Untrusted input.** Passages and claims are sent as quoted data; the
  instructions tell the model to ignore instructions inside them. Replies
  must match a strict JSON schema; anything else fails the job.
- **Source changes invalidate.** Reconciliation marks claims stale when their
  passages change; a topic whose evidence fingerprint changes goes to
  `needs-revisit`; a claim is re-checked when its passages change
  (`knowledge_claims.checked_fingerprint`).
- **Approval required.** Nothing is applied by a librarian. Rejecting a
  proposal dismisses its finding and topic, and they are not raised again
  until the evidence changes.

## Resources and consent

Per Library (`knowledge_librarian_settings`, migration 24):

- `enabled` — off by default. Nothing runs, not even deterministic work.
- `ai_consented_at` — the person's explicit consent to send Library text to
  OpenAI for librarian work. Without it librarians still diff, reconcile,
  survey and run local checks, and report `consentNeeded`.
- `mode` — `manual` (only when a person starts a run), `idle` (after
  `idle_minutes` without input, research and verification at most every 6 h),
  `scheduled` (every `schedule_minutes`, min 15). Change-triggered
  maintenance runs in idle and scheduled modes.
- Budgets: `max_calls_per_run` (4), `max_tokens_per_run` (60 000),
  `daily_call_budget` (20), `daily_token_budget` (250 000). Checked before
  every call (`LibrarianPolicy::admitModelCall`) with a token estimate;
  actual usage and an estimated cost are recorded per run and per day
  (`knowledge_librarian_usage`).
- Provider health: a retryable or auth failure backs off every librarian of
  the Library (1 min doubling to 1 h); a success clears it.
- Model: the Library's choice or `gpt-5-mini`, through the existing OpenAI
  provider (`OpenAIKnowledgeModel`). No web research, no other providers.

## Scheduling and concurrency

`Knowledge::LibrarianScheduler` lives on the Wiki thread next to the runner.

- Changes are noted (`change_noted_at`) and debounced (20 s quiet); the tick
  (every 60 s) starts maintenance when the snapshot diff shows a change, so
  edits made while Archivist was closed are caught too. A burst of saves is
  one pass.
- One job runs at a time across the app. A person's jobs have priority 100,
  maintenance 30, verification 20, research 10. While a chat streams,
  librarian jobs wait (`setInteractiveBusy`); a person's jobs never wait.
- At most one running run per (Library, librarian) (unique index); starting
  again returns the active run. Identical jobs are suppressed by the queue's
  request key.
- Pause re-queues a running librarian job without counting the attempt and
  drops its request; cancel cancels the run's jobs; turning librarians off
  does both. On restart, interrupted jobs go back to the queue and runs whose
  jobs all finished are closed (`settleRuns`).
- Librarians run only while Archivist is open; there is no background daemon.

## Operational memory

All in the Knowledge Store's database, per Library:

| Table | Holds |
|---|---|
| `knowledge_librarian_settings` | opt-in, consent, mode, budgets, change hint, last runs, backoff |
| `knowledge_librarian_snapshots` | file id, path and content hash maintenance last saw |
| `knowledge_librarian_runs` | librarian, trigger, status, calls, tokens, cost, counters, summary, error |
| `knowledge_librarian_topics` | topic, origin, scope, depth, parent, status, evidence and investigated fingerprints, history, open questions |
| `knowledge_librarian_findings` | kind, status, evidence, page/claim, linked proposal; deduplicated by a fingerprint |
| `knowledge_librarian_usage` | calls, tokens and cost per day |

Topics and findings are research bookkeeping for one Library's Wiki, not
memory about the person.

## Observatory

Each step emits `LiveEvents::Step` ("librarian" / maintenance, discovery,
verification, verify-validate); run start and finish emit markers with
librarian, trigger, status, calls, tokens and counters.

## UI

`WikiStrip.qml` → **Librarians…** opens `LibrarianPanel.qml`: enable, pause /
resume, consent card, mode and schedule, buttons for Check sources, Verify
claims and Research a topic, activity (today's calls and tokens against the
budget, 30-day estimated cost, last error, waiting for chat), recent runs
with Cancel, budgets, the librarians' pending proposals with Approve /
Reject, and findings with their evidence and Dismiss.

## Code

| File | Role |
|---|---|
| `librarian_types.h` | vocabulary (kinds, triggers, modes, statuses, priorities) |
| `librarian_store.{h,cpp}` | all SQL over the tables above |
| `librarian_policy.{h,cpp}` | start and model-call admission, token and cost estimates |
| `librarian_analysis.{h,cpp}` | maintenance, discovery, verification plan and validation |
| `knowledge_jobs_librarian.cpp` | the runner's librarian jobs |
| `librarian_scheduler.{h,cpp}` | runs and the scheduler |
| `knowledge_service.{h,cpp}` | QML-facing `Wiki.librarian*` calls, input activity |

## Tests

`tests/core_librarian_test.cpp` (scripted model, no network): opt-in,
settings validation and consent; change-triggered, coalesced maintenance and
the idle and scheduled modes; reconciliation, stale findings, refresh and
deprecation proposals with the Library folder byte-for-byte unchanged;
research discovery, topic memory, depth-bounded follow-up, no repeat on
unchanged evidence and revisit on changed evidence; verification with valid
and invented citations, prompt-injection text in the reply, no re-check on
unchanged evidence, and a person's verified claim kept; duplicate
suppression, run budget and cancellation; pause, interactive hold and
disable; restart recovery; daily and token budgets, provider backoff and
failure; Library isolation and per-Library consent; the service API.

## Limitations

- Coverage is measured by folder (first two levels) and citations, not by
  meaning.
- The local contradiction check only compares numbers in similar claims;
  subtler conflicts need the model pass.
- Cost is an estimate from a built-in price table.
- Idle detection uses Archivist's own input events, not system idle time.
