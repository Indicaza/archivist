#pragma once

#include "Core/Database/core_database.h"
#include "embedding_client.h"
#include "embedding_config.h"
#include "embedding_index.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>

// The persistent, bounded background indexer for Library chunks.
//
// State lives in SQLite, not in memory: a job row per Library says what the
// indexer is doing, and the work left is always "indexed chunks without a
// current source row and vector", recomputed from the tables. So a crash, a
// quit or a cancelled request loses at most the batch in flight, and restart
// recovery is just "running jobs go back to queued".
//
// Lives on one thread with its own database connection; every method must be
// called on that thread. Requests are asynchronous, so the thread's event
// loop keeps running while OpenAI answers.
namespace Retrieval
{
struct IndexerPolicy
{
    int batchSize = 64;              // inputs per request (OpenAI allows 2048)
    int maxBatchTokens = 60000;      // estimated tokens per request (OpenAI allows 300k)
    int maxConcurrentRequests = 2;   // across Libraries; at most one per Library
    int minRequestIntervalMs = 250;  // between request starts, all Libraries
    int maxAttempts = 6;             // consecutive failures before a job fails
    int baseBackoffMs = 2000;
    int maxBackoffMs = 5 * 60 * 1000;
    qint64 maxJobTokens = 5000000;      // one job pauses past this (≈ $0.10 with text-embedding-3-small)
    qint64 dailyTokenBudget = 20000000; // every Library together, per UTC day (≈ $0.40)
    // A failed job is not restarted by the periodic sync before this long;
    // Resume restarts it at once.
    int failedRetryDelayMs = 30 * 60 * 1000;
};

class EmbeddingIndexer final : public QObject
{
    Q_OBJECT

public:
    using ClientFactory = std::function<std::unique_ptr<EmbeddingClient>()>;

    EmbeddingIndexer(CoreDatabase &database, ClientFactory clientFactory, IndexerPolicy policy = {}, EmbeddingConfig config = currentEmbeddingConfig(), QObject *parent = nullptr);
    ~EmbeddingIndexer() override;

    // For tests: what "now" is when stamping retries and usage.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    [[nodiscard]] const IndexerPolicy &policy() const { return m_policy; }
    [[nodiscard]] const EmbeddingConfig &config() const { return m_config; }

    // Puts jobs a previous process left running back in the queue and starts
    // working. Returns how many were recovered.
    int recover();

    // Consent. Enabling queues the first job; disabling stops work and
    // forgets every vector source of the Library.
    void enable(const QString &libraryId);
    void disable(const QString &libraryId);

    // Pause keeps the job and stops sending. Resume clears a pause (by the
    // user, a budget, a missing or refused key) or a failure and continues.
    // Cancel ends the job and keeps the Library paused until Resume.
    void pause(const QString &libraryId);
    void resume(const QString &libraryId);
    void cancel(const QString &libraryId);

    // Removes sources whose chunks are gone and queues a job when anything
    // needs (re)indexing. Cheap when nothing changed; call it after scans
    // and on a timer.
    void sync(const QString &libraryId);
    void syncAll();

    [[nodiscard]] bool idle() const;
    [[nodiscard]] int activeRequests() const { return static_cast<int>(m_active.size()); }

signals:
    // Something about this Library's index or job changed.
    void libraryChanged(const QString &libraryId);
    // No request is in flight and no runnable job is waiting.
    void becameIdle();

private:
    struct Batch
    {
        QString jobId;
        QString libraryId;
        QList<EmbeddingIndex::SourceText> sources; // every pending source the request covers
        QStringList hashes;                        // distinct hashes sent, in input order
        qint64 estimatedTokens = 0;
        std::unique_ptr<EmbeddingClient> client;
    };

    void scheduleTick(int delayMs = 0);
    void tick();
    // Advances one job as far as it can without a request; returns true when
    // a request was started.
    bool advance(const QVariantMap &job);
    void handleResult(const QString &libraryId, const EmbeddingResult &result);
    void stopRequest(const QString &libraryId);
    void complete(const QString &jobId, const QString &libraryId);
    void pauseJob(const QString &jobId, const QString &libraryId, const QString &reason, const QString &message);
    [[nodiscard]] int backoffMs(int attempt, int retryAfterMs) const;
    [[nodiscard]] QDateTime now() const { return m_clock ? m_clock() : QDateTime::currentDateTimeUtc(); }

    CoreDatabase &m_database;
    ClientFactory m_clientFactory;
    IndexerPolicy m_policy;
    EmbeddingConfig m_config;
    std::function<QDateTime()> m_clock;
    QTimer m_timer;
    QHash<QString, std::shared_ptr<Batch>> m_active; // by Library id
    QDateTime m_lastRequestAt;
    bool m_ticking = false;
    bool m_wasBusy = false;
};
}
