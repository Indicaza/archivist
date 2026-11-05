#include "librarian_store.h"

#include "knowledge_text.h"
#include "librarian_types.h"

#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <QHash>
#include <QSet>

#include <algorithm>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

QString text(const QVariantMap &row, const char *key)
{
    const QVariant value = row.value(QLatin1String(key));
    return value.isNull() ? QString() : value.toString();
}

QVariantMap camel(const QVariantMap &row)
{
    QVariantMap map;

    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        map.insert(it.key(), nullable(it.value()));
    }

    return map;
}

void ensureSettingsRow(CoreDatabase &database, const QString &libraryId)
{
    database.prepare(QStringLiteral("INSERT OR IGNORE INTO knowledge_librarian_settings (library_id) VALUES (?)")).bind(1, libraryId).run();
}

const QString runColumns = QStringLiteral(
    "r.id, r.library_id AS libraryId, r.librarian, r.trigger, r.status, r.request, r.model_calls AS modelCalls, "
    "r.input_tokens AS inputTokens, r.output_tokens AS outputTokens, r.estimated_cost_usd AS estimatedCostUsd, "
    "r.jobs_created AS jobsCreated, r.findings_created AS findingsCreated, r.proposals_created AS proposalsCreated, "
    "r.summary, r.error_message AS errorMessage, r.started_at AS startedAt, r.updated_at AS updatedAt, r.completed_at AS completedAt, "
    "(SELECT COUNT(*) FROM knowledge_jobs j WHERE j.run_id = r.id AND j.status IN ('queued', 'running')) AS activeJobs"
);

QVariantMap runMap(const QVariantMap &row)
{
    QVariantMap map = camel(row);
    map.insert(QStringLiteral("request"), JsonValues::parseObject(text(row, "request")));
    map.insert(QStringLiteral("summary"), JsonValues::parseObject(text(row, "summary")));
    return map;
}

const QString topicColumns = QStringLiteral(
    "id, library_id AS libraryId, topic, topic_key AS topicKey, origin, scope, depth, parent_topic_id AS parentTopicId, status, "
    "page_id AS pageId, evidence_fingerprint AS evidenceFingerprint, investigated_fingerprint AS investigatedFingerprint, findings, "
    "open_questions AS openQuestions, last_job_id AS lastJobId, last_proposal_id AS lastProposalId, attempts, "
    "investigated_at AS investigatedAt, created_at AS createdAt, updated_at AS updatedAt"
);

QVariantMap topicMap(const QVariantMap &row)
{
    QVariantMap map = camel(row);
    map.insert(QStringLiteral("scope"), JsonValues::parseObject(text(row, "scope")));
    map.insert(QStringLiteral("findings"), JsonValues::parse(text(row, "findings")).toList());
    map.insert(QStringLiteral("openQuestions"), JsonValues::parse(text(row, "openQuestions")).toList());
    return map;
}
}

namespace LibrarianStore
{
using namespace Librarian;

// ---- Settings ----------------------------------------------------------------

bool Settings::allows(const QString &librarian) const
{
    return librarian == Kind::maintenance ? maintenance : librarian == Kind::research ? research : librarian == Kind::verification ? verification : false;
}

QString Settings::effectiveModel() const
{
    return model.trimmed().isEmpty() ? defaultModel : model.trimmed();
}

QString Settings::lastRunAt(const QString &librarian) const
{
    return librarian == Kind::maintenance ? lastMaintenanceAt : librarian == Kind::research ? lastResearchAt : lastVerificationAt;
}

QVariantMap Settings::toVariant() const
{
    return {
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("consented"), consented()},
        {QStringLiteral("consentedAt"), nullableString(consentedAt)},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("scheduleMinutes"), scheduleMinutes},
        {QStringLiteral("idleMinutes"), idleMinutes},
        {QStringLiteral("maintenance"), maintenance},
        {QStringLiteral("research"), research},
        {QStringLiteral("verification"), verification},
        {QStringLiteral("paused"), paused},
        {QStringLiteral("model"), effectiveModel()},
        {QStringLiteral("maxCallsPerRun"), maxCallsPerRun},
        {QStringLiteral("maxTokensPerRun"), maxTokensPerRun},
        {QStringLiteral("dailyCallBudget"), dailyCallBudget},
        {QStringLiteral("dailyTokenBudget"), dailyTokenBudget},
        {QStringLiteral("maxDepth"), maxDepth},
        {QStringLiteral("changeNotedAt"), nullableString(changeNotedAt)},
        {QStringLiteral("lastMaintenanceAt"), nullableString(lastMaintenanceAt)},
        {QStringLiteral("lastResearchAt"), nullableString(lastResearchAt)},
        {QStringLiteral("lastVerificationAt"), nullableString(lastVerificationAt)},
        {QStringLiteral("nextScheduledAt"), nullableString(nextScheduledAt)},
        {QStringLiteral("consecutiveFailures"), consecutiveFailures},
        {QStringLiteral("backoffUntil"), nullableString(backoffUntil)},
        {QStringLiteral("lastError"), nullableString(lastError)},
    };
}

Settings settings(CoreDatabase &database, const QString &libraryId)
{
    Settings result;
    result.libraryId = libraryId;
    const auto row = database.prepare(QStringLiteral("SELECT * FROM knowledge_librarian_settings WHERE library_id = ?")).bind(1, libraryId).one();

    if (!row) {
        return result;
    }

    const auto flag = [&](const char *key) { return row->value(QLatin1String(key)).toInt() == 1; };
    const auto number = [&](const char *key) { return row->value(QLatin1String(key)).toInt(); };
    result.enabled = flag("enabled");
    result.consentedAt = text(*row, "ai_consented_at");
    result.mode = text(*row, "mode");
    result.scheduleMinutes = number("schedule_minutes");
    result.idleMinutes = number("idle_minutes");
    result.maintenance = flag("maintenance_enabled");
    result.research = flag("research_enabled");
    result.verification = flag("verification_enabled");
    result.paused = flag("paused");
    result.model = text(*row, "model");
    result.maxCallsPerRun = number("max_calls_per_run");
    result.maxTokensPerRun = number("max_tokens_per_run");
    result.dailyCallBudget = number("daily_call_budget");
    result.dailyTokenBudget = number("daily_token_budget");
    result.maxDepth = number("max_depth");
    result.changeNotedAt = text(*row, "change_noted_at");
    result.lastMaintenanceAt = text(*row, "last_maintenance_at");
    result.lastResearchAt = text(*row, "last_research_at");
    result.lastVerificationAt = text(*row, "last_verification_at");
    result.nextScheduledAt = text(*row, "next_scheduled_at");
    result.consecutiveFailures = number("consecutive_failures");
    result.backoffUntil = text(*row, "backoff_until");
    result.lastError = text(*row, "last_error");
    return result;
}

Settings updateSettings(CoreDatabase &database, const QString &libraryId, const QVariantMap &changes)
{
    struct Column
    {
        const char *name;
        char kind; // b = boolean, i = integer, s = text
        int min;
        int max;
    };

    static const QHash<QString, Column> columns{
        {QStringLiteral("enabled"), {"enabled", 'b', 0, 1}},
        {QStringLiteral("mode"), {"mode", 's', 0, 0}},
        {QStringLiteral("scheduleMinutes"), {"schedule_minutes", 'i', 15, 60 * 24 * 30}},
        {QStringLiteral("idleMinutes"), {"idle_minutes", 'i', 1, 24 * 60}},
        {QStringLiteral("maintenance"), {"maintenance_enabled", 'b', 0, 1}},
        {QStringLiteral("research"), {"research_enabled", 'b', 0, 1}},
        {QStringLiteral("verification"), {"verification_enabled", 'b', 0, 1}},
        {QStringLiteral("paused"), {"paused", 'b', 0, 1}},
        {QStringLiteral("model"), {"model", 's', 0, 0}},
        {QStringLiteral("maxCallsPerRun"), {"max_calls_per_run", 'i', 0, 50}},
        {QStringLiteral("maxTokensPerRun"), {"max_tokens_per_run", 'i', 0, 2000000}},
        {QStringLiteral("dailyCallBudget"), {"daily_call_budget", 'i', 0, 1000}},
        {QStringLiteral("dailyTokenBudget"), {"daily_token_budget", 'i', 0, 50000000}},
        {QStringLiteral("maxDepth"), {"max_depth", 'i', 0, 5}},
    };

    return Transactions::join(database, [&]() {
        ensureSettingsRow(database, libraryId);
        QStringList assignments;
        QVariantList values;

        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (!columns.contains(it.key())) {
                throw CoreError(QStringLiteral("Unknown librarian setting: %1").arg(it.key()), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_INVALID_SETTING")}});
            }

            const Column column = columns.value(it.key());
            QVariant value;

            if (column.kind == 'b') {
                value = it.value().toBool() ? 1 : 0;
            } else if (column.kind == 'i') {
                value = std::clamp(it.value().toInt(), column.min, column.max);
            } else if (it.key() == QStringLiteral("mode")) {
                const QString mode = it.value().toString();

                if (!Mode::isValid(mode)) {
                    throw CoreError(QStringLiteral("Unknown librarian mode: %1").arg(mode), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_INVALID_SETTING")}});
                }

                value = mode;
            } else {
                const QString string = it.value().toString().trimmed();
                value = string.isEmpty() ? QVariant() : QVariant(string.left(80));
            }

            assignments.append(QLatin1String(column.name) + QStringLiteral(" = ?"));
            values.append(value);
        }

        if (changes.contains(QStringLiteral("mode")) || changes.contains(QStringLiteral("scheduleMinutes"))) {
            // A new schedule starts counting from now.
            assignments.append(QStringLiteral("next_scheduled_at = NULL"));
        }

        if (changes.value(QStringLiteral("paused")).isValid() && !changes.value(QStringLiteral("paused")).toBool()) {
            // Resuming also forgets an earlier provider backoff.
            assignments.append(QStringLiteral("backoff_until = NULL"));
            assignments.append(QStringLiteral("consecutive_failures = 0"));
        }

        if (!assignments.isEmpty()) {
            auto statement = database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET %1, updated_at = %2 WHERE library_id = ?")
                                                  .arg(assignments.join(QStringLiteral(", ")), nowSql));
            statement.bindAll(values);
            statement.bind(static_cast<int>(values.size()) + 1, libraryId);
            statement.run();
        }

        return settings(database, libraryId);
    });
}

void setConsent(CoreDatabase &database, const QString &libraryId, bool consented)
{
    Transactions::join(database, [&]() {
        ensureSettingsRow(database, libraryId);
        database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET ai_consented_at = %1, updated_at = %2 WHERE library_id = ?")
                             .arg(consented ? QStringLiteral("COALESCE(ai_consented_at, %1)").arg(nowSql) : QStringLiteral("NULL"), nowSql))
            .bind(1, libraryId)
            .run();
    });
}

QStringList enabledLibraries(CoreDatabase &database)
{
    QStringList result;

    for (const QVariantMap &row : database.prepare(QStringLiteral(
                                                      "SELECT s.library_id FROM knowledge_librarian_settings s INNER JOIN libraries l ON l.id = s.library_id "
                                                      "WHERE s.enabled = 1 AND l.archived_at IS NULL ORDER BY s.library_id"
                                                  ))
                                      .all()) {
        result.append(row.value(QStringLiteral("library_id")).toString());
    }

    return result;
}

void noteChange(CoreDatabase &database, const QString &libraryId, const QDateTime &when)
{
    Transactions::join(database, [&]() {
        ensureSettingsRow(database, libraryId);
        // The first change since the last maintenance wins: later changes
        // coalesce into the same pending pass.
        database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET change_noted_at = COALESCE(change_noted_at, ?), updated_at = %1 WHERE library_id = ?").arg(nowSql))
            .bind(1, JsCompat::isoTimestamp(when))
            .bind(2, libraryId)
            .run();
    });
}

void clearChange(CoreDatabase &database, const QString &libraryId)
{
    database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET change_noted_at = NULL WHERE library_id = ?")).bind(1, libraryId).run();
}

void markRan(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QDateTime &when)
{
    const QString column = librarian == Kind::maintenance ? QStringLiteral("last_maintenance_at")
        : librarian == Kind::research                     ? QStringLiteral("last_research_at")
                                                          : QStringLiteral("last_verification_at");
    Transactions::join(database, [&]() {
        ensureSettingsRow(database, libraryId);
        database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET %1 = ?, updated_at = %2 WHERE library_id = ?").arg(column, nowSql))
            .bind(1, JsCompat::isoTimestamp(when))
            .bind(2, libraryId)
            .run();
    });
}

void setNextScheduled(CoreDatabase &database, const QString &libraryId, const QDateTime &when)
{
    database.prepare(QStringLiteral("UPDATE knowledge_librarian_settings SET next_scheduled_at = ? WHERE library_id = ?"))
        .bind(1, when.isValid() ? QVariant(JsCompat::isoTimestamp(when)) : QVariant())
        .bind(2, libraryId)
        .run();
}

void recordProviderFailure(CoreDatabase &database, const QString &libraryId, const QString &message, const QDateTime &now)
{
    Transactions::join(database, [&]() {
        ensureSettingsRow(database, libraryId);
        const int failures = settings(database, libraryId).consecutiveFailures + 1;
        const qint64 backoffMs = std::min<qint64>(60LL * 60 * 1000, 60LL * 1000 * (1LL << std::min(failures - 1, 6)));
        database.prepare(QStringLiteral(
                             "UPDATE knowledge_librarian_settings SET consecutive_failures = ?, backoff_until = ?, last_error = ?, updated_at = %1 WHERE library_id = ?"
        )
                             .arg(nowSql))
            .bind(1, failures)
            .bind(2, JsCompat::isoTimestamp(now.addMSecs(backoffMs)))
            .bind(3, message.left(500))
            .bind(4, libraryId)
            .run();
    });
}

void recordProviderSuccess(CoreDatabase &database, const QString &libraryId)
{
    database.prepare(QStringLiteral(
                         "UPDATE knowledge_librarian_settings SET consecutive_failures = 0, backoff_until = NULL, last_error = NULL "
                         "WHERE library_id = ? AND (consecutive_failures != 0 OR backoff_until IS NOT NULL OR last_error IS NOT NULL)"
    ))
        .bind(1, libraryId)
        .run();
}

// ---- Source snapshot -------------------------------------------------------

QVariantMap SourceChanges::toVariant() const
{
    const auto capped = [](const QStringList &list) { return list.mid(0, 50); };
    return {
        {QStringLiteral("added"), capped(added)},
        {QStringLiteral("modified"), capped(modified)},
        {QStringLiteral("moved"), capped(moved)},
        {QStringLiteral("deleted"), capped(deleted)},
        {QStringLiteral("addedCount"), added.size()},
        {QStringLiteral("modifiedCount"), modified.size()},
        {QStringLiteral("movedCount"), moved.size()},
        {QStringLiteral("deletedCount"), deleted.size()},
    };
}

namespace
{
struct FileState
{
    QString path;
    QString hash;
};

QHash<QString, FileState> currentFiles(CoreDatabase &database, const QString &libraryId)
{
    QHash<QString, FileState> files;

    for (const QVariantMap &row : database.prepare(QStringLiteral(
                                                      "SELECT f.id, f.relative_path, d.content_hash FROM library_files f "
                                                      "INNER JOIN library_documents d ON d.library_file_id = f.id "
                                                      "WHERE f.library_id = ? AND f.status = 'available' AND d.status = 'indexed'"
                                                  ))
                                      .bind(1, libraryId)
                                      .all()) {
        files.insert(row.value(QStringLiteral("id")).toString(), {row.value(QStringLiteral("relative_path")).toString(), text(row, "content_hash")});
    }

    return files;
}
}

SourceChanges diffSources(CoreDatabase &database, const QString &libraryId)
{
    SourceChanges changes;
    const QHash<QString, FileState> now = currentFiles(database, libraryId);
    QHash<QString, FileState> before;

    for (const QVariantMap &row : database.prepare(QStringLiteral("SELECT library_file_id, relative_path, content_hash FROM knowledge_librarian_snapshots WHERE library_id = ?"))
                                      .bind(1, libraryId)
                                      .all()) {
        before.insert(row.value(QStringLiteral("library_file_id")).toString(), {row.value(QStringLiteral("relative_path")).toString(), text(row, "content_hash")});
    }

    for (auto it = now.cbegin(); it != now.cend(); ++it) {
        const auto previous = before.constFind(it.key());

        if (previous == before.cend()) {
            changes.added.append(it->path);
            changes.addedFileIds.append(it.key());
            changes.changedFileIds.append(it.key());
        } else if (previous->hash != it->hash) {
            changes.modified.append(it->path);
            changes.changedFileIds.append(it.key());
        } else if (previous->path != it->path) {
            changes.moved.append(previous->path + QStringLiteral(" -> ") + it->path);
            changes.changedFileIds.append(it.key());
        }
    }

    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!now.contains(it.key())) {
            changes.deleted.append(it->path);
        }
    }

    changes.added.sort();
    changes.modified.sort();
    changes.moved.sort();
    changes.deleted.sort();
    changes.addedFileIds.sort();
    changes.changedFileIds.sort();
    return changes;
}

void saveSnapshot(CoreDatabase &database, const QString &libraryId)
{
    Transactions::join(database, [&]() {
        database.prepare(QStringLiteral("DELETE FROM knowledge_librarian_snapshots WHERE library_id = ?")).bind(1, libraryId).run();
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_librarian_snapshots (library_id, library_file_id, relative_path, content_hash, status) "
                "SELECT f.library_id, f.id, f.relative_path, d.content_hash, 'indexed' FROM library_files f "
                "INNER JOIN library_documents d ON d.library_file_id = f.id "
                "WHERE f.library_id = ? AND f.status = 'available' AND d.status = 'indexed'"
            ))
            .bind(1, libraryId)
            .run();
    });
}

// ---- Runs --------------------------------------------------------------------

RunStart startRun(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request)
{
    if (!Kind::isValid(librarian)) {
        throw CoreError(QStringLiteral("Unknown librarian: %1").arg(librarian), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_UNKNOWN")}});
    }

    return Transactions::join(database, [&]() {
        RunStart start;

        if (const auto active = activeRun(database, libraryId, librarian)) {
            start.run = *active;
            return start;
        }

        const QString id = newId();
        database
            .prepare(QStringLiteral("INSERT INTO knowledge_librarian_runs (id, library_id, librarian, trigger, request) VALUES (?, ?, ?, ?, ?)"))
            .bind(1, id)
            .bind(2, libraryId)
            .bind(3, librarian)
            .bind(4, trigger)
            .bind(5, JsonValues::toJsonString(request))
            .run();
        start.run = *run(database, id);
        start.created = true;
        return start;
    });
}

std::optional<QVariantMap> run(CoreDatabase &database, const QString &runId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM knowledge_librarian_runs r WHERE r.id = ?").arg(runColumns)).bind(1, runId).one();
    return row ? std::optional<QVariantMap>(runMap(*row)) : std::nullopt;
}

std::optional<QVariantMap> activeRun(CoreDatabase &database, const QString &libraryId, const QString &librarian)
{
    const auto row = database
                         .prepare(QStringLiteral("SELECT %1 FROM knowledge_librarian_runs r WHERE r.library_id = ? AND r.librarian = ? AND r.status = 'running'").arg(runColumns))
                         .bind(1, libraryId)
                         .bind(2, librarian)
                         .one();
    return row ? std::optional<QVariantMap>(runMap(*row)) : std::nullopt;
}

QVariantList runs(CoreDatabase &database, const QString &libraryId, int limit)
{
    QVariantList result;

    for (const QVariantMap &row : database.prepare(QStringLiteral("SELECT %1 FROM knowledge_librarian_runs r WHERE r.library_id = ? ORDER BY r.started_at DESC, r.id DESC LIMIT ?").arg(runColumns))
                                      .bind(1, libraryId)
                                      .bind(2, std::max(1, limit))
                                      .all()) {
        result.append(runMap(row));
    }

    return result;
}

void addRunUsage(CoreDatabase &database, const QString &runId, int calls, int inputTokens, int outputTokens, double costUsd)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_librarian_runs SET model_calls = model_calls + ?, input_tokens = input_tokens + ?, output_tokens = output_tokens + ?, "
                     "estimated_cost_usd = estimated_cost_usd + ?, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, std::max(0, calls))
        .bind(2, std::max(0, inputTokens))
        .bind(3, std::max(0, outputTokens))
        .bind(4, std::max(0.0, costUsd))
        .bind(5, runId)
        .run();
}

void countRun(CoreDatabase &database, const QString &runId, const QString &counter, int amount)
{
    static const QHash<QString, QString> counters{
        {QStringLiteral("jobs"), QStringLiteral("jobs_created")},
        {QStringLiteral("findings"), QStringLiteral("findings_created")},
        {QStringLiteral("proposals"), QStringLiteral("proposals_created")},
    };
    const QString column = counters.value(counter);

    if (column.isEmpty() || runId.isEmpty() || amount == 0) {
        return;
    }

    database.prepare(QStringLiteral("UPDATE knowledge_librarian_runs SET %1 = %1 + ?, updated_at = %2 WHERE id = ?").arg(column, nowSql)).bind(1, amount).bind(2, runId).run();
}

void noteRun(CoreDatabase &database, const QString &runId, const QVariantMap &fields)
{
    if (runId.isEmpty() || fields.isEmpty()) {
        return;
    }

    const auto current = run(database, runId);

    if (!current) {
        return;
    }

    QVariantMap summary = current->value(QStringLiteral("summary")).toMap();

    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        summary.insert(it.key(), it.value());
    }

    database.prepare(QStringLiteral("UPDATE knowledge_librarian_runs SET summary = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, JsonValues::toJsonString(summary))
        .bind(2, runId)
        .run();
}

void finishRun(CoreDatabase &database, const QString &runId, const QString &status, const QString &errorMessage)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_librarian_runs SET status = ?, error_message = COALESCE(?, error_message), completed_at = %1, updated_at = %1 "
                     "WHERE id = ? AND status = 'running'"
        )
                     .arg(nowSql))
        .bind(1, status)
        .bind(2, errorMessage.isEmpty() ? QVariant() : QVariant(errorMessage))
        .bind(3, runId)
        .run();
}

bool settleRun(CoreDatabase &database, const QString &runId)
{
    return Transactions::join(database, [&]() {
        const auto current = run(database, runId);

        if (!current || current->value(QStringLiteral("status")).toString() != RunStatus::running) {
            return false;
        }

        if (current->value(QStringLiteral("activeJobs")).toInt() > 0) {
            return false;
        }

        int total = 0;
        QHash<QString, int> byStatus;
        QString lastError;

        for (const QVariantMap &row : database.prepare(QStringLiteral("SELECT status, error_message FROM knowledge_jobs WHERE run_id = ? ORDER BY updated_at"))
                                          .bind(1, runId)
                                          .all()) {
            ++total;
            byStatus[row.value(QStringLiteral("status")).toString()] += 1;

            if (!row.value(QStringLiteral("error_message")).isNull() && row.value(QStringLiteral("status")).toString() == QStringLiteral("failed")) {
                lastError = row.value(QStringLiteral("error_message")).toString();
            }
        }

        QString status = RunStatus::completed;

        if (total > 0 && byStatus.value(QStringLiteral("failed")) == total) {
            status = RunStatus::failed;
        } else if (total > 0 && byStatus.value(QStringLiteral("cancelled")) == total) {
            status = RunStatus::cancelled;
        }

        noteRun(database, runId, {
            {QStringLiteral("jobsCompleted"), byStatus.value(QStringLiteral("completed"))},
            {QStringLiteral("jobsFailed"), byStatus.value(QStringLiteral("failed"))},
            {QStringLiteral("jobsCancelled"), byStatus.value(QStringLiteral("cancelled"))},
        });
        finishRun(database, runId, status, status == RunStatus::completed ? QString() : lastError);
        return true;
    });
}

QStringList settleRuns(CoreDatabase &database)
{
    QStringList settled;

    for (const QVariantMap &row : database.prepare(QStringLiteral("SELECT id FROM knowledge_librarian_runs WHERE status = 'running'")).all()) {
        const QString id = row.value(QStringLiteral("id")).toString();

        if (settleRun(database, id)) {
            settled.append(id);
        }
    }

    return settled;
}

// ---- Research topics -------------------------------------------------------

QString topicKey(const QString &topic)
{
    return KnowledgeText::normalizedStatement(topic);
}

QVariantMap upsertTopic(CoreDatabase &database, const TopicDraft &draft)
{
    return Transactions::join(database, [&]() {
        const QString key = topicKey(draft.topic);
        const auto existing = database.prepare(QStringLiteral("SELECT %1 FROM knowledge_librarian_topics WHERE library_id = ? AND topic_key = ?").arg(topicColumns))
                                  .bind(1, draft.libraryId)
                                  .bind(2, key)
                                  .one();

        if (!existing) {
            const QString id = newId();
            database
                .prepare(QStringLiteral(
                    "INSERT INTO knowledge_librarian_topics (id, library_id, topic, topic_key, origin, scope, depth, parent_topic_id, evidence_fingerprint, page_id) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                ))
                .bind(1, id)
                .bind(2, draft.libraryId)
                .bind(3, draft.topic.simplified().left(300))
                .bind(4, key)
                .bind(5, draft.origin)
                .bind(6, JsonValues::toJsonString(draft.scope))
                .bind(7, std::max(0, draft.depth))
                .bind(8, draft.parentTopicId.isEmpty() ? QVariant() : QVariant(draft.parentTopicId))
                .bind(9, draft.evidenceFingerprint.isEmpty() ? QVariant() : QVariant(draft.evidenceFingerprint))
                .bind(10, draft.pageId.isEmpty() ? QVariant() : QVariant(draft.pageId))
                .run();
            return *topic(database, id);
        }

        const QVariantMap current = topicMap(*existing);
        const QString id = current.value(QStringLiteral("id")).toString();
        const QString status = current.value(QStringLiteral("status")).toString();
        const QString investigated = current.value(QStringLiteral("investigatedFingerprint")).toString();
        const bool evidenceChanged = !draft.evidenceFingerprint.isEmpty() && !investigated.isEmpty() && draft.evidenceFingerprint != investigated;
        const bool settled = status == TopicStatus::covered || status == TopicStatus::dismissed || status == TopicStatus::exhausted;

        database
            .prepare(QStringLiteral(
                         "UPDATE knowledge_librarian_topics SET scope = ?, evidence_fingerprint = COALESCE(?, evidence_fingerprint), "
                         "page_id = COALESCE(?, page_id), status = ?, updated_at = %1 WHERE id = ?"
            )
                         .arg(nowSql))
            .bind(1, JsonValues::toJsonString(draft.scope.isEmpty() ? current.value(QStringLiteral("scope")).toMap() : draft.scope))
            .bind(2, draft.evidenceFingerprint.isEmpty() ? QVariant() : QVariant(draft.evidenceFingerprint))
            .bind(3, draft.pageId.isEmpty() ? QVariant() : QVariant(draft.pageId))
            .bind(4, evidenceChanged && settled ? TopicStatus::needsRevisit : status)
            .bind(5, id)
            .run();
        return *topic(database, id);
    });
}

std::optional<QVariantMap> topic(CoreDatabase &database, const QString &topicId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM knowledge_librarian_topics WHERE id = ?").arg(topicColumns)).bind(1, topicId).one();
    return row ? std::optional<QVariantMap>(topicMap(*row)) : std::nullopt;
}

QVariantList topics(CoreDatabase &database, const QString &libraryId, const QString &status, int limit)
{
    QVariantList result;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                                   "SELECT %1 FROM knowledge_librarian_topics WHERE library_id = ?1 AND (?2 = '' OR status = ?2) "
                                                   "ORDER BY updated_at DESC, id LIMIT ?3"
                                      )
                                                   .arg(topicColumns))
                                      .bind(1, libraryId)
                                      .bind(2, status)
                                      .bind(3, std::max(1, limit))
                                      .all()) {
        result.append(topicMap(row));
    }

    return result;
}

void updateTopic(CoreDatabase &database, const QString &topicId, const QVariantMap &fields)
{
    static const QHash<QString, QString> columns{
        {QStringLiteral("status"), QStringLiteral("status")},
        {QStringLiteral("pageId"), QStringLiteral("page_id")},
        {QStringLiteral("investigatedFingerprint"), QStringLiteral("investigated_fingerprint")},
        {QStringLiteral("findings"), QStringLiteral("findings")},
        {QStringLiteral("openQuestions"), QStringLiteral("open_questions")},
        {QStringLiteral("lastJobId"), QStringLiteral("last_job_id")},
        {QStringLiteral("lastProposalId"), QStringLiteral("last_proposal_id")},
        {QStringLiteral("attempts"), QStringLiteral("attempts")},
        {QStringLiteral("investigatedAt"), QStringLiteral("investigated_at")},
    };

    if (fields.isEmpty()) {
        return;
    }

    QStringList assignments;
    QVariantList values;

    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString column = columns.value(it.key());

        if (column.isEmpty()) {
            throw CoreError(QStringLiteral("Unknown topic field: %1").arg(it.key()));
        }

        assignments.append(column + QStringLiteral(" = ?"));
        const QVariant value = it.value();
        values.append(value.typeId() == QMetaType::QVariantList ? QVariant(JsonValues::toJsonString(value)) : isNullish(value) ? QVariant() : value);
    }

    auto statement = database.prepare(QStringLiteral("UPDATE knowledge_librarian_topics SET %1, updated_at = %2 WHERE id = ?").arg(assignments.join(QStringLiteral(", ")), nowSql));
    statement.bindAll(values);
    statement.bind(static_cast<int>(values.size()) + 1, topicId);
    statement.run();
}

// ---- Findings ----------------------------------------------------------------

namespace
{
QString findingPrint(const QString &kind, const QStringList &identity)
{
    return KnowledgeText::sha256(QStringList{kind, identity.join(QLatin1Char('\x1e'))}.join(QLatin1Char('\x1f')));
}
}

bool linkFinding(CoreDatabase &database, const QString &libraryId, const QString &kind, const QStringList &identity, const QString &proposalId)
{
    if (proposalId.isEmpty()) {
        return false;
    }

    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_librarian_findings SET proposal_id = ?, status = 'proposed', updated_at = %1 "
                     "WHERE library_id = ? AND fingerprint = ? AND status IN ('open', 'proposed')"
        )
                     .arg(nowSql))
        .bind(1, proposalId)
        .bind(2, libraryId)
        .bind(3, findingPrint(kind, identity))
        .run();
    return database.changes() > 0;
}

FindingWrite recordFinding(CoreDatabase &database, const FindingDraft &draft)
{
    return Transactions::join(database, [&]() {
        FindingWrite write;
        const QString print = findingPrint(draft.kind, draft.identity);
        const auto existing = database.prepare(QStringLiteral("SELECT id, status, proposal_id FROM knowledge_librarian_findings WHERE library_id = ? AND fingerprint = ?"))
                                  .bind(1, draft.libraryId)
                                  .bind(2, print)
                                  .one();

        if (existing) {
            write.findingId = existing->value(QStringLiteral("id")).toString();
            const QString status = existing->value(QStringLiteral("status")).toString();

            // A person's answer (dismissed) or an applied fix (resolved) stands.
            if (status == FindingStatus::open || status == FindingStatus::proposed) {
                const bool proposes = !draft.proposalId.isEmpty();
                database
                    .prepare(QStringLiteral(
                                 "UPDATE knowledge_librarian_findings SET detail = ?, evidence = ?, proposal_id = COALESCE(?, proposal_id), "
                                 "status = CASE WHEN ? THEN 'proposed' ELSE status END, run_id = COALESCE(?, run_id), updated_at = %1 WHERE id = ?"
                    )
                                 .arg(nowSql))
                    .bind(1, draft.detail)
                    .bind(2, JsonValues::toJsonString(draft.evidence))
                    .bind(3, proposes ? QVariant(draft.proposalId) : QVariant())
                    .bind(4, proposes ? 1 : 0)
                    .bind(5, draft.runId.isEmpty() ? QVariant() : QVariant(draft.runId))
                    .bind(6, write.findingId)
                    .run();
            }

            return write;
        }

        write.findingId = newId();
        write.created = true;
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_librarian_findings (id, library_id, librarian, kind, status, title, detail, page_id, claim_id, related_claim_id, "
                "evidence, proposal_id, run_id, job_id, fingerprint) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            ))
            .bind(1, write.findingId)
            .bind(2, draft.libraryId)
            .bind(3, draft.librarian)
            .bind(4, draft.kind)
            .bind(5, draft.proposalId.isEmpty() ? FindingStatus::open : FindingStatus::proposed)
            .bind(6, draft.title.simplified().left(300))
            .bind(7, draft.detail.left(4000))
            .bind(8, draft.pageId.isEmpty() ? QVariant() : QVariant(draft.pageId))
            .bind(9, draft.claimId.isEmpty() ? QVariant() : QVariant(draft.claimId))
            .bind(10, draft.relatedClaimId.isEmpty() ? QVariant() : QVariant(draft.relatedClaimId))
            .bind(11, JsonValues::toJsonString(draft.evidence))
            .bind(12, draft.proposalId.isEmpty() ? QVariant() : QVariant(draft.proposalId))
            .bind(13, draft.runId.isEmpty() ? QVariant() : QVariant(draft.runId))
            .bind(14, draft.jobId.isEmpty() ? QVariant() : QVariant(draft.jobId))
            .bind(15, print)
            .run();
        countRun(database, draft.runId, QStringLiteral("findings"));
        return write;
    });
}

QVariantList findings(CoreDatabase &database, const QString &libraryId, const QString &status, int limit)
{
    QVariantList result;
    const bool all = status == QStringLiteral("all");
    const QString filter = status.isEmpty() ? QStringLiteral("f.status IN ('open', 'proposed')")
                           : all            ? QStringLiteral("(?2 IS NULL OR 1 = 1)")
                                            : QStringLiteral("f.status = ?2");

    auto statement = database.prepare(QStringLiteral(
                                          "SELECT f.id, f.library_id AS libraryId, f.librarian, f.kind, f.status, f.title, f.detail, f.page_id AS pageId, "
                                          "p.title AS pageTitle, f.claim_id AS claimId, c.statement AS claimStatement, f.related_claim_id AS relatedClaimId, "
                                          "f.evidence, f.proposal_id AS proposalId, x.status AS proposalStatus, f.run_id AS runId, f.job_id AS jobId, "
                                          "f.created_at AS createdAt, f.updated_at AS updatedAt, f.resolved_at AS resolvedAt "
                                          "FROM knowledge_librarian_findings f "
                                          "LEFT JOIN knowledge_pages p ON p.id = f.page_id "
                                          "LEFT JOIN knowledge_claims c ON c.id = f.claim_id "
                                          "LEFT JOIN knowledge_proposals x ON x.id = f.proposal_id "
                                          "WHERE f.library_id = ?1 AND %1 ORDER BY f.created_at DESC, f.id LIMIT ?3"
    )
                                          .arg(filter));
    statement.bind(1, libraryId);

    if (!status.isEmpty() && !all) {
        statement.bind(2, status);
    }

    statement.bind(3, std::max(1, limit));

    for (const QVariantMap &row : statement.all()) {
        QVariantMap map = camel(row);
        map.insert(QStringLiteral("evidence"), JsonValues::parse(text(row, "evidence")).toList());
        result.append(map);
    }

    return result;
}

bool setFindingStatus(CoreDatabase &database, const QString &findingId, const QString &status)
{
    if (status != FindingStatus::dismissed && status != FindingStatus::open) {
        throw CoreError(QStringLiteral("A finding can only be dismissed or reopened."));
    }

    database
        .prepare(QStringLiteral(
                     "UPDATE knowledge_librarian_findings SET status = CASE WHEN ? = 'open' AND proposal_id IS NOT NULL "
                     "AND (SELECT status FROM knowledge_proposals WHERE id = proposal_id) = 'pending' THEN 'proposed' ELSE ? END, "
                     "resolved_at = NULL, updated_at = %1 WHERE id = ?"
        )
                     .arg(nowSql))
        .bind(1, status)
        .bind(2, status)
        .bind(3, findingId)
        .run();
    return database.changes() == 1;
}

QStringList librarianProposalIds(CoreDatabase &database, const QString &libraryId)
{
    QStringList ids;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                          "SELECT x.id FROM knowledge_proposals x LEFT JOIN knowledge_jobs j ON j.id = x.job_id "
                                          "WHERE x.library_id = ? AND x.status = 'pending' AND (j.librarian IS NOT NULL OR x.actor_id LIKE 'librarian:%') "
                                          "ORDER BY x.created_at DESC, x.id"
                                      ))
                                      .bind(1, libraryId)
                                      .all()) {
        ids.append(row.value(QStringLiteral("id")).toString());
    }

    return ids;
}

void syncWithProposals(CoreDatabase &database, const QString &libraryId)
{
    Transactions::join(database, [&]() {
        const QString proposalStatus = QStringLiteral("(SELECT x.status FROM knowledge_proposals x WHERE x.id = %1)");
        const QString findingProposal = proposalStatus.arg(QStringLiteral("f.proposal_id"));
        database
            .prepare(QStringLiteral(
                         "UPDATE knowledge_librarian_findings AS f SET "
                         "status = CASE %1 WHEN 'applied' THEN 'resolved' WHEN 'rejected' THEN 'dismissed' ELSE 'open' END, "
                         "resolved_at = CASE %1 WHEN 'applied' THEN %2 ELSE NULL END, updated_at = %2 "
                         "WHERE f.library_id = ? AND f.status = 'proposed' AND COALESCE(%1, '') != 'pending'"
            )
                         .arg(findingProposal, nowSql))
            .bind(1, libraryId)
            .run();

        const QString topicProposal = proposalStatus.arg(QStringLiteral("t.last_proposal_id"));
        database
            .prepare(QStringLiteral(
                         "UPDATE knowledge_librarian_topics AS t SET "
                         "status = CASE %1 WHEN 'applied' THEN 'covered' WHEN 'rejected' THEN 'dismissed' ELSE 'open' END, "
                         "page_id = COALESCE((SELECT x.target_page_id FROM knowledge_proposals x WHERE x.id = t.last_proposal_id AND x.status = 'applied'), t.page_id), "
                         "updated_at = %2 WHERE t.library_id = ? AND t.status = 'proposed' AND COALESCE(%1, '') != 'pending'"
            )
                         .arg(topicProposal, nowSql))
            .bind(1, libraryId)
            .run();

        // A topic whose investigation job ended without recording anything
        // (cancelled, failed, budget) can be picked up again.
        database
            .prepare(QStringLiteral(
                         "UPDATE knowledge_librarian_topics AS t SET status = 'open', updated_at = %1 WHERE t.library_id = ? AND t.status = 'queued' "
                         "AND NOT EXISTS (SELECT 1 FROM knowledge_jobs j WHERE j.id = t.last_job_id AND j.status IN ('queued', 'running'))"
            )
                         .arg(nowSql))
            .bind(1, libraryId)
            .run();
    });
}

// ---- Usage -------------------------------------------------------------------

QVariantMap Usage::toVariant() const
{
    return {
        {QStringLiteral("calls"), calls},
        {QStringLiteral("inputTokens"), inputTokens},
        {QStringLiteral("outputTokens"), outputTokens},
        {QStringLiteral("tokens"), tokens()},
        {QStringLiteral("costUsd"), costUsd},
    };
}

QString usageDay(const QDateTime &when)
{
    return when.toUTC().date().toString(Qt::ISODate);
}

Usage dailyUsage(CoreDatabase &database, const QString &libraryId, const QString &day)
{
    Usage usage;
    const auto row = database.prepare(QStringLiteral("SELECT model_calls, input_tokens, output_tokens, estimated_cost_usd FROM knowledge_librarian_usage WHERE library_id = ? AND day = ?"))
                         .bind(1, libraryId)
                         .bind(2, day)
                         .one();

    if (row) {
        usage.calls = row->value(QStringLiteral("model_calls")).toInt();
        usage.inputTokens = row->value(QStringLiteral("input_tokens")).toInt();
        usage.outputTokens = row->value(QStringLiteral("output_tokens")).toInt();
        usage.costUsd = row->value(QStringLiteral("estimated_cost_usd")).toDouble();
    }

    return usage;
}

void addUsage(CoreDatabase &database, const QString &libraryId, const QString &day, const Usage &usage)
{
    database
        .prepare(QStringLiteral(
            "INSERT INTO knowledge_librarian_usage (library_id, day, model_calls, input_tokens, output_tokens, estimated_cost_usd) VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT (library_id, day) DO UPDATE SET model_calls = model_calls + excluded.model_calls, input_tokens = input_tokens + excluded.input_tokens, "
            "output_tokens = output_tokens + excluded.output_tokens, estimated_cost_usd = estimated_cost_usd + excluded.estimated_cost_usd"
        ))
        .bind(1, libraryId)
        .bind(2, day)
        .bind(3, std::max(0, usage.calls))
        .bind(4, std::max(0, usage.inputTokens))
        .bind(5, std::max(0, usage.outputTokens))
        .bind(6, std::max(0.0, usage.costUsd))
        .run();
}

QVariantMap usageSummary(CoreDatabase &database, const QString &libraryId, const QDateTime &now)
{
    Usage month;
    const auto row = database
                         .prepare(QStringLiteral(
                             "SELECT COALESCE(SUM(model_calls), 0) AS calls, COALESCE(SUM(input_tokens), 0) AS input, COALESCE(SUM(output_tokens), 0) AS output, "
                             "COALESCE(SUM(estimated_cost_usd), 0) AS cost FROM knowledge_librarian_usage WHERE library_id = ? AND day >= ?"
                         ))
                         .bind(1, libraryId)
                         .bind(2, usageDay(now.addDays(-29)))
                         .one();

    if (row) {
        month.calls = row->value(QStringLiteral("calls")).toInt();
        month.inputTokens = row->value(QStringLiteral("input")).toInt();
        month.outputTokens = row->value(QStringLiteral("output")).toInt();
        month.costUsd = row->value(QStringLiteral("cost")).toDouble();
    }

    return {
        {QStringLiteral("today"), dailyUsage(database, libraryId, usageDay(now)).toVariant()},
        {QStringLiteral("last30Days"), month.toVariant()},
    };
}
}
