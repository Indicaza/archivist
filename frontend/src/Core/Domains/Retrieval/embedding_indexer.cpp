#include "embedding_indexer.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QDebug>
#include <QLocale>
#include <algorithm>
#include <cmath>

using namespace EmbeddingIndex;

namespace
{
const QString libraryScope = Retrieval::ScopeType::library;

QVariant null()
{
    return QVariant::fromValue(nullptr);
}

QString errorText(const std::exception &error)
{
    if (const auto *coreError = dynamic_cast<const CoreError *>(&error)) {
        return coreError->message();
    }

    return QString::fromUtf8(error.what());
}

// Keeps a finished client alive until the event loop is back at the top, so
// it is never destroyed from inside its own callback.
void retire(std::unique_ptr<Retrieval::EmbeddingClient> client, QObject *context)
{
    if (!client) {
        return;
    }

    std::shared_ptr<Retrieval::EmbeddingClient> holder(std::move(client));
    QTimer::singleShot(0, context, [holder]() {});
}
}

namespace Retrieval
{
EmbeddingIndexer::EmbeddingIndexer(CoreDatabase &database, ClientFactory clientFactory, IndexerPolicy policy, EmbeddingConfig config, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_clientFactory(std::move(clientFactory))
    , m_policy(policy)
    , m_config(std::move(config))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &EmbeddingIndexer::tick);
}

EmbeddingIndexer::~EmbeddingIndexer()
{
    for (const auto &batch : std::as_const(m_active)) {
        if (batch->client) {
            batch->client->cancel();
        }
    }
}

int EmbeddingIndexer::recover()
{
    const int recovered = m_database.transaction([this]() { return recoverInterruptedJobs(m_database); });

    if (recovered > 0) {
        qInfo() << "[Embeddings] Re-queued" << recovered << "indexing job(s) left running by a previous process.";
    }

    syncAll();
    scheduleTick();
    return recovered;
}

void EmbeddingIndexer::enable(const QString &libraryId)
{
    m_database.transaction([&]() { setLibraryEnabled(m_database, libraryId, true); });
    emit libraryChanged(libraryId);
    sync(libraryId);
}

void EmbeddingIndexer::disable(const QString &libraryId)
{
    stopRequest(libraryId);
    m_database.transaction([&]() {
        if (const auto job = activeJob(m_database, libraryScope, libraryId)) {
            updateJob(m_database, job->value(QStringLiteral("id")).toString(), {
                {QStringLiteral("status"), JobStatus::cancelled},
                {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
                {QStringLiteral("errorMessage"), null()},
            });
        }

        setLibraryEnabled(m_database, libraryId, false);
        removeScope(m_database, libraryScope, libraryId);
        collectGarbage(m_database, m_config);
    });
    emit libraryChanged(libraryId);
    scheduleTick();
}

void EmbeddingIndexer::pause(const QString &libraryId)
{
    stopRequest(libraryId);
    m_database.transaction([&]() {
        setLibraryPaused(m_database, libraryId, true);

        if (const auto job = activeJob(m_database, libraryScope, libraryId)) {
            updateJob(m_database, job->value(QStringLiteral("id")).toString(), {
                {QStringLiteral("status"), JobStatus::paused},
                {QStringLiteral("pauseReason"), PauseReason::user},
                {QStringLiteral("nextAttemptAt"), null()},
            });
        }
    });
    emit libraryChanged(libraryId);
    scheduleTick();
}

void EmbeddingIndexer::resume(const QString &libraryId)
{
    if (!isLibraryEnabled(m_database, libraryId)) {
        return;
    }

    m_database.transaction([&]() {
        setLibraryPaused(m_database, libraryId, false);
        const QVariantMap job = ensureJob(m_database, libraryScope, libraryId, m_config);
        updateJob(m_database, job.value(QStringLiteral("id")).toString(), {
            {QStringLiteral("status"), JobStatus::queued},
            {QStringLiteral("pauseReason"), null()},
            {QStringLiteral("errorMessage"), null()},
            {QStringLiteral("nextAttemptAt"), null()},
            {QStringLiteral("attemptCount"), 0},
        });
    });
    emit libraryChanged(libraryId);
    scheduleTick();
}

void EmbeddingIndexer::cancel(const QString &libraryId)
{
    stopRequest(libraryId);
    m_database.transaction([&]() {
        setLibraryPaused(m_database, libraryId, true);

        if (const auto job = activeJob(m_database, libraryScope, libraryId)) {
            updateJob(m_database, job->value(QStringLiteral("id")).toString(), {
                {QStringLiteral("status"), JobStatus::cancelled},
                {QStringLiteral("pauseReason"), null()},
                {QStringLiteral("nextAttemptAt"), null()},
                {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
            });
        }
    });
    emit libraryChanged(libraryId);
    scheduleTick();
}

void EmbeddingIndexer::sync(const QString &libraryId)
{
    try {
        const QVariantMap settings = librarySettings(m_database, libraryId);

        if (!settings.value(QStringLiteral("enabled")).toBool() || !enabledLibraries(m_database).contains(libraryId)) {
            return; // not opted in, archived or deleted
        }

        bool changed = false;
        m_database.transaction([&]() {
            // Chunk deletions remove their source rows through a trigger; this
            // catches the rest (unavailable files, moved chunks) and then drops
            // vectors nothing points at any more. Both are index lookups.
            const int removed = removeStaleLibrarySources(m_database, libraryId);
            const int collected = collectGarbage(m_database, m_config);

            if (removed > 0 || collected > 0) {
                changed = true;
            }

            if (settings.value(QStringLiteral("paused")).toBool() || activeJob(m_database, libraryScope, libraryId)) {
                return;
            }

            if (pendingLibrarySummary(m_database, libraryId, m_config).items == 0) {
                return;
            }

            const auto latest = latestJob(m_database, libraryScope, libraryId);

            if (latest && latest->value(QStringLiteral("status")).toString() == JobStatus::failed) {
                const QDateTime failedAt = QDateTime::fromString(latest->value(QStringLiteral("completedAt")).toString(), Qt::ISODateWithMs);

                if (failedAt.isValid() && failedAt.msecsTo(now()) < m_policy.failedRetryDelayMs) {
                    return;
                }
            }

            ensureJob(m_database, libraryScope, libraryId, m_config);
            changed = true;
        });

        if (changed) {
            emit libraryChanged(libraryId);
            scheduleTick();
        }
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Embeddings] Sync failed for a Library:" << errorText(error);
    }
}

void EmbeddingIndexer::syncAll()
{
    for (const QString &libraryId : enabledLibraries(m_database)) {
        sync(libraryId);
    }
}

bool EmbeddingIndexer::idle() const
{
    return m_active.isEmpty() && !m_timer.isActive() && !m_ticking;
}

void EmbeddingIndexer::scheduleTick(int delayMs)
{
    const int delay = std::max(0, delayMs);

    if (!m_timer.isActive() || m_timer.remainingTime() > delay) {
        m_timer.start(delay);
    }
}

int EmbeddingIndexer::backoffMs(int attempt, int retryAfterMs) const
{
    const double exponential = m_policy.baseBackoffMs * std::pow(2.0, std::max(0, attempt - 1));
    const int wait = static_cast<int>(std::min<double>(exponential, m_policy.maxBackoffMs));
    return std::min(m_policy.maxBackoffMs, std::max(wait, retryAfterMs));
}

void EmbeddingIndexer::tick()
{
    m_ticking = true;

    try {
        const QDateTime current = now();

        for (const QVariantMap &job : runnableJobs(m_database, current)) {
            if (m_active.size() >= m_policy.maxConcurrentRequests) {
                break;
            }

            if (m_active.contains(job.value(QStringLiteral("scopeId")).toString())) {
                continue;
            }

            // Rate limit: one request start per interval across Libraries.
            if (m_lastRequestAt.isValid()) {
                const qint64 since = m_lastRequestAt.msecsTo(QDateTime::currentDateTimeUtc());

                if (since < m_policy.minRequestIntervalMs) {
                    scheduleTick(static_cast<int>(m_policy.minRequestIntervalMs - since));
                    break;
                }
            }

            advance(job);
        }

        // Wake again for the earliest retry.
        const QDateTime next = nextQueuedAttempt(m_database);

        if (next.isValid()) {
            scheduleTick(static_cast<int>(std::clamp<qint64>(current.msecsTo(next), 0, m_policy.maxBackoffMs)));
        }
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Embeddings] Indexing tick failed:" << errorText(error);
        scheduleTick(m_policy.baseBackoffMs);
    }

    m_ticking = false;

    if (idle()) {
        emit becameIdle();
    }
}

bool EmbeddingIndexer::advance(const QVariantMap &startingJob)
{
    const QString jobId = startingJob.value(QStringLiteral("id")).toString();
    const QString libraryId = startingJob.value(QStringLiteral("scopeId")).toString();
    LiveEvents::Scope live(QStringLiteral("retrieval"), QStringLiteral("embedding-batch"));

    if (!isLibraryEnabled(m_database, libraryId)) {
        m_database.transaction([&]() {
            updateJob(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::cancelled},
                {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
            });
        });
        emit libraryChanged(libraryId);
        return false;
    }

    // Reuse what the cache already has, a few pages at a time, until a page
    // needs the API or nothing is left.
    for (int page = 0; page < 64; ++page) {
        QList<SourceText> pending;
        QList<SourceText> needsEmbedding;
        QVariantMap job;

        m_database.transaction([&]() {
            job = *EmbeddingIndex::job(m_database, jobId);
            QVariantMap fields;

            if (job.value(QStringLiteral("status")).toString() != JobStatus::running) {
                fields.insert(QStringLiteral("status"), JobStatus::running);
            }

            if (job.value(QStringLiteral("startedAt")).typeId() != QMetaType::QString) {
                // A fresh job: forget sources whose chunks are gone and size the work.
                const int removed = removeStaleLibrarySources(m_database, libraryId);
                const PendingSummary summary = pendingLibrarySummary(m_database, libraryId, m_config);
                fields.insert(QStringLiteral("startedAt"), JsCompat::isoTimestamp(now()));
                fields.insert(QStringLiteral("removedItems"), job.value(QStringLiteral("removedItems")).toInt() + removed);
                fields.insert(QStringLiteral("totalItems"), job.value(QStringLiteral("processedItems")).toInt() + summary.items);
            }

            pending = pendingLibrarySources(m_database, libraryId, m_config, m_policy.batchSize * 4);
            QStringList hashes;

            for (const SourceText &source : pending) {
                hashes.append(source.contentHash);
            }

            const QSet<QString> cached = existingVectors(m_database, hashes, m_config);
            int reused = 0;

            for (const SourceText &source : pending) {
                if (cached.contains(source.contentHash)) {
                    upsertSource(m_database, source);
                    ++reused;
                } else {
                    needsEmbedding.append(source);
                }
            }

            if (reused > 0) {
                fields.insert(QStringLiteral("reusedItems"), job.value(QStringLiteral("reusedItems")).toInt() + reused);
                fields.insert(QStringLiteral("processedItems"), job.value(QStringLiteral("processedItems")).toInt() + reused);
            }

            if (job.value(QStringLiteral("processedItems")).toInt() + reused > job.value(QStringLiteral("totalItems")).toInt()
                && !fields.contains(QStringLiteral("totalItems"))) {
                fields.insert(QStringLiteral("totalItems"), job.value(QStringLiteral("processedItems")).toInt() + reused);
            }

            if (!fields.isEmpty()) {
                updateJob(m_database, jobId, fields);
                job = *EmbeddingIndex::job(m_database, jobId);
            }
        });

        if (pending.isEmpty()) {
            complete(jobId, libraryId);
            return false;
        }

        if (needsEmbedding.isEmpty()) {
            emit libraryChanged(libraryId);
            continue;
        }

        // One request: distinct hashes up to the input and token limits. Every
        // pending source sharing a sent hash is covered by the answer.
        auto batch = std::make_shared<Batch>();
        batch->jobId = jobId;
        batch->libraryId = libraryId;
        QHash<QString, QString> textByHash;

        for (const SourceText &source : needsEmbedding) {
            if (textByHash.contains(source.contentHash)) {
                batch->sources.append(source);
                continue;
            }

            const int tokens = std::max(1, source.estimatedTokens);

            if (batch->hashes.size() >= m_policy.batchSize
                || (!batch->hashes.isEmpty() && batch->estimatedTokens + tokens > m_policy.maxBatchTokens)) {
                continue;
            }

            textByHash.insert(source.contentHash, source.content);
            batch->hashes.append(source.contentHash);
            batch->sources.append(source);
            batch->estimatedTokens += tokens;
        }

        // Spending safeguards, checked before anything is sent.
        const qint64 usedToday = tokensUsedOn(m_database, now());

        if (usedToday + batch->estimatedTokens > m_policy.dailyTokenBudget) {
            pauseJob(jobId, libraryId, PauseReason::budget,
                     QStringLiteral("Paused: today's embedding budget (%1 tokens for all Libraries) is used up. Resume tomorrow or raise the limit.")
                         .arg(QLocale(QLocale::English).toString(m_policy.dailyTokenBudget)));
            return false;
        }

        if (job.value(QStringLiteral("tokensUsed")).toLongLong() + batch->estimatedTokens > m_policy.maxJobTokens) {
            pauseJob(jobId, libraryId, PauseReason::budget,
                     QStringLiteral("Paused: this indexing job reached its limit of %1 tokens. Resume to continue.").arg(QLocale(QLocale::English).toString(m_policy.maxJobTokens)));
            return false;
        }

        QStringList inputs;

        for (const QString &hash : std::as_const(batch->hashes)) {
            inputs.append(textByHash.value(hash));
        }

        batch->client = m_clientFactory();
        m_active.insert(libraryId, batch);
        m_lastRequestAt = QDateTime::currentDateTimeUtc();
        live.setField(QStringLiteral("inputs"), inputs.size());
        live.setField(QStringLiteral("estimatedTokens"), batch->estimatedTokens);
        emit libraryChanged(libraryId);
        std::weak_ptr<Batch> weak = batch;
        batch->client->embed(inputs, m_config, [this, weak, libraryId](const EmbeddingResult &result) {
            const auto current = weak.lock();

            // Dropped by pause, cancel or disable while in flight.
            if (!current || m_active.value(libraryId) != current) {
                return;
            }

            handleResult(libraryId, result);
        });
        return true;
    }

    // Many pages of pure cache reuse: let the event loop breathe, then go on.
    scheduleTick();
    return false;
}

void EmbeddingIndexer::handleResult(const QString &libraryId, const EmbeddingResult &result)
{
    const std::shared_ptr<Batch> batch = m_active.take(libraryId);
    retire(std::move(batch->client), this);
    const QString jobId = batch->jobId;

    if (result.error == EmbeddingResult::Error::Cancelled) {
        scheduleTick();
        return;
    }

    try {
        if (result.ok()) {
            m_database.transaction([&]() {
                const QVariantMap job = *EmbeddingIndex::job(m_database, jobId);
                QHash<QString, bool> stored;

                for (qsizetype index = 0; index < batch->hashes.size(); ++index) {
                    storeVector(m_database, batch->hashes.at(index), m_config, result.vectors.at(index),
                                index == 0 ? result.promptTokens : 0);
                    stored.insert(batch->hashes.at(index), true);
                }

                // The chunk may have changed or vanished while the request was
                // out; only sources that are still pending with this hash are
                // associated. The rest are picked up by the next page.
                int associated = 0;

                for (const SourceText &source : std::as_const(batch->sources)) {
                    const QString currentHash = currentContentHash(m_database, source);

                    if (currentHash == source.contentHash && stored.contains(source.contentHash)) {
                        upsertSource(m_database, source);
                        ++associated;
                    }
                }

                const int tokens = result.promptTokens > 0 ? result.promptTokens : static_cast<int>(batch->estimatedTokens);
                addUsage(m_database, now(), tokens, 1);
                const int processed = job.value(QStringLiteral("processedItems")).toInt() + associated;
                updateJob(m_database, jobId, {
                    {QStringLiteral("processedItems"), processed},
                    {QStringLiteral("totalItems"), std::max(processed, job.value(QStringLiteral("totalItems")).toInt())},
                    {QStringLiteral("embeddedItems"), job.value(QStringLiteral("embeddedItems")).toInt() + associated},
                    {QStringLiteral("tokensUsed"), job.value(QStringLiteral("tokensUsed")).toLongLong() + tokens},
                    {QStringLiteral("requestCount"), job.value(QStringLiteral("requestCount")).toInt() + 1},
                    {QStringLiteral("attemptCount"), 0},
                    {QStringLiteral("errorMessage"), null()},
                });
            });
            emit libraryChanged(libraryId);
            scheduleTick();
            return;
        }

        if (result.error == EmbeddingResult::Error::NotConfigured) {
            pauseJob(jobId, libraryId, PauseReason::notConfigured, result.message);
            return;
        }

        if (result.error == EmbeddingResult::Error::Unauthorized) {
            pauseJob(jobId, libraryId, PauseReason::unauthorized,
                     QStringLiteral("OpenAI refused the API key (%1). Fix the key, then resume.").arg(result.message));
            return;
        }

        m_database.transaction([&]() {
            const QVariantMap job = *EmbeddingIndex::job(m_database, jobId);
            const int attempts = job.value(QStringLiteral("attemptCount")).toInt() + 1;

            if (!result.retryable() || attempts >= m_policy.maxAttempts) {
                updateJob(m_database, jobId, {
                    {QStringLiteral("status"), JobStatus::failed},
                    {QStringLiteral("attemptCount"), attempts},
                    {QStringLiteral("errorMessage"), result.message},
                    {QStringLiteral("nextAttemptAt"), null()},
                    {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
                });
                return;
            }

            updateJob(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::queued},
                {QStringLiteral("attemptCount"), attempts},
                {QStringLiteral("errorMessage"), result.message},
                {QStringLiteral("nextAttemptAt"), JsCompat::isoTimestamp(now().addMSecs(backoffMs(attempts, result.retryAfterMs)))},
            });
        });
        emit libraryChanged(libraryId);
        scheduleTick();
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Embeddings] Could not store an embedding batch:" << errorText(error);
        scheduleTick(m_policy.baseBackoffMs);
    }
}

void EmbeddingIndexer::stopRequest(const QString &libraryId)
{
    const std::shared_ptr<Batch> batch = m_active.take(libraryId);

    if (batch && batch->client) {
        batch->client->cancel();
        retire(std::move(batch->client), this);
    }
}

void EmbeddingIndexer::complete(const QString &jobId, const QString &libraryId)
{
    m_database.transaction([&]() {
        const int removed = removeStaleLibrarySources(m_database, libraryId);
        const QVariantMap job = *EmbeddingIndex::job(m_database, jobId);
        collectGarbage(m_database, m_config);
        updateJob(m_database, jobId, {
            {QStringLiteral("status"), JobStatus::completed},
            {QStringLiteral("removedItems"), job.value(QStringLiteral("removedItems")).toInt() + removed},
            {QStringLiteral("totalItems"), job.value(QStringLiteral("processedItems")).toInt()},
            {QStringLiteral("errorMessage"), null()},
            {QStringLiteral("nextAttemptAt"), null()},
            {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
        });
    });
    emit libraryChanged(libraryId);
}

void EmbeddingIndexer::pauseJob(const QString &jobId, const QString &libraryId, const QString &reason, const QString &message)
{
    m_database.transaction([&]() {
        updateJob(m_database, jobId, {
            {QStringLiteral("status"), JobStatus::paused},
            {QStringLiteral("pauseReason"), reason},
            {QStringLiteral("errorMessage"), message},
            {QStringLiteral("nextAttemptAt"), null()},
        });
    });
    emit libraryChanged(libraryId);
}
}
