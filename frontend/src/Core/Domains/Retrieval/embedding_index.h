#pragma once

#include "Core/Database/core_database.h"
#include "embedding_config.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <optional>

// The SQLite side of semantic retrieval (migration 22). Plain functions over
// one connection, like the other repositories; callers own transactions.
//
//   embedding_vectors          content hash + config → Float32 vector (the cache)
//   embedding_sources          source (library chunk, later wiki page/memory)
//                              → content hash, scope and citation metadata
//   semantic_library_settings  per-Library consent and pause
//   embedding_jobs             the persistent indexing queue
//   embedding_usage            tokens and requests per UTC day
namespace EmbeddingIndex
{
using Retrieval::EmbeddingConfig;

// Something with text that can be embedded and cited.
struct SourceText
{
    QString sourceType;
    QString sourceId;
    QString scopeType;
    QString scopeId;
    QString contentHash;
    QString content;
    QString parentId;  // Library file id for chunks
    QString locator;   // relative path at indexing time (citations read the live path)
    int startLine = 0;
    int endLine = 0;
    int estimatedTokens = 0;
};

// ---- Per-Library consent ------------------------------------------------

// {libraryId, enabled, paused, consentedAt}
[[nodiscard]] QVariantMap librarySettings(CoreDatabase &database, const QString &libraryId);
[[nodiscard]] bool isLibraryEnabled(CoreDatabase &database, const QString &libraryId);
// Enabling records the consent time. Disabling only flips the flag; call
// removeScope() and collectGarbage() to forget the vectors.
void setLibraryEnabled(CoreDatabase &database, const QString &libraryId, bool enabled);
void setLibraryPaused(CoreDatabase &database, const QString &libraryId, bool paused);
// Enabled, not archived Libraries.
[[nodiscard]] QStringList enabledLibraries(CoreDatabase &database);

// ---- Library chunk sources ----------------------------------------------

// Indexed chunks of available files whose source row is missing, points at
// other content, or has no vector for `config`. Ordered by path and ordinal.
[[nodiscard]] QList<SourceText> pendingLibraryChunks(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config, int limit);

struct PendingSummary
{
    int items = 0;          // chunks to (re)associate
    int uniqueContent = 0;  // distinct hashes among them without a vector: what an API call would embed
    qint64 estimatedTokens = 0; // tokens for those hashes
};
[[nodiscard]] PendingSummary pendingLibrarySummary(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config);

// Source rows of a Library whose chunk is gone, moved to another Library or
// no longer searchable (file unavailable, document not indexed). Returns how
// many were removed.
int removeStaleLibrarySources(CoreDatabase &database, const QString &libraryId);

// ---- Vectors and sources (any source type) ------------------------------

// Hashes among `hashes` that already have a vector for `config`.
[[nodiscard]] QSet<QString> existingVectors(CoreDatabase &database, const QStringList &hashes, const EmbeddingConfig &config);
// Stores a unit vector. An existing vector for the same hash and config wins.
void storeVector(CoreDatabase &database, const QString &contentHash, const EmbeddingConfig &config, const QVector<float> &vector, int tokenCount);
[[nodiscard]] QVector<float> vectorFor(CoreDatabase &database, const QString &contentHash, const EmbeddingConfig &config);
void upsertSource(CoreDatabase &database, const SourceText &source);
void removeSource(CoreDatabase &database, const QString &sourceType, const QString &sourceId);
// Every source row in a scope. Returns how many.
int removeScope(CoreDatabase &database, const QString &scopeType, const QString &scopeId);
// Deletes vectors no source points at and vectors made with another config.
// Returns how many.
int collectGarbage(CoreDatabase &database, const EmbeddingConfig &config);

struct ScopeCounts
{
    int sources = 0;   // source rows in the scope
    int searchable = 0; // of those, with a vector for the config
};
[[nodiscard]] ScopeCounts scopeCounts(CoreDatabase &database, const QString &scopeType, const QString &scopeId, const EmbeddingConfig &config);
[[nodiscard]] int vectorCount(CoreDatabase &database);

// ---- Jobs ----------------------------------------------------------------

namespace JobStatus
{
inline const QString queued = QStringLiteral("queued");
inline const QString running = QStringLiteral("running");
inline const QString paused = QStringLiteral("paused");
inline const QString completed = QStringLiteral("completed");
inline const QString failed = QStringLiteral("failed");
inline const QString cancelled = QStringLiteral("cancelled");
}

namespace PauseReason
{
inline const QString user = QStringLiteral("user");
inline const QString budget = QStringLiteral("budget");             // daily or per-job token limit
inline const QString unauthorized = QStringLiteral("unauthorized"); // OpenAI refused the key
inline const QString notConfigured = QStringLiteral("not-configured"); // no key
}

// The queued, running or paused job of a scope.
[[nodiscard]] std::optional<QVariantMap> activeJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId);
[[nodiscard]] std::optional<QVariantMap> latestJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId);
[[nodiscard]] std::optional<QVariantMap> job(CoreDatabase &database, const QString &jobId);
// The active job, or a new queued one.
QVariantMap ensureJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId, const EmbeddingConfig &config);
// Sets the given columns (camelCase keys of the job map) and updated_at.
void updateJob(CoreDatabase &database, const QString &jobId, const QVariantMap &fields);
// Jobs left running by a previous process go back to queued. Returns how many.
int recoverInterruptedJobs(CoreDatabase &database);
// Queued (or running, without a request in flight) jobs whose next attempt
// is due at `now`, oldest first.
[[nodiscard]] QList<QVariantMap> runnableJobs(CoreDatabase &database, const QDateTime &now);
// When the earliest queued job may run again; invalid when none waits.
[[nodiscard]] QDateTime nextQueuedAttempt(CoreDatabase &database);

// ---- Spending ------------------------------------------------------------

[[nodiscard]] QString usageDay(const QDateTime &now);
void addUsage(CoreDatabase &database, const QDateTime &now, int tokens, int requests);
[[nodiscard]] qint64 tokensUsedOn(CoreDatabase &database, const QDateTime &now);

// ---- Status --------------------------------------------------------------

// What the UI shows for one Library: settings, counts, pending work and the
// latest job. {libraryId, enabled, paused, consentedAt, model, dimensions,
// sources, searchable, pending, pendingTokens, state, job}
[[nodiscard]] QVariantMap libraryStatus(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config);
}
