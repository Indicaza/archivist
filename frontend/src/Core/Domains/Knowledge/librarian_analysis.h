#pragma once

#include "knowledge_model.h"
#include "knowledge_sources.h"
#include "librarian_store.h"

#include "Core/Database/core_database.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// What the three librarians actually work out. Everything here is a plain
// function over the database, deterministic and local, except that
// verification prepares one bounded model request and validates its reply.
// The job runner decides when these run and owns every model call.
//
//   Maintenance   maintain()           diff sources, reconcile citations, stale-claim
//                                      findings, deprecation proposals, pages to refresh
//   Research      discover()           survey folders and open questions, check Wiki
//                                      coverage, pick a few topics to investigate
//                 recordInvestigation() remember what an investigation produced
//   Verification  planVerification()   unsupported claims, open assumptions, possible
//                                      contradictions, claims whose evidence changed
//                 applyVerification()  validate verdicts; findings and corrections
//
// Nothing here edits a page: changes are proposals a person approves.
// Model output and Library text are data, never instructions.
namespace LibrarianAnalysis
{
struct Context
{
    QString libraryId;
    QString runId;
    QString jobId;
    LibrarianStore::Settings settings;
    // The person consented and the budgets allow model work in this run.
    bool mayUseModel = false;
};

// ---- Maintenance -----------------------------------------------------------

struct Maintenance
{
    LibrarianStore::SourceChanges changes;
    KnowledgeSources::ReconcileReport report;
    int findings = 0;
    int proposals = 0;
    // Pages with claims that went stale because a cited file changed (not
    // vanished): worth a refresh from the current sources.
    QStringList refreshPageIds;

    [[nodiscard]] QVariantMap toVariant() const;
};

// Compares the Library with the last snapshot, re-checks every citation
// (KnowledgeSources::reconcile), records stale-citation findings, proposes
// deprecating AI pages whose sources are all gone (a person's pages only get
// a finding), lists pages worth refreshing and saves the new snapshot.
Maintenance maintain(CoreDatabase &database, const Context &context);

// ---- Research --------------------------------------------------------------

struct Subject
{
    QString topicId;
    QString topic;
    QString origin;
    QStringList fileIds;    // sources to read first (folder subjects)
    QString targetPageId;   // the page that already covers it, to expand
    int depth = 0;
    double coverage = 0;    // share of its files the Wiki cites
    int chunks = 0;
    QString fingerprint;    // its evidence as it is now
};

struct Discovery
{
    QList<Subject> selected; // to investigate now, best first
    int surveyed = 0;
    int uncovered = 0;
    int covered = 0;
    int unchanged = 0;       // skipped: evidence unchanged since investigated
    int findings = 0;

    [[nodiscard]] QVariantMap toVariant() const;
};

// Surveys the Library's folders (and open research questions, and the
// person's `manualTopic`), checks how much of each the Wiki already cites,
// remembers each as a topic with an evidence fingerprint, records
// coverage-gap findings and picks at most `maxTopics` topics whose evidence
// is new or changed. Topics already investigated against the same evidence
// are skipped without any model call.
Discovery discover(CoreDatabase &database, const Context &context, const QString &manualTopic, int maxTopics);

// After an investigation job: the topic remembers the proposal (or that the
// Wiki already reflects these sources), the evidence it was investigated
// against and its open questions; questions become child topics up to
// `maxDepth`. `jobResult` is the job's result map.
void recordInvestigation(CoreDatabase &database, const QString &topicId, const QString &jobId, const QVariantMap &jobResult, int maxDepth);
// An investigation found nothing citable: the topic is exhausted until its
// evidence changes.
void recordExhausted(CoreDatabase &database, const QString &topicId, const QString &jobId, const QString &reason);

// ---- Verification ----------------------------------------------------------

struct ClaimCheck
{
    QString label;      // C1, C2, ...
    QString claimId;
    QString pageId;
    QString pageTitle;
    QString claimType;
    QString statement;
    QString origin;
    QString verificationStatus;
    QString fingerprint; // statement + the passages it was checked against
    QStringList passageLabels;
};

struct VerificationPlan
{
    QList<ClaimCheck> checks;
    QList<KnowledgeSources::Passage> passages; // P1..Pn
    QList<QPair<QString, QString>> conflicts;  // claim labels that may contradict each other
    int findings = 0;
    int unchanged = 0;  // claims skipped: checked against the same evidence before
    int candidates = 0; // possible contradictions found locally

    [[nodiscard]] bool needsModel() const { return !checks.isEmpty(); }
    [[nodiscard]] QVariantMap toVariant() const;
};

// Records deterministic findings (evidence-requiring claims without a
// citation, open assumptions, and, without model access, possible
// contradictions where two similar claims state different numbers) and
// selects at most `maxClaims` AI claims to check against original passages
// gathered through the Context Assembly retrieval contract.
VerificationPlan planVerification(CoreDatabase &database, const Context &context, int maxClaims);

[[nodiscard]] Knowledge::ModelRequest verificationPrompt(const VerificationPlan &plan, const QString &model);
[[nodiscard]] QJsonObject verificationSchema();

struct VerificationOutcome
{
    int supported = 0;
    int contradicted = 0;
    int insufficient = 0;
    int conflicts = 0;
    int findings = 0;
    int proposals = 0;
    QVariantList dropped; // verdicts or citations that failed validation

    [[nodiscard]] QVariantMap toVariant() const;
};

// Validates the model's verdicts against the passages it was shown. A
// contradiction counts only with a citation that checks out; it becomes a
// finding and, for the page, one correction proposal (AI claims are
// retracted and replaced; a person's or verified claim is never retracted,
// only marked as contradicted so the conflict is visible). "Supported" never
// marks a claim verified: only a person does that. Throws CoreError
// {code: "WIKI_INVALID_RESPONSE"} when the reply is unusable.
VerificationOutcome applyVerification(CoreDatabase &database, const Context &context, const VerificationPlan &plan, const QString &responseText, const QString &modelId);
}
