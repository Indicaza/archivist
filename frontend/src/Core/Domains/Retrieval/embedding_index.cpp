#include "embedding_index.h"

#include "vector_codec.h"

#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QHash>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString jobColumns = QStringLiteral(
    "id, scope_type AS scopeType, scope_id AS scopeId, status, model, dimensions, "
    "total_items AS totalItems, processed_items AS processedItems, embedded_items AS embeddedItems, "
    "reused_items AS reusedItems, removed_items AS removedItems, tokens_used AS tokensUsed, "
    "request_count AS requestCount, attempt_count AS attemptCount, error_message AS errorMessage, "
    "pause_reason AS pauseReason, next_attempt_at AS nextAttemptAt, created_at AS createdAt, "
    "started_at AS startedAt, updated_at AS updatedAt, completed_at AS completedAt"
);

QVariantMap jobMap(const QVariantMap &row)
{
    QVariantMap job;

    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        job.insert(it.key(), nullable(it.value()));
    }

    return job;
}

// A library chunk is searchable only while its document is indexed and its
// file is available, the same rule Library FTS applies.
const QString searchableChunkJoin = QStringLiteral(
    "FROM library_chunks c "
    "INNER JOIN library_files f ON f.id = c.library_file_id "
    "INNER JOIN library_documents d ON d.library_file_id = c.library_file_id "
);

const QString pendingCondition = QStringLiteral(
    "LEFT JOIN embedding_sources s ON s.source_type = 'library-chunk' AND s.source_id = c.id "
    "LEFT JOIN embedding_vectors v ON v.content_hash = c.content_hash AND v.model = ? AND v.dimensions = ? AND v.input_version = ? "
    "WHERE c.library_id = ? AND d.status = 'indexed' AND f.status = 'available' "
    "AND (s.source_id IS NULL OR s.content_hash != c.content_hash OR s.scope_id != c.library_id "
    "OR s.locator IS NOT f.relative_path OR s.start_line IS NOT c.start_line OR s.end_line IS NOT c.end_line OR v.id IS NULL) "
);
}

namespace EmbeddingIndex
{
QVariantMap librarySettings(CoreDatabase &database, const QString &libraryId)
{
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT enabled, paused, consented_at AS consentedAt FROM semantic_library_settings WHERE library_id = ?"
                         ))
                         .bind(1, libraryId)
                         .one();
    return {
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("enabled"), row ? row->value(QStringLiteral("enabled")).toInt() == 1 : false},
        {QStringLiteral("paused"), row ? row->value(QStringLiteral("paused")).toInt() == 1 : false},
        {QStringLiteral("consentedAt"), row ? nullable(row->value(QStringLiteral("consentedAt"))) : QVariant::fromValue(nullptr)},
    };
}

bool isLibraryEnabled(CoreDatabase &database, const QString &libraryId)
{
    return database.prepare(QStringLiteral("SELECT enabled FROM semantic_library_settings WHERE library_id = ?"))
               .bind(1, libraryId)
               .scalar()
               .toInt()
        == 1;
}

void setLibraryEnabled(CoreDatabase &database, const QString &libraryId, bool enabled)
{
    database
        .prepare(QStringLiteral(
                     "INSERT INTO semantic_library_settings (library_id, enabled, consented_at, paused, updated_at) "
                     "VALUES (?1, ?2, CASE WHEN ?2 = 1 THEN %1 ELSE NULL END, 0, %1) "
                     "ON CONFLICT(library_id) DO UPDATE SET enabled = excluded.enabled, "
                     "consented_at = CASE WHEN excluded.enabled = 1 THEN COALESCE(semantic_library_settings.consented_at, excluded.consented_at) ELSE NULL END, "
                     "paused = 0, updated_at = excluded.updated_at"
        )
                     .arg(nowSql))
        .bind(1, libraryId)
        .bind(2, enabled ? 1 : 0)
        .run();
}

void setLibraryPaused(CoreDatabase &database, const QString &libraryId, bool paused)
{
    database.prepare(QStringLiteral("UPDATE semantic_library_settings SET paused = ?, updated_at = %1 WHERE library_id = ?").arg(nowSql))
        .bind(1, paused ? 1 : 0)
        .bind(2, libraryId)
        .run();
}

QStringList enabledLibraries(CoreDatabase &database)
{
    QStringList ids;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                          "SELECT s.library_id AS id FROM semantic_library_settings s "
                                          "INNER JOIN libraries l ON l.id = s.library_id "
                                          "WHERE s.enabled = 1 AND l.archived_at IS NULL ORDER BY s.library_id"
                                      ))
                                      .all()) {
        ids.append(row.value(QStringLiteral("id")).toString());
    }

    return ids;
}

QList<SourceText> pendingLibraryChunks(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config, int limit)
{
    QList<SourceText> pending;
    const auto rows = database
                          .prepare(QStringLiteral(
                                       "SELECT c.id AS sourceId, c.library_id AS scopeId, c.content_hash AS contentHash, c.content AS content, "
                                       "c.library_file_id AS parentId, f.relative_path AS locator, c.start_line AS startLine, "
                                       "c.end_line AS endLine, c.estimated_tokens AS estimatedTokens "
                                   )
                                   + searchableChunkJoin + pendingCondition
                                   + QStringLiteral("ORDER BY f.relative_path COLLATE NOCASE ASC, c.ordinal ASC, c.id ASC LIMIT ?"))
                          .bind(1, config.model)
                          .bind(2, config.dimensions)
                          .bind(3, config.inputVersion)
                          .bind(4, libraryId)
                          .bind(5, std::max(1, limit))
                          .all();

    for (const QVariantMap &row : rows) {
        SourceText source;
        source.sourceType = Retrieval::SourceType::libraryChunk;
        source.sourceId = row.value(QStringLiteral("sourceId")).toString();
        source.scopeType = Retrieval::ScopeType::library;
        source.scopeId = row.value(QStringLiteral("scopeId")).toString();
        source.contentHash = row.value(QStringLiteral("contentHash")).toString();
        source.content = row.value(QStringLiteral("content")).toString();
        source.parentId = row.value(QStringLiteral("parentId")).toString();
        source.locator = row.value(QStringLiteral("locator")).toString();
        source.startLine = row.value(QStringLiteral("startLine")).toInt();
        source.endLine = row.value(QStringLiteral("endLine")).toInt();
        source.estimatedTokens = row.value(QStringLiteral("estimatedTokens")).toInt();
        pending.append(source);
    }

    return pending;
}

PendingSummary pendingLibrarySummary(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config)
{
    const auto row = database
                         .prepare(QStringLiteral(
                                      "SELECT COUNT(*) AS items, "
                                      "COUNT(DISTINCT CASE WHEN v.id IS NULL THEN c.content_hash END) AS uniqueContent "
                                  )
                                  + searchableChunkJoin + pendingCondition)
                         .bind(1, config.model)
                         .bind(2, config.dimensions)
                         .bind(3, config.inputVersion)
                         .bind(4, libraryId)
                         .one();
    // Tokens once per distinct hash that needs the API.
    const QVariant tokens = database
                                .prepare(QStringLiteral(
                                    "SELECT COALESCE(SUM(tokens), 0) FROM ("
                                    "SELECT MAX(c.estimated_tokens) AS tokens FROM library_chunks c "
                                    "INNER JOIN library_files f ON f.id = c.library_file_id "
                                    "INNER JOIN library_documents d ON d.library_file_id = c.library_file_id "
                                    "WHERE c.library_id = ? AND d.status = 'indexed' AND f.status = 'available' "
                                    "AND NOT EXISTS (SELECT 1 FROM embedding_vectors v WHERE v.content_hash = c.content_hash "
                                    "AND v.model = ? AND v.dimensions = ? AND v.input_version = ?) "
                                    "GROUP BY c.content_hash)"
                                ))
                                .bind(1, libraryId)
                                .bind(2, config.model)
                                .bind(3, config.dimensions)
                                .bind(4, config.inputVersion)
                                .scalar();
    PendingSummary summary;
    summary.items = row ? row->value(QStringLiteral("items")).toInt() : 0;
    summary.uniqueContent = row ? row->value(QStringLiteral("uniqueContent")).toInt() : 0;
    summary.estimatedTokens = tokens.toLongLong();
    return summary;
}

int removeStaleLibrarySources(CoreDatabase &database, const QString &libraryId)
{
    database
        .prepare(QStringLiteral(
            "DELETE FROM embedding_sources WHERE source_type = 'library-chunk' AND scope_type = 'library' AND scope_id = ?1 "
            "AND NOT EXISTS (SELECT 1 FROM library_chunks c "
            "INNER JOIN library_files f ON f.id = c.library_file_id "
            "INNER JOIN library_documents d ON d.library_file_id = c.library_file_id "
            "WHERE c.id = embedding_sources.source_id AND c.library_id = ?1 "
            "AND d.status = 'indexed' AND f.status = 'available')"
        ))
        .bind(1, libraryId)
        .run();
    return database.changes();
}

QSet<QString> existingVectors(CoreDatabase &database, const QStringList &hashes, const EmbeddingConfig &config)
{
    QSet<QString> found;
    const QStringList unique = uniqueIds(hashes);

    for (qsizetype start = 0; start < unique.size(); start += 400) {
        const QStringList slice = unique.mid(start, 400);
        auto statement = database.prepare(QStringLiteral(
            "SELECT content_hash AS hash FROM embedding_vectors WHERE model = ? AND dimensions = ? AND input_version = ? "
            "AND content_hash IN (%1)"
        ).arg(placeholders(slice.size())));
        statement.bind(1, config.model).bind(2, config.dimensions).bind(3, config.inputVersion);

        for (qsizetype index = 0; index < slice.size(); ++index) {
            statement.bind(static_cast<int>(index) + 4, slice.at(index));
        }

        for (const QVariantMap &row : statement.all()) {
            found.insert(row.value(QStringLiteral("hash")).toString());
        }
    }

    return found;
}

void storeVector(CoreDatabase &database, const QString &contentHash, const EmbeddingConfig &config, const QVector<float> &vector, int tokenCount)
{
    if (vector.size() != config.dimensions) {
        throw CoreError(QStringLiteral("An embedding has %1 dimensions; %2 were expected.").arg(vector.size()).arg(config.dimensions));
    }

    database
        .prepare(QStringLiteral(
            "INSERT INTO embedding_vectors (content_hash, model, dimensions, input_version, vector, token_count) "
            "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(content_hash, model, dimensions, input_version) DO NOTHING"
        ))
        .bind(1, contentHash)
        .bind(2, config.model)
        .bind(3, config.dimensions)
        .bind(4, config.inputVersion)
        .bind(5, VectorCodec::encode(vector))
        .bind(6, std::max(0, tokenCount))
        .run();
}

QVector<float> vectorFor(CoreDatabase &database, const QString &contentHash, const EmbeddingConfig &config)
{
    const QVariant blob = database
                              .prepare(QStringLiteral(
                                  "SELECT vector FROM embedding_vectors WHERE content_hash = ? AND model = ? AND dimensions = ? AND input_version = ?"
                              ))
                              .bind(1, contentHash)
                              .bind(2, config.model)
                              .bind(3, config.dimensions)
                              .bind(4, config.inputVersion)
                              .scalar();
    return VectorCodec::decode(blob.toByteArray(), config.dimensions);
}

void upsertSource(CoreDatabase &database, const SourceText &source)
{
    database
        .prepare(QStringLiteral(
                     "INSERT INTO embedding_sources (source_type, source_id, scope_type, scope_id, content_hash, parent_id, locator, "
                     "start_line, end_line, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, %1) "
                     "ON CONFLICT(source_type, source_id) DO UPDATE SET scope_type = excluded.scope_type, "
                     "scope_id = excluded.scope_id, content_hash = excluded.content_hash, parent_id = excluded.parent_id, "
                     "locator = excluded.locator, start_line = excluded.start_line, end_line = excluded.end_line, "
                     "updated_at = excluded.updated_at"
        )
                     .arg(nowSql))
        .bind(1, source.sourceType)
        .bind(2, source.sourceId)
        .bind(3, source.scopeType)
        .bind(4, source.scopeId)
        .bind(5, source.contentHash)
        .bind(6, nullableString(source.parentId))
        .bind(7, nullableString(source.locator))
        .bind(8, source.startLine > 0 ? QVariant(source.startLine) : QVariant())
        .bind(9, source.endLine > 0 ? QVariant(source.endLine) : QVariant())
        .run();
}

void removeSource(CoreDatabase &database, const QString &sourceType, const QString &sourceId)
{
    database.prepare(QStringLiteral("DELETE FROM embedding_sources WHERE source_type = ? AND source_id = ?"))
        .bind(1, sourceType)
        .bind(2, sourceId)
        .run();
}

int removeScope(CoreDatabase &database, const QString &scopeType, const QString &scopeId)
{
    database.prepare(QStringLiteral("DELETE FROM embedding_sources WHERE scope_type = ? AND scope_id = ?"))
        .bind(1, scopeType)
        .bind(2, scopeId)
        .run();
    return database.changes();
}

int collectGarbage(CoreDatabase &database, const EmbeddingConfig &config)
{
    database
        .prepare(QStringLiteral(
            "DELETE FROM embedding_vectors WHERE NOT (model = ? AND dimensions = ? AND input_version = ?) "
            "OR NOT EXISTS (SELECT 1 FROM embedding_sources s WHERE s.content_hash = embedding_vectors.content_hash)"
        ))
        .bind(1, config.model)
        .bind(2, config.dimensions)
        .bind(3, config.inputVersion)
        .run();
    return database.changes();
}

ScopeCounts scopeCounts(CoreDatabase &database, const QString &scopeType, const QString &scopeId, const EmbeddingConfig &config)
{
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT COUNT(*) AS sources, COUNT(v.id) AS searchable FROM embedding_sources s "
                             "LEFT JOIN embedding_vectors v ON v.content_hash = s.content_hash AND v.model = ? AND v.dimensions = ? "
                             "AND v.input_version = ? WHERE s.scope_type = ? AND s.scope_id = ?"
                         ))
                         .bind(1, config.model)
                         .bind(2, config.dimensions)
                         .bind(3, config.inputVersion)
                         .bind(4, scopeType)
                         .bind(5, scopeId)
                         .one();
    ScopeCounts counts;
    counts.sources = row ? row->value(QStringLiteral("sources")).toInt() : 0;
    counts.searchable = row ? row->value(QStringLiteral("searchable")).toInt() : 0;
    return counts;
}

int vectorCount(CoreDatabase &database)
{
    return database.prepare(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")).scalar().toInt();
}

std::optional<QVariantMap> activeJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId)
{
    const auto row = database
                         .prepare(QStringLiteral("SELECT %1 FROM embedding_jobs WHERE scope_type = ? AND scope_id = ? "
                                                 "AND status IN ('queued', 'running', 'paused') LIMIT 1")
                                      .arg(jobColumns))
                         .bind(1, scopeType)
                         .bind(2, scopeId)
                         .one();
    return row ? std::optional<QVariantMap>(jobMap(*row)) : std::nullopt;
}

std::optional<QVariantMap> latestJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId)
{
    if (auto active = activeJob(database, scopeType, scopeId)) {
        return active;
    }

    const auto row = database
                         .prepare(QStringLiteral("SELECT %1 FROM embedding_jobs WHERE scope_type = ? AND scope_id = ? "
                                                 "ORDER BY created_at DESC, rowid DESC LIMIT 1")
                                      .arg(jobColumns))
                         .bind(1, scopeType)
                         .bind(2, scopeId)
                         .one();
    return row ? std::optional<QVariantMap>(jobMap(*row)) : std::nullopt;
}

std::optional<QVariantMap> job(CoreDatabase &database, const QString &jobId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM embedding_jobs WHERE id = ?").arg(jobColumns)).bind(1, jobId).one();
    return row ? std::optional<QVariantMap>(jobMap(*row)) : std::nullopt;
}

QVariantMap ensureJob(CoreDatabase &database, const QString &scopeType, const QString &scopeId, const EmbeddingConfig &config)
{
    if (auto active = activeJob(database, scopeType, scopeId)) {
        return *active;
    }

    const QString id = newId();
    database
        .prepare(QStringLiteral(
            "INSERT INTO embedding_jobs (id, scope_type, scope_id, status, model, dimensions) VALUES (?, ?, ?, 'queued', ?, ?)"
        ))
        .bind(1, id)
        .bind(2, scopeType)
        .bind(3, scopeId)
        .bind(4, config.model)
        .bind(5, config.dimensions)
        .run();
    return *job(database, id);
}

void updateJob(CoreDatabase &database, const QString &jobId, const QVariantMap &fields)
{
    static const QHash<QString, QString> columns{
        {QStringLiteral("status"), QStringLiteral("status")},
        {QStringLiteral("totalItems"), QStringLiteral("total_items")},
        {QStringLiteral("processedItems"), QStringLiteral("processed_items")},
        {QStringLiteral("embeddedItems"), QStringLiteral("embedded_items")},
        {QStringLiteral("reusedItems"), QStringLiteral("reused_items")},
        {QStringLiteral("removedItems"), QStringLiteral("removed_items")},
        {QStringLiteral("tokensUsed"), QStringLiteral("tokens_used")},
        {QStringLiteral("requestCount"), QStringLiteral("request_count")},
        {QStringLiteral("attemptCount"), QStringLiteral("attempt_count")},
        {QStringLiteral("errorMessage"), QStringLiteral("error_message")},
        {QStringLiteral("pauseReason"), QStringLiteral("pause_reason")},
        {QStringLiteral("nextAttemptAt"), QStringLiteral("next_attempt_at")},
        {QStringLiteral("startedAt"), QStringLiteral("started_at")},
        {QStringLiteral("completedAt"), QStringLiteral("completed_at")},
    };
    QStringList assignments;
    QVariantList values;

    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString column = columns.value(it.key());

        if (column.isEmpty()) {
            throw CoreError(QStringLiteral("Unknown embedding job field \"%1\".").arg(it.key()));
        }

        assignments.append(column + QStringLiteral(" = ?"));
        const QVariant value = it.value();
        values.append(value.typeId() == QMetaType::Nullptr ? QVariant() : value);
    }

    assignments.append(QStringLiteral("updated_at = %1").arg(nowSql));
    auto statement = database.prepare(QStringLiteral("UPDATE embedding_jobs SET %1 WHERE id = ?").arg(assignments.join(QStringLiteral(", "))));

    for (qsizetype index = 0; index < values.size(); ++index) {
        statement.bind(static_cast<int>(index) + 1, values.at(index));
    }

    statement.bind(static_cast<int>(values.size()) + 1, jobId).run();
}

int recoverInterruptedJobs(CoreDatabase &database)
{
    database.prepare(QStringLiteral("UPDATE embedding_jobs SET status = 'queued', next_attempt_at = NULL, updated_at = %1 WHERE status = 'running'").arg(nowSql)).run();
    return database.changes();
}

QList<QVariantMap> runnableJobs(CoreDatabase &database, const QDateTime &now)
{
    QList<QVariantMap> jobs;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral("SELECT %1 FROM embedding_jobs WHERE status IN ('queued', 'running') "
                                                              "AND (next_attempt_at IS NULL OR next_attempt_at <= ?) "
                                                              "ORDER BY created_at ASC, rowid ASC")
                                                   .arg(jobColumns))
                                      .bind(1, JsCompat::isoTimestamp(now))
                                      .all()) {
        jobs.append(jobMap(row));
    }

    return jobs;
}

QDateTime nextQueuedAttempt(CoreDatabase &database)
{
    const QVariant next = database
                              .prepare(QStringLiteral(
                                  "SELECT MIN(next_attempt_at) FROM embedding_jobs WHERE status = 'queued' AND next_attempt_at IS NOT NULL"
                              ))
                              .scalar();
    return next.isNull() ? QDateTime() : QDateTime::fromString(next.toString(), Qt::ISODateWithMs);
}

QString usageDay(const QDateTime &now)
{
    return now.toUTC().date().toString(Qt::ISODate);
}

void addUsage(CoreDatabase &database, const QDateTime &now, int tokens, int requests)
{
    database
        .prepare(QStringLiteral(
            "INSERT INTO embedding_usage (day, tokens, requests) VALUES (?, ?, ?) "
            "ON CONFLICT(day) DO UPDATE SET tokens = tokens + excluded.tokens, requests = requests + excluded.requests"
        ))
        .bind(1, usageDay(now))
        .bind(2, std::max(0, tokens))
        .bind(3, std::max(0, requests))
        .run();
}

qint64 tokensUsedOn(CoreDatabase &database, const QDateTime &now)
{
    return database.prepare(QStringLiteral("SELECT tokens FROM embedding_usage WHERE day = ?")).bind(1, usageDay(now)).scalar().toLongLong();
}

QVariantMap libraryStatus(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config)
{
    QVariantMap status = librarySettings(database, libraryId);
    const ScopeCounts counts = scopeCounts(database, Retrieval::ScopeType::library, libraryId, config);
    const bool enabled = status.value(QStringLiteral("enabled")).toBool();
    const PendingSummary pending = pendingLibrarySummary(database, libraryId, config);
    const auto latest = latestJob(database, Retrieval::ScopeType::library, libraryId);
    status.insert(QStringLiteral("model"), config.model);
    status.insert(QStringLiteral("dimensions"), config.dimensions);
    status.insert(QStringLiteral("sources"), counts.sources);
    status.insert(QStringLiteral("searchable"), counts.searchable);
    status.insert(QStringLiteral("pending"), pending.items);
    status.insert(QStringLiteral("pendingContent"), pending.uniqueContent);
    status.insert(QStringLiteral("pendingTokens"), pending.estimatedTokens);
    status.insert(QStringLiteral("job"), latest ? QVariant(*latest) : QVariant::fromValue(nullptr));

    // One word the UI can switch on.
    QString state = QStringLiteral("off");

    if (enabled) {
        const QString jobStatus = latest ? latest->value(QStringLiteral("status")).toString() : QString();

        if (jobStatus == JobStatus::running || jobStatus == JobStatus::queued) {
            state = latest->value(QStringLiteral("nextAttemptAt")).typeId() == QMetaType::QString ? QStringLiteral("waiting") : QStringLiteral("indexing");
        } else if (jobStatus == JobStatus::paused) {
            state = QStringLiteral("paused");
        } else if (jobStatus == JobStatus::failed) {
            state = QStringLiteral("failed");
        } else if (pending.items > 0) {
            state = QStringLiteral("outdated");
        } else {
            state = QStringLiteral("ready");
        }
    }

    status.insert(QStringLiteral("state"), state);
    return status;
}
}
