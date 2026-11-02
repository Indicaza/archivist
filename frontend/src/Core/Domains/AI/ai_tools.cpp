#include "ai_tools.h"
#include "ai_tools_definition.h"
#include "ai_tools_input.h"
#include "ai_tools_read.h"

#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QElapsedTimer>
#include <cmath>
#include <optional>

// The executor: validates input, enforces permissions, runs the tool's
// handler, translates failures and cancellation into ToolErrors, records
// ai_tool_executions and emits the tool.* lifecycle events. Tool
// definitions live with their families (see ai_tools_definition.h).
using namespace CoreValues;
using AITools::Context;
using AITools::Definition;
using AITools::ToolError;
using AITools::Input::InvalidInput;
using AITools::ReadTools::ToolCancelled;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

// ---- ai_tool_executions ----

const QString executionSelect = QStringLiteral(
    "SELECT id, run_id, tool_id, permission_level, status, input_json, output_json, error_code, error_message, "
    "requested_at, started_at, completed_at, duration_ms FROM ai_tool_executions "
);

QVariant parseStoredJson(const QVariant &value)
{
    if (isNullish(value)) {
        return QVariant::fromValue(nullptr);
    }

    bool ok = false;
    const QVariant parsed = JsonValues::parse(value.toString(), &ok);
    return ok ? parsed : QVariant::fromValue(nullptr);
}

QVariantMap mapExecution(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("runId"), row.value(QStringLiteral("run_id"))},
        {QStringLiteral("toolId"), row.value(QStringLiteral("tool_id"))},
        {QStringLiteral("permission"), row.value(QStringLiteral("permission_level"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("input"), parseStoredJson(row.value(QStringLiteral("input_json")))},
        {QStringLiteral("output"), parseStoredJson(row.value(QStringLiteral("output_json")))},
        {QStringLiteral("errorCode"), nullable(row.value(QStringLiteral("error_code")))},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
        {QStringLiteral("requestedAt"), row.value(QStringLiteral("requested_at"))},
        {QStringLiteral("startedAt"), nullable(row.value(QStringLiteral("started_at")))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("durationMs"), nullable(row.value(QStringLiteral("duration_ms")))},
    };
}

QVariantMap requireExecution(CoreDatabase &database, const QString &executionId)
{
    const auto row = database.prepare(executionSelect + QStringLiteral("WHERE id = ?")).bind(1, executionId).one();

    if (!row) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be loaded.").arg(executionId));
    }

    return mapExecution(*row);
}

QString serialized(const QVariant &value)
{
    return value.isValid() ? JsonValues::toJsonString(value) : QStringLiteral("null");
}

double elapsedMs(const QElapsedTimer &timer)
{
    return std::round(timer.nsecsElapsed() / 1000.0) / 1000.0;
}

void finishFailed(Context &context, const Definition &tool, const ToolError &error, double durationMs)
{
    CoreDatabase &database = *context.database;
    const bool cancelled = error.code() == QStringLiteral("cancelled");
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = ?, error_code = ?, error_message = ?, completed_at = %1, duration_ms = ? "
            "WHERE id = ? AND status IN ('requested', 'running')"
        ).arg(nowSql))
        .bind(1, cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"))
        .bind(2, error.code())
        .bind(3, error.message())
        .bind(4, durationMs)
        .bind(5, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be finalized.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(cancelled ? QStringLiteral("tool.cancelled") : QStringLiteral("tool.failed"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("errorCode"), error.code()},
            {QStringLiteral("message"), error.message()},
            {QStringLiteral("durationMs"), durationMs},
        });
    }
}

QVariantMap safeSummary(QVariantMap (*summarize)(const QVariantMap &), const QVariantMap &value)
{
    try {
        return summarize ? summarize(value) : QVariantMap();
    } catch (...) {
        return {{QStringLiteral("summaryUnavailable"), true}};
    }
}
}

namespace AITools
{
QVariantMap execute(Context context, const QString &toolId, const QVariant &input)
{
    const Definition *found = findTool(toolId);

    if (!found) {
        throw ToolError(QStringLiteral("tool_not_found"), QStringLiteral("AI tool \"%1\" is not registered.").arg(toolId));
    }

    const Definition &tool = *found;
    CoreDatabase &database = *context.database;
    QElapsedTimer timer;
    timer.start();
    context.executionId = newId();
    database
        .prepare(QStringLiteral(
            "INSERT INTO ai_tool_executions (id, run_id, tool_id, permission_level, status, input_json) "
            "VALUES (?, ?, ?, ?, 'requested', ?)"
        ))
        .bind(1, context.executionId)
        .bind(2, context.runId)
        .bind(3, tool.id)
        .bind(4, tool.permission)
        .bind(5, serialized(input))
        .run();

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.requested"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("permission"), tool.permission},
            {QStringLiteral("executionMode"), tool.executionMode},
        });
    }

    QVariantMap parsed;

    try {
        parsed = tool.parse(input);
    } catch (const InvalidInput &issue) {
        const ToolError error(
            QStringLiteral("invalid_input"),
            QStringLiteral("AI tool \"%1\" received invalid input.").arg(tool.id),
            {{QStringLiteral("issues"), QVariantList{issue.issue}}}
        );
        finishFailed(context, tool, error, elapsedMs(timer));
        throw error;
    }

    // Workspace changes only move views between panels: they never touch
    // files, and the workspace can undo them.
    const bool permitted = tool.permission == AITools::readOnlyPermission
        || (tool.permission == AITools::safeLocalMutationPermission && tool.executionMode == AITools::proposalMode)
        || (tool.permission == AITools::safeLocalMutationPermission && tool.executionMode == AITools::workspaceMode);

    if (!permitted) {
        const ToolError error(
            QStringLiteral("permission_denied"),
            QStringLiteral("AI tool \"%1\" requires %2 permission.").arg(tool.id, tool.permission),
            {{QStringLiteral("requiredPermission"), tool.permission}, {QStringLiteral("grantedPermissions"), QVariantList{QStringLiteral("read-only")}}}
        );
        finishFailed(context, tool, error, elapsedMs(timer));
        throw error;
    }

    const QVariantMap inputSummary = safeSummary(tool.summarizeInput, parsed);
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = 'running', started_at = %1 WHERE id = ? AND status = 'requested'"
        ).arg(nowSql))
        .bind(1, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be started.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.started"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("permission"), tool.permission},
            {QStringLiteral("executionMode"), tool.executionMode},
            {QStringLiteral("input"), inputSummary},
        });
    }

    std::optional<ToolError> failure;
    QVariantMap output;

    try {
        output = tool.run(parsed, context);

        if (context.cancelled && context.cancelled()) {
            throw ToolCancelled{};
        }
    } catch (const ToolCancelled &) {
        failure.emplace(QStringLiteral("cancelled"), QStringLiteral("The AI tool was cancelled."));
    } catch (const ToolError &error) {
        failure = error;
    } catch (const CoreError &error) {
        failure.emplace(
            QStringLiteral("execution_failed"),
            JsCompat::isBlank(error.message()) ? QStringLiteral("The AI tool failed unexpectedly.") : error.message()
        );
    } catch (const std::exception &error) {
        const QString message = QString::fromUtf8(error.what());
        failure.emplace(
            QStringLiteral("execution_failed"),
            JsCompat::isBlank(message) ? QStringLiteral("The AI tool failed unexpectedly.") : message
        );
    }

    if (failure && context.cancelled && context.cancelled()) {
        failure.emplace(QStringLiteral("cancelled"), QStringLiteral("The AI tool was cancelled."));
    }

    if (failure) {
        finishFailed(context, tool, *failure, elapsedMs(timer));
        throw *failure;
    }

    const double durationMs = elapsedMs(timer);
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = 'completed', output_json = ?, error_code = NULL, error_message = NULL, "
            "completed_at = %1, duration_ms = ? WHERE id = ? AND status = 'running'"
        ).arg(nowSql))
        .bind(1, serialized(output))
        .bind(2, durationMs)
        .bind(3, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be completed.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.completed"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("durationMs"), durationMs},
            {QStringLiteral("output"), safeSummary(tool.summarizeOutput, output)},
        });
    }

    QVariantMap execution = requireExecution(database, context.executionId);
    execution.insert(QStringLiteral("output"), output);
    return execution;
}

QVariantList executionsByRun(CoreDatabase &database, const QString &runId)
{
    QVariantList result;

    for (const QVariantMap &row :
         database.prepare(executionSelect + QStringLiteral("WHERE run_id = ? ORDER BY requested_at ASC, rowid ASC")).bind(1, runId).all()) {
        result.append(mapExecution(row));
    }

    return result;
}
}
