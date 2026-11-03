# Living Wiki and Knowledge Store

The Living Wiki is a per-Library set of pages that an AI writes from the
Library's files and a person reviews. Every factual sentence the AI adds is a
**claim** with **citations** to exact Library passages. Nothing the model
returns reaches a page until a person approves it, pages never touch the
Library folder, and the cited files stay the authority.

It lives in Archivist's SQLite database (migration 23), in the native core,
and uses only the existing OpenAI provider and embedding index.

## What it is made of

```
knowledge_pages       one row per page; slug, title, type, body, status, review status, revision
knowledge_claims      statements on a page; type, verification status, confidence, origin
knowledge_citations   claim → Library chunk (file, path, lines, chunk hash, quote), with a status
knowledge_relations   page/claim → page/claim: depends-on, related-to, contradicts, supersedes, derived-from, part-of
knowledge_revisions   immutable snapshots of a page (body + claim set), with actor and proposal
knowledge_proposals   reviewable changes: create, modify, deprecate, merge, reconcile
knowledge_jobs        durable work queue: generate-page, expand-page, reconcile-sources
knowledge_search      FTS5 over page and claim text (kept by triggers)
```

All rows carry `library_id`; foreign keys cascade when a Library is deleted.
Relations, FTS rows and embedding sources are cleaned up by trigger when a
page or claim goes away.

### Pages

Types: `overview`, `topic`, `concept`, `entity`, `decision-log`,
`file-summary`, `question`. Status: `active`, `deprecated`, `merged`. Review
status: `reviewed`, `needs-review` (a source changed), `draft`. A page shows
under the virtual path `Wiki/<slug>.md`; no file is ever written.

### Claims and confidence

Claim types: `fact`, `decision`, `inference` (need evidence), `definition`,
`question`, `assumption`. Verification status:

| Status | Meaning |
| --- | --- |
| unverified | no supporting citation (allowed for questions/assumptions, or added by hand) |
| supported | at least one citation still matches a live Library passage |
| verified | a person confirmed it |
| contested | a `contradicts` relation points at it |
| stale | it had citations and none of them supports it any more |
| retracted | withdrawn by review; kept in history, never searched |

Confidence is computed, never taken from the model: verified 0.95; supported
0.6 plus 0.1 per additional **distinct** passage (by chunk hash) up to 0.85;
contested 0.3; stale 0.15; unverified AI claims 0.25 (manual 0.5). A model's
own confidence can only lower the value. Regenerating a page, or citing the
same passage twice, therefore cannot raise confidence, and Wiki text is never
a citation (citations accept only `library-chunk`), so the Wiki can't support
itself.

## Generation pipeline

`KnowledgeJobs::enqueue` → `KnowledgeJobRunner` (one job at a time):

1. **Select evidence.** `KnowledgeGeneration::selectEvidence`: the given files'
   chunks, or a Library search for the topic (hybrid when the Library opted in
   to semantic indexing, keyword otherwise), plus every passage the target page
   already cites. Up to `maxPassages` (default 12). An existing page whose slug
   matches the topic becomes the target, so asking twice revises instead of
   duplicating.
2. **Fingerprint.** Request + passage hashes + target revision + prompt version.
   If an earlier job with the same fingerprint left a pending proposal, or an
   applied one whose page has not changed since, the job completes as
   `unchanged` without calling the model.
3. **Generate.** `OpenAIKnowledgeModel` sends one Responses API request with a
   strict JSON schema (`store: false`). Passages are labelled `P1..Pn`, existing
   claims `E1..En`, all quoted as data.
4. **Validate.** `parseResponse` drops what it can't verify and says so in the
   proposal: citations to passages that weren't offered, quotes not found in
   the passage, fact/decision/inference claims left without a valid citation,
   relations to unknown targets. Relations need evidence; no speculative links.
5. **Propose.** A `create` or `modify` proposal stores the payload, the
   evidence list, validation notes and a line diff. Its base revision is the
   page revision it was written against.
6. **Review.** `KnowledgeProposals::approve` applies it in one transaction:
   page body, claims (de-duplicated by normalized statement), citations,
   relations, and an immutable revision. If the page moved on in the meantime
   (another proposal, a manual edit, a restore) nothing is applied and the
   proposal becomes `conflicted`. `reject` changes nothing.

A newer content proposal for a page supersedes older pending ones; identical
proposals are never stored twice.

### Manual edits

`createManualPage`, `editPage` (with a base revision; a mismatch is
`WIKI_REVISION_CONFLICT`), `addManualClaim`, `reviewClaim`
(verify/retract/reopen) and `restoreRevision` all write revisions. A refresh
retracts only AI claims that were not verified; manual claims and
person-verified claims survive regeneration. A page edited by hand is marked
so, and an AI proposal written against an older revision conflicts instead of
overwriting it.

## Source reconciliation

`KnowledgeSources::reconcile` (job type `reconcile-sources`, started from the
review window's "Check sources") re-anchors every citation of the Library:

| Situation | Result |
| --- | --- |
| same chunk, same text | `valid` (lines refreshed) |
| file moved/renamed in the app (same file id) | `moved` |
| file renamed outside the app (new file id, same text) | `moved` to the new file |
| re-indexed, text shifted | `reanchored` to the new chunk and lines |
| quote still in the file, chunk text changed | `reanchored`, hash updated |
| quote gone | `modified` |
| file gone / unreadable | `deleted` / `unavailable` |

Claims then follow their citations (supported ↔ stale). For a page with
stale claims the page becomes `needs-review` and one `reconcile` proposal
lists them; approving retracts them as a new revision, rejecting keeps them
stale. The same situation never produces a second proposal, and a question
the person already answered is not asked again. Reconciliation never edits a
page body, and nothing is deleted: when the text comes back, the claim
recovers.

## Retrieval

`KnowledgeRetrieval::search` runs FTS5 (`bm25`, title weighted) and, when the
Library opted in, vector search over `wiki-claim` and `wiki-page` sources,
fused with the same reciprocal rank fusion as Library search. Scores are
weighted down by low confidence and `needs-review`. Retracted and stale
claims, and deprecated or merged pages, are not returned.

`ContextSources::wikiKnowledge` turns hits into Context Assembly v3
`wiki-knowledge` candidates. They are untrusted reference material, labelled
as Wiki interpretations with status, confidence, freshness and their
original sources, and rank below Library evidence. Config keys
(`context-assembly:3`): `wikiTokens` (2000; 0 turns it off), `wikiPriority`
(50), `maxWikiItems` (6), `wikiRetrieval` (`auto` / `off`).

## Jobs

A job row holds everything: request, source fingerprint, stage, progress,
attempts, error code and message, result. Running jobs return to `queued`
on start (`recoverInterrupted`); provider failures retry with exponential
backoff up to `maxAttempts` (rate limits honour `retry-after`); a missing key
or a refused request fails at once with a clear message. Jobs can be
cancelled (the model request is dropped) and retried. Only one active job per
(Library, type, request) can exist.

Job types `investigate-gap`, `discover-related`, `identify-contradictions`,
`suggest-improvements` and `process-correction` are reserved in the schema for future autonomous librarians and are
refused by `KnowledgeJobs::enqueue` until one implements them. A librarian
would enqueue jobs with `requested_by` naming itself and submit proposals
through `KnowledgeProposals::submit`, so everything it does stays reviewable.
There is no scheduler.

## App

`Knowledge::KnowledgeService` is the QML singleton `Wiki`: jobs on an
"ArchivistWiki" thread, reads and review actions on its own connection,
failures as `{ok: false, code, message}`. Approved changes trigger a semantic
index sync. `WikiStrip.qml` in the Library sidebar shows page and review
counts and opens the review window (write a page, check sources, approve or
reject proposals with their diff, inspect claims and citations, verify or
retract, restore revisions, cancel or retry jobs).

## Tests

`tests/core_knowledge_test.cpp` (scripted model and embeddings, no network):
text helpers, the migration, generation → proposal → approval, citation
validation, relations and contradictions, immutable revisions and restore,
rejection and manual edits, reconciliation through re-indexing, moves,
renames, modification and deletion, repeated processing without duplicates or
confidence inflation, interrupted and cancelled jobs, provider failures and
offline behaviour, Library isolation, retrieval into Context Assembly v3,
merge and deprecation, and the app service.

## Limitations

- Generation is one model call per page; very large topics are cut to
  `maxPassages` passages.
- Contradiction detection is limited to what the model reports with evidence
  in a single page pass; no cross-page scan yet (reserved for a librarian).
- Reconciliation runs when a person starts it; it does not run after every
  Library scan.
- The review window is deliberately minimal (no inline page editor; manual
  edits go through `Wiki.editPage`).
