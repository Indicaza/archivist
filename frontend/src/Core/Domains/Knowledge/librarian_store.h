#pragma once

#include "Core/Database/core_database.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Every SQL statement over the librarians' operational memory (migration
// 24): per-Library settings and consent, the file snapshot maintenance
// compares against, runs and their model usage, research topics, findings
// and daily usage. Plain functions on one connection, like the other
// Knowledge repositories; writers join the caller's transaction.
//
// This is research bookkeeping for the Wiki of one Library, not memory
// about the person. Nothing here changes a Wiki page.
namespace LibrarianStore
{
// ---- Settings ----------------------------------------------------------------

struct Settings
{
    QString libraryId;
    bool enabled = false;
    QString consentedAt;  // set: the person agreed to send Library text to OpenAI for librarian work
    QString mode = QStringLiteral("manual");
    int scheduleMinutes = 1440;
    int idleMinutes = 10;
    bool maintenance = true;
    bool research = true;
    bool verification = true;
    bool paused = false;
    QString model;
    int maxCallsPerRun = 4;
    int maxTokensPerRun = 60000;
    int dailyCallBudget = 20;
    int dailyTokenBudget = 250000;
    int maxDepth = 2;
    QString changeNotedAt;
    QString lastMaintenanceAt;
    QString lastResearchAt;
    QString lastVerificationAt;
    QString nextScheduledAt;
    int consecutiveFailures = 0;
    QString backoffUntil;
    QString lastError;

    [[nodiscard]] bool consented() const { return !consentedAt.isEmpty(); }
    [[nodiscard]] bool allows(const QString &librarian) const;
    [[nodiscard]] QString effectiveModel() const;
    [[nodiscard]] QString lastRunAt(const QString &librarian) const;
    [[nodiscard]] QVariantMap toVariant() const;
};

// The Library's settings; defaults (disabled) when it never had any.
[[nodiscard]] Settings settings(CoreDatabase &database, const QString &libraryId);
// Applies the given camelCase keys (enabled, mode, scheduleMinutes,
// idleMinutes, maintenance, research, verification, paused, model,
// maxCallsPerRun, maxTokensPerRun, dailyCallBudget, dailyTokenBudget,
// maxDepth). Values are validated and clamped; unknown keys throw.
Settings updateSettings(CoreDatabase &database, const QString &libraryId, const QVariantMap &changes);
// Consent to send Library text to OpenAI for librarian research. Revoking
// it stops further model calls at once; deterministic work continues.
void setConsent(CoreDatabase &database, const QString &libraryId, bool consented);
// Libraries whose librarians are enabled.
[[nodiscard]] QStringList enabledLibraries(CoreDatabase &database);
void noteChange(CoreDatabase &database, const QString &libraryId, const QDateTime &when);
void clearChange(CoreDatabase &database, const QString &libraryId);
void markRan(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QDateTime &when);
void setNextScheduled(CoreDatabase &database, const QString &libraryId, const QDateTime &when);
// Provider health for backoff: failures push `backoffUntil` out
// exponentially (1 min doubling to 1 h); a success clears it.
void recordProviderFailure(CoreDatabase &database, const QString &libraryId, const QString &message, const QDateTime &now);
void recordProviderSuccess(CoreDatabase &database, const QString &libraryId);

// ---- Source snapshot -------------------------------------------------------

// How the Library's files differ from what maintenance last saw.
struct SourceChanges
{
    QStringList added;    // newly indexed (relative paths)
    QStringList modified; // content changed
    QStringList moved;    // same file, new path ("old -> new")
    QStringList deleted;  // gone, unreadable or no longer indexed
    QStringList addedFileIds;
    QStringList changedFileIds; // added + modified + moved

    [[nodiscard]] bool any() const { return !added.isEmpty() || !modified.isEmpty() || !moved.isEmpty() || !deleted.isEmpty(); }
    [[nodiscard]] QVariantMap toVariant() const;
};

// Compares the Library's indexed files with the snapshot. A Library that
// never had a snapshot reports every indexed file as added.
[[nodiscard]] SourceChanges diffSources(CoreDatabase &database, const QString &libraryId);
// Records the current files as the snapshot.
void saveSnapshot(CoreDatabase &database, const QString &libraryId);

// ---- Runs --------------------------------------------------------------------

struct RunStart
{
    QVariantMap run;
    bool created = false; // false: that librarian already had a running run
};

RunStart startRun(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request = {});
// {id, libraryId, librarian, trigger, status, request, modelCalls,
//  inputTokens, outputTokens, estimatedCostUsd, jobsCreated, findingsCreated,
//  proposalsCreated, summary, errorMessage, startedAt, updatedAt, completedAt,
//  activeJobs}
[[nodiscard]] std::optional<QVariantMap> run(CoreDatabase &database, const QString &runId);
[[nodiscard]] std::optional<QVariantMap> activeRun(CoreDatabase &database, const QString &libraryId, const QString &librarian);
// Newest first.
[[nodiscard]] QVariantList runs(CoreDatabase &database, const QString &libraryId, int limit = 20);
void addRunUsage(CoreDatabase &database, const QString &runId, int calls, int inputTokens, int outputTokens, double costUsd);
// Adds to jobs_created, findings_created or proposals_created.
void countRun(CoreDatabase &database, const QString &runId, const QString &counter, int amount = 1);
// Merges `fields` into the run's summary.
void noteRun(CoreDatabase &database, const QString &runId, const QVariantMap &fields);
void finishRun(CoreDatabase &database, const QString &runId, const QString &status, const QString &errorMessage = {});
// Finishes a running run whose jobs are all done: completed, or failed when
// every job failed, or cancelled when they were cancelled. True when it
// finished now.
bool settleRun(CoreDatabase &database, const QString &runId);
// settleRun for every running run (after a restart).
QStringList settleRuns(CoreDatabase &database);

// ---- Research topics -------------------------------------------------------

struct TopicDraft
{
    QString libraryId;
    QString topic;
    QString origin;
    QVariantMap scope;           // {directory, fileIds}
    int depth = 0;
    QString parentTopicId;
    QString evidenceFingerprint; // the sources as they are now
    QString pageId;
};

// Finds the topic with the same key or creates it. When the evidence
// fingerprint differs from the one it was investigated against, a covered,
// proposed, dismissed or exhausted topic becomes needs-revisit.
QVariantMap upsertTopic(CoreDatabase &database, const TopicDraft &draft);
// {id, libraryId, topic, topicKey, origin, scope, depth, parentTopicId,
//  status, pageId, evidenceFingerprint, investigatedFingerprint, findings,
//  openQuestions, lastJobId, lastProposalId, attempts, investigatedAt, ...}
[[nodiscard]] std::optional<QVariantMap> topic(CoreDatabase &database, const QString &topicId);
// `status` empty: all. Most recently updated first.
[[nodiscard]] QVariantList topics(CoreDatabase &database, const QString &libraryId, const QString &status = {}, int limit = 100);
// Sets the given camelCase fields (status, pageId, investigatedFingerprint,
// findings, openQuestions, lastJobId, lastProposalId, attempts,
// investigatedAt).
void updateTopic(CoreDatabase &database, const QString &topicId, const QVariantMap &fields);
[[nodiscard]] QString topicKey(const QString &topic);

// ---- Findings ----------------------------------------------------------------

struct FindingDraft
{
    QString libraryId;
    QString librarian;
    QString kind;
    QString title;
    QString detail;
    QString pageId;
    QString claimId;
    QString relatedClaimId;
    QVariantList evidence;
    QString proposalId;
    QString runId;
    QString jobId;
    QStringList identity; // what makes two findings "the same"; hashed with kind
};

struct FindingWrite
{
    QString findingId;
    bool created = false;
};

// Records a finding once. The same finding again (same identity) is not
// duplicated: an open one picks up a new proposal; a dismissed or resolved
// one stays as the person left it, so a question already answered is not
// asked again.
FindingWrite recordFinding(CoreDatabase &database, const FindingDraft &draft);
// Points the open finding with this kind and identity at a proposal that
// addresses it (it becomes "proposed"). False when there was none.
bool linkFinding(CoreDatabase &database, const QString &libraryId, const QString &kind, const QStringList &identity, const QString &proposalId);
// {id, libraryId, librarian, kind, status, title, detail, pageId, pageTitle,
//  claimId, claimStatement, relatedClaimId, evidence, proposalId,
//  proposalStatus, runId, jobId, createdAt, updatedAt, resolvedAt}.
// `status` empty: open and proposed; "all": every status.
[[nodiscard]] QVariantList findings(CoreDatabase &database, const QString &libraryId, const QString &status = {}, int limit = 100);
// "dismissed" or "open" (reopen). False when the finding does not exist.
bool setFindingStatus(CoreDatabase &database, const QString &findingId, const QString &status);
// Pending proposals a librarian made (its jobs, or a deterministic
// librarian proposal), so the review list can show where they came from.
[[nodiscard]] QStringList librarianProposalIds(CoreDatabase &database, const QString &libraryId);
// Brings findings and topics in line with what happened to their
// proposals: applied -> resolved/covered, rejected -> dismissed,
// superseded or conflicted -> open again.
void syncWithProposals(CoreDatabase &database, const QString &libraryId);

// ---- Usage -------------------------------------------------------------------

struct Usage
{
    int calls = 0;
    int inputTokens = 0;
    int outputTokens = 0;
    double costUsd = 0;

    [[nodiscard]] int tokens() const { return inputTokens + outputTokens; }
    [[nodiscard]] QVariantMap toVariant() const;
};

[[nodiscard]] QString usageDay(const QDateTime &when);
[[nodiscard]] Usage dailyUsage(CoreDatabase &database, const QString &libraryId, const QString &day);
void addUsage(CoreDatabase &database, const QString &libraryId, const QString &day, const Usage &usage);
// {today: Usage, last30Days: Usage}
[[nodiscard]] QVariantMap usageSummary(CoreDatabase &database, const QString &libraryId, const QDateTime &now);
}
