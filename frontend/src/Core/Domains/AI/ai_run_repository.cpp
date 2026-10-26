#include "ai_run_repository.h"

#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QSet>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString runSelect = QStringLiteral(
    "SELECT id, chat_id, library_id, agent_id, user_message_id, assistant_message_id, context_run_id, "
    "context_compiler_id, context_compiler_version, status, phase, provider, model, final_response, "
    "error_code, error_message, started_at, completed_at, cancelled_at FROM ai_runs "
);

const QString eventSelect = QStringLiteral(
    "SELECT id, run_id, sequence, event_type, payload_json, created_at FROM ai_run_events "
);

bool isPhaseEvent(const QString &eventType)
{
    static const QSet<QString> phases{
        QStringLiteral("run.started"),
        QStringLiteral("retrieval.started"),
        QStringLiteral("retrieval.completed"),
        QStringLiteral("context.started"),
        QStringLiteral("context.completed"),
        QStringLiteral("model.started"),
        QStringLiteral("model.completed"),
        QStringLiteral("tool.requested"),
        QStringLiteral("tool.started"),
        QStringLiteral("tool.completed"),
        QStringLiteral("tool.failed"),
        QStringLiteral("tool.cancelled"),
        QStringLiteral("run.cancelled"),
        QStringLiteral("run.failed"),
        QStringLiteral("run.completed"),
    };
    return phases.contains(eventType);
}

QVariantMap mapRun(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("chatId"), row.value(QStringLiteral("chat_id"))},
        {QStringLiteral("libraryId"), nullable(row.value(QStringLiteral("library_id")))},
        {QStringLiteral("agentId"), row.value(QStringLiteral("agent_id"))},
        {QStringLiteral("userMessageId"), row.value(QStringLiteral("user_message_id"))},
        {QStringLiteral("assistantMessageId"), row.value(QStringLiteral("assistant_message_id"))},
        {QStringLiteral("contextRunId"), nullable(row.value(QStringLiteral("context_run_id")))},
        {
            QStringLiteral("contextCompiler"),
            QVariantMap{
                {QStringLiteral("id"), row.value(QStringLiteral("context_compiler_id"))},
                {QStringLiteral("version"), row.value(QStringLiteral("context_compiler_version"))},
            },
        },
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("phase"), row.value(QStringLiteral("phase"))},
        {QStringLiteral("provider"), row.value(QStringLiteral("provider"))},
        {QStringLiteral("model"), row.value(QStringLiteral("model"))},
        {QStringLiteral("finalResponse"), nullable(row.value(QStringLiteral("final_response")))},
        {QStringLiteral("errorCode"), nullable(row.value(QStringLiteral("error_code")))},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
        {QStringLiteral("startedAt"), row.value(QStringLiteral("started_at"))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("cancelledAt"), nullable(row.value(QStringLiteral("cancelled_at")))},
    };
}

QVariantMap mapEvent(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("runId"), row.value(QStringLiteral("run_id"))},
        {QStringLiteral("sequence"), row.value(QStringLiteral("sequence"))},
        {QStringLiteral("eventType"), row.value(QStringLiteral("event_type"))},
        {QStringLiteral("payload"), JsonValues::parseObject(row.value(QStringLiteral("payload_json")).toString())},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
    };
}

qint64 nextSequence(CoreDatabase &database, const QString &runId)
{
    return database
        .prepare(QStringLiteral("SELECT COALESCE(MAX(sequence), 0) + 1 FROM ai_run_events WHERE run_id = ?"))
        .bind(1, runId)
        .scalar()
        .toLongLong();
}

void insertEvent(CoreDatabase &database, const QString &eventId, const QString &runId, const QString &eventType, const QVariantMap &payload)
{
    database
        .prepare(QStringLiteral("INSERT INTO ai_run_events (id, run_id, sequence, event_type, payload_json) VALUES (?, ?, ?, ?, ?)"))
        .bind(1, eventId)
        .bind(2, runId)
        .bind(3, nextSequence(database, runId))
        .bind(4, eventType)
        .bind(5, JsonValues::toJsonString(payload))
        .run();
}

// Runs work inside a transaction unless the caller already opened one.
template<typename Work>
auto inTransaction(CoreDatabase &database, Work &&work) -> decltype(work())
{
    if (database.inTransaction()) {
        return work();
    }

    return database.transaction(std::forward<Work>(work));
}
}

namespace AIRunRepository
{
bool isTerminal(const QString &status)
{
    return status == QStringLiteral("completed") || status == QStringLiteral("cancelled") || status == QStringLiteral("failed");
}

std::optional<QVariantMap> runById(CoreDatabase &database, const QString &runId)
{
    const auto row = database.prepare(runSelect + QStringLiteral("WHERE id = ?")).bind(1, runId).one();
    return row ? std::optional<QVariantMap>(mapRun(*row)) : std::nullopt;
}

QVariantMap requireRun(CoreDatabase &database, const QString &runId)
{
    const auto run = runById(database, runId);

    if (!run) {
        throw CoreError(QStringLiteral("AI Run not found."));
    }

    return *run;
}

QVariantList runsByChat(CoreDatabase &database, const QString &chatId, int limit)
{
    QVariantList runs;
    const auto rows = database
        .prepare(runSelect + QStringLiteral("WHERE chat_id = ? ORDER BY started_at DESC LIMIT ?"))
        .bind(1, chatId)
        .bind(2, limit)
        .all();

    for (const QVariantMap &row : rows) {
        runs.append(mapRun(row));
    }

    return runs;
}

std::optional<QVariantMap> activeRunByChat(CoreDatabase &database, const QString &chatId)
{
    const auto row = database
        .prepare(runSelect + QStringLiteral("WHERE chat_id = ? AND status = 'running' ORDER BY started_at DESC LIMIT 1"))
        .bind(1, chatId)
        .one();
    return row ? std::optional<QVariantMap>(mapRun(*row)) : std::nullopt;
}

QVariantMap createRun(CoreDatabase &database, const QVariantMap &input)
{
    const QString chatId = input.value(QStringLiteral("chatId")).toString();

    if (const auto active = activeRunByChat(database, chatId)) {
        throw CoreError(
            QStringLiteral("This Chat already has an active AI Run."),
            {{QStringLiteral("runId"), active->value(QStringLiteral("id"))}}
        );
    }

    const QVariantMap compiler = input.value(QStringLiteral("contextCompiler")).toMap();
    const QString runId = newId();
    const QVariant libraryId = input.value(QStringLiteral("libraryId"));

    database
        .prepare(QStringLiteral(
            "INSERT INTO ai_runs (id, chat_id, library_id, agent_id, user_message_id, assistant_message_id, "
            "context_compiler_id, context_compiler_version, status, phase, provider, model) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'running', 'run.started', ?, ?)"
        ))
        .bind(1, runId)
        .bind(2, chatId)
        .bind(3, isNullish(libraryId) ? QVariant() : libraryId)
        .bind(4, input.value(QStringLiteral("agentId")))
        .bind(5, input.value(QStringLiteral("userMessageId")))
        .bind(6, input.value(QStringLiteral("assistantMessageId")))
        .bind(7, compiler.value(QStringLiteral("id")))
        .bind(8, compiler.value(QStringLiteral("version")))
        .bind(9, input.value(QStringLiteral("provider")))
        .bind(10, input.value(QStringLiteral("model")))
        .run();

    return requireRun(database, runId);
}

QVariantMap appendEvent(CoreDatabase &database, const QString &runId, const QString &eventType, const QVariantMap &payload)
{
    return inTransaction(database, [&]() {
        requireRun(database, runId);
        const QString eventId = newId();
        insertEvent(database, eventId, runId, eventType, payload);

        if (isPhaseEvent(eventType)) {
            database.prepare(QStringLiteral("UPDATE ai_runs SET phase = ? WHERE id = ?")).bind(1, eventType).bind(2, runId).run();
        }

        const auto row = database.prepare(eventSelect + QStringLiteral("WHERE id = ?")).bind(1, eventId).one();

        if (!row) {
            throw CoreError(QStringLiteral("The newly created AI Run event could not be loaded."));
        }

        return mapEvent(*row);
    });
}

QVariantList events(CoreDatabase &database, const QString &runId, int afterSequence)
{
    requireRun(database, runId);
    QVariantList result;
    const auto rows = database
        .prepare(eventSelect + QStringLiteral("WHERE run_id = ? AND sequence > ? ORDER BY sequence ASC"))
        .bind(1, runId)
        .bind(2, afterSequence)
        .all();

    for (const QVariantMap &row : rows) {
        result.append(mapEvent(row));
    }

    return result;
}

QVariantMap completeRun(CoreDatabase &database, const QString &runId, const QString &finalResponse, const QVariant &contextRunId)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE ai_runs SET status = 'completed', phase = 'run.completed', final_response = ?, "
                     "context_run_id = ?, error_code = NULL, error_message = NULL, completed_at = %1 "
                     "WHERE id = ? AND status = 'running'"
        )
                     .arg(nowSql))
        .bind(1, finalResponse)
        .bind(2, isNullish(contextRunId) ? QVariant() : contextRunId)
        .bind(3, runId)
        .run();
    return requireRun(database, runId);
}

QVariantMap cancelRun(CoreDatabase &database, const QString &runId, const QString &finalResponse)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE ai_runs SET status = 'cancelled', phase = 'run.cancelled', final_response = ?, "
                     "error_code = NULL, error_message = NULL, completed_at = COALESCE(completed_at, %1), "
                     "cancelled_at = COALESCE(cancelled_at, %1) WHERE id = ? AND status IN ('running', 'cancelled')"
        )
                     .arg(nowSql))
        .bind(1, finalResponse)
        .bind(2, runId)
        .run();
    return requireRun(database, runId);
}

QVariantMap failRun(
    CoreDatabase &database,
    const QString &runId,
    const QString &errorCode,
    const QString &errorMessage,
    const QString &finalResponse
)
{
    database
        .prepare(QStringLiteral(
                     "UPDATE ai_runs SET status = 'failed', phase = 'run.failed', final_response = ?, error_code = ?, "
                     "error_message = ?, completed_at = %1 WHERE id = ? AND status = 'running'"
        )
                     .arg(nowSql))
        .bind(1, finalResponse)
        .bind(2, errorCode)
        .bind(3, errorMessage)
        .bind(4, runId)
        .run();
    return requireRun(database, runId);
}

int recoverInterruptedRuns(CoreDatabase &database)
{
    const auto rows = database
        .prepare(QStringLiteral(
            "SELECT ai_runs.id, ai_runs.assistant_message_id, messages.content AS message_content, "
            "messages.status AS message_status, context_runs.id AS context_run_id FROM ai_runs "
            "INNER JOIN messages ON messages.id = ai_runs.assistant_message_id "
            "LEFT JOIN context_runs ON context_runs.assistant_message_id = ai_runs.assistant_message_id "
            "WHERE ai_runs.status = 'running' ORDER BY ai_runs.started_at ASC"
        ))
        .all();

    if (rows.isEmpty()) {
        return 0;
    }

    inTransaction(database, [&]() {
        for (const QVariantMap &row : rows) {
            const QString runId = row.value(QStringLiteral("id")).toString();
            const QString assistantMessageId = row.value(QStringLiteral("assistant_message_id")).toString();
            const QString content = row.value(QStringLiteral("message_content")).toString();
            const QString status = row.value(QStringLiteral("message_status")).toString();
            const QVariant contextRunId = nullable(row.value(QStringLiteral("context_run_id")));

            if (status == QStringLiteral("complete")) {
                database
                    .prepare(QStringLiteral(
                                 "UPDATE ai_runs SET status = 'completed', phase = 'run.completed', final_response = ?, "
                                 "context_run_id = ?, completed_at = %1 WHERE id = ?"
                    )
                                 .arg(nowSql))
                    .bind(1, content)
                    .bind(2, isNullish(contextRunId) ? QVariant() : contextRunId)
                    .bind(3, runId)
                    .run();
                insertEvent(database, newId(), runId, QStringLiteral("run.completed"), {
                    {QStringLiteral("recovered"), true},
                    {QStringLiteral("assistantMessageId"), assistantMessageId},
                    {QStringLiteral("contextRunId"), contextRunId},
                    {QStringLiteral("characterCount"), content.size()},
                });
                continue;
            }

            if (status == QStringLiteral("cancelled")) {
                database
                    .prepare(QStringLiteral(
                                 "UPDATE ai_runs SET status = 'cancelled', phase = 'run.cancelled', final_response = ?, "
                                 "completed_at = %1, cancelled_at = %1 WHERE id = ?"
                    )
                                 .arg(nowSql))
                    .bind(1, content)
                    .bind(2, runId)
                    .run();
                insertEvent(database, newId(), runId, QStringLiteral("run.cancelled"), {
                    {QStringLiteral("recovered"), true},
                    {QStringLiteral("assistantMessageId"), assistantMessageId},
                    {QStringLiteral("characterCount"), content.size()},
                });
                continue;
            }

            QString streamed;
            const auto deltaRows = database
                .prepare(QStringLiteral(
                    "SELECT payload_json FROM ai_run_events WHERE run_id = ? AND event_type = 'model.delta' ORDER BY sequence ASC"
                ))
                .bind(1, runId)
                .all();

            for (const QVariantMap &deltaRow : deltaRows) {
                const QVariant delta = JsonValues::parseObject(deltaRow.value(QStringLiteral("payload_json")).toString())
                                           .value(QStringLiteral("delta"));
                streamed += delta.typeId() == QMetaType::QString ? delta.toString() : QString();
            }

            const QString partial = streamed.isEmpty() ? content : streamed;
            const QString message = QStringLiteral("Archivist stopped before the AI Run completed.");

            database
                .prepare(QStringLiteral("UPDATE messages SET content = ?, status = 'failed', updated_at = %1 WHERE id = ?").arg(nowSql))
                .bind(1, partial)
                .bind(2, assistantMessageId)
                .run();
            insertEvent(database, newId(), runId, QStringLiteral("run.failed"), {
                {QStringLiteral("recovered"), true},
                {QStringLiteral("errorCode"), QStringLiteral("process_interrupted")},
                {QStringLiteral("message"), message},
            });
            database
                .prepare(QStringLiteral(
                             "UPDATE ai_runs SET status = 'failed', phase = 'run.failed', final_response = ?, "
                             "error_code = 'process_interrupted', error_message = ?, completed_at = %1 WHERE id = ?"
                )
                             .arg(nowSql))
                .bind(1, partial)
                .bind(2, message)
                .bind(3, runId)
                .run();
        }
    });

    return static_cast<int>(rows.size());
}
}
