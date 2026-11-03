#pragma once

#include "knowledge_generation.h"
#include "knowledge_model.h"

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
// Jobs are started by a person (explicit, user-initiated processing). The
// job types reserved for autonomous librarians are accepted by the schema
// but refused here until a librarian implements them (see KNOWLEDGE.md).
namespace KnowledgeJobs
{
// Job types this build can run.
[[nodiscard]] bool isRunnable(const QString &jobType);

// The queued or running job for the same request, or a new queued one.
// Throws for job types that are not implemented yet.
QVariantMap enqueue(CoreDatabase &database, const QString &libraryId, const QString &jobType, const QVariantMap &request, const QString &requestedBy = QStringLiteral("user"));
// {id, libraryId, jobType, status, stage, request, requestKey,
//  sourceFingerprint, progress, attemptCount, maxAttempts, errorCode,
//  errorMessage, result, requestedBy, model, nextAttemptAt, createdAt,
//  startedAt, updatedAt, completedAt}
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
// Queued jobs due at `now`, oldest first.
[[nodiscard]] QList<QVariantMap> runnable(CoreDatabase &database, const QDateTime &now);
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
};

// Works through the queue one job at a time on the thread that owns
// `database`. Model calls are asynchronous, so the thread's event loop stays
// free while a page is being written.
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
    [[nodiscard]] bool idle() const;
    [[nodiscard]] QString activeJobId() const { return m_activeJobId; }

signals:
    void jobChanged(const QString &libraryId, const QString &jobId);
    // Pages, claims or proposals of the Library changed.
    void knowledgeChanged(const QString &libraryId);

private:
    void schedule(int delayMs = 0);
    void processNext();
    void runReconcile(const QVariantMap &job);
    void runGeneration(const QVariantMap &job);
    void continueGeneration(const QString &jobId, const QVector<float> &queryVector, const QString &semanticNote);
    void onModelResult(const QString &jobId, const ModelResult &result);
    void failOrRetry(const QString &jobId, const QString &code, const QString &message, bool retryable, int retryAfterMs = 0);
    void finishActive();
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
    bool m_started = false;
    // The active generation job's request and evidence, between steps.
    KnowledgeGeneration::Request m_request;
    KnowledgeGeneration::Evidence m_evidence;
};
}
