#pragma once

#include "knowledge_generation.h"
#include "knowledge_model.h"
#include "librarian_analysis.h"
#include "librarian_types.h"

#include "Core/Database/core_database.h"
#include "Core/Domains/Retrieval/embedding_client.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>

// The durable Wiki job queue and the runner that works through it.
//
// A job row is the whole truth about a piece of Wiki work: what was asked,
// which sources it read (source fingerprint), how far it got, how often it
// tried and why it failed. The runner keeps nothing that a restart would
// lose: jobs left running come back as queued, and the same request with the
// same sources never produces a second proposal.
//
// Jobs come from a person (writing a page, checking sources) or from a
// librarian (see LIBRARIAN.md): librarian jobs carry the librarian's name,
// the run they belong to and a lower priority, and follow LibrarianPolicy.
// `suggest-improvements` and `process-correction` stay reserved.
namespace KnowledgeJobs
{
// Job types this build can run.
[[nodiscard]] bool isRunnable(const QString &jobType);

struct JobOptions
{
    QString requestedBy = QStringLiteral("user");
    QString librarian; // empty: a person's job
    QString runId;     // the librarian run it belongs to
    int priority = Librarian::Priority::user;
};

// The queued or running job for the same request, or a new queued one
// (`created` says which). Throws for job types that are not implemented.
QVariantMap enqueue(CoreDatabase &database, const QString &libraryId, const QString &jobType, const QVariantMap &request, const QString &requestedBy = QStringLiteral("user"));
QVariantMap enqueue(CoreDatabase &database, const QString &libraryId, const QString &jobType, const QVariantMap &request, const JobOptions &options);
// {id, libraryId, jobType, status, stage, request, requestKey,
//  sourceFingerprint, progress, attemptCount, maxAttempts, errorCode,
//  errorMessage, result, requestedBy, model, nextAttemptAt, createdAt,
//  startedAt, updatedAt, completedAt, priority, librarian, runId}
[[nodiscard]] std::optional<QVariantMap> job(CoreDatabase &database, const QString &jobId);
// Newest first.
[[nodiscard]] QVariantList jobs(CoreDatabase &database, const QString &libraryId, int limit = 50);
// Sets the given columns (camelCase keys) and updated_at.
void update(CoreDatabase &database, const QString &jobId, const QVariantMap &fields);
// Running jobs (the app quit or crashed mid-request) go back to queued.
int recoverInterrupted(CoreDatabase &database);
// Queued or running -> cancelled. False when the job had already finished.
bool cancel(CoreDatabase &database, const QString &jobId);
// Failed or cancelled -> queued with a fresh attempt budget.
bool retry(CoreDatabase &database, const QString &jobId);
// Queued jobs due at `now`, highest priority first, then oldest first.
[[nodiscard]] QList<QVariantMap> runnable(CoreDatabase &database, const QDateTime &now);
// The jobs of a librarian run, oldest first.
[[nodiscard]] QVariantList runJobs(CoreDatabase &database, const QString &runId);
[[nodiscard]] QDateTime nextAttempt(CoreDatabase &database);
}

namespace Knowledge
{
struct RunnerPolicy
{
    int maxAttempts = 3;
    int baseBackoffMs = 2000;
    int maxBackoffMs = 60000;
    int embedTimeoutMs = 8000;
    // How soon to look again when every due job is held back (librarians
    // paused, in backoff or waiting for a chat to finish).
    int holdRecheckMs = 5000;
    // Claims one verification pass may send to the model.
    int maxVerificationClaims = 6;
};

// Works through the queue one job at a time on the thread that owns
// `database`. Model calls are asynchronous, so the thread's event loop stays
// free while a page is being written. One job at a time is also the
// librarians' concurrency bound: their jobs queue behind a person's and are
// held back entirely while a chat is streaming.
class KnowledgeJobRunner final : public QObject
{
    Q_OBJECT

public:
    using ModelFactory = std::function<std::unique_ptr<KnowledgeModel>()>;
    using EmbedderFactory = std::function<std::unique_ptr<Retrieval::EmbeddingClient>()>;

    KnowledgeJobRunner(CoreDatabase &database, ModelFactory models, RunnerPolicy policy = {}, QObject *parent = nullptr);
    ~KnowledgeJobRunner() override;

    // Optional: embeds the topic so evidence selection can use hybrid
    // search on Libraries that opted in to semantic indexing.
    void setEmbedder(EmbedderFactory embedder);
    // Overridable clock for tests.
    void setClock(std::function<QDateTime()> clock);

    // Re-queues interrupted jobs and starts working.
    void start();
    // Looks for work now (after enqueue, retry).
    void poke();
    // Cancels the job; drops its model request if it is the one running.
    void cancelJob(const QString &jobId);
    // A chat is streaming: librarian jobs wait (a person's jobs do not).
    void setInteractiveBusy(bool busy);
    [[nodiscard]] bool interactiveBusy() const { return m_interactiveBusy; }
    // The Library's librarians were paused or disabled: if one of their
    // jobs is running, its request is dropped and the job queued again.
    void suspendLibrarians(const QString &libraryId);
    [[nodiscard]] bool idle() const;
    [[nodiscard]] QString activeJobId() const { return m_activeJobId; }

signals:
    void jobChanged(const QString &libraryId, const QString &jobId);
    // Pages, claims or proposals of the Library changed.
    void knowledgeChanged(const QString &libraryId);
    // A librarian run finished (all its jobs are done).
    void runFinished(const QString &libraryId, const QString &runId);

private:
    void schedule(int delayMs = 0);
    void processNext();
    void runReconcile(const QVariantMap &job);
    void runGeneration(const QVariantMap &job);
    void continueGeneration(const QString &jobId, const QVector<float> &queryVector, const QString &semanticNote);
    void onModelResult(const QString &jobId, const ModelResult &result);
    void failOrRetry(const QString &jobId, const QString &code, const QString &message, bool retryable, int retryAfterMs = 0);
    void finishActive();

    // Librarian jobs (knowledge_jobs_librarian.cpp).
    void runMaintenance(const QVariantMap &job);
    void runDiscovery(const QVariantMap &job);
    void runVerification(const QVariantMap &job);
    void onVerificationResult(const QString &jobId, const ModelResult &result);
    [[nodiscard]] LibrarianAnalysis::Context librarianContext(const QVariantMap &job);
    // Marks the job running; false when it could not be.
    bool markRunning(const QVariantMap &job, const QString &stage);
    void completeJob(const QString &jobId, const QVariantMap &result, const QString &stage = QStringLiteral("done"));
    // Usage, provider health and the run's counters after a model reply.
    void accountModelCall(const ModelResult &result, const QString &model);
    void settleRunOf(const QString &libraryId, const QString &runId);
    [[nodiscard]] bool stillRunning(const QString &jobId);
    [[nodiscard]] QDateTime now() const;

    CoreDatabase &m_database;
    ModelFactory m_models;
    EmbedderFactory m_embedders;
    RunnerPolicy m_policy;
    std::function<QDateTime()> m_clock;
    std::unique_ptr<KnowledgeModel> m_model;
    std::unique_ptr<Retrieval::EmbeddingClient> m_embedder;
    QTimer m_timer;
    QTimer m_embedTimer;
    QString m_activeJobId;
    QString m_activeLibraryId;
    QString m_activeLibrarian;
    QString m_activeRunId;
    QString m_activeJobType;
    bool m_started = false;
    bool m_interactiveBusy = false;
    LibrarianAnalysis::Context m_context;
    LibrarianAnalysis::VerificationPlan m_plan;
    QString m_verificationModel;
    // The active generation job's request and evidence, between steps.
    KnowledgeGeneration::Request m_request;
    KnowledgeGeneration::Evidence m_evidence;
};
}
