#pragma once

#include "Core/Database/core_database.h"

#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <stdexcept>

// Native port of backend/src/core/tools: the tool registry, the executor that
// records ai_tool_executions and emits tool.* events, the six read-only
// Library tools and the five proposal-only edit tools.
namespace AITools
{
// AIToolError: code is one of tool_not_found, invalid_input,
// permission_denied, invalid_output, timed_out, cancelled, execution_failed.
class ToolError final : public std::runtime_error
{
public:
    ToolError(QString code, const QString &message, QVariantMap details = {})
        : std::runtime_error(message.toStdString())
        , m_code(std::move(code))
        , m_message(message)
        , m_details(std::move(details))
    {
    }

    [[nodiscard]] const QString &code() const noexcept { return m_code; }
    [[nodiscard]] const QString &message() const noexcept { return m_message; }
    [[nodiscard]] const QVariantMap &details() const noexcept { return m_details; }

private:
    QString m_code;
    QString m_message;
    QVariantMap m_details;
};

struct Context
{
    CoreDatabase *database = nullptr;
    QString runId;
    QString chatId;
    QString libraryId; // empty when the Chat has no Library
    QString executionId; // set by execute()
    std::function<bool()> cancelled;
    // Appends a run event (tool.*, edit.proposed).
    std::function<void(const QString &eventType, const QVariantMap &payload)> emitEvent;
};

struct Availability
{
    bool discovery = true;
    bool fullFileRead = true;
    bool verification = true;
    bool mutationProposals = false;
};

// listModelAvailableAITools: [{name, description, parameters}], sorted by id.
[[nodiscard]] QVariantList providerTools(const Availability &availability);
[[nodiscard]] bool isMutationProposalTool(const QString &toolId);
[[nodiscard]] QStringList toolIds();

// AIToolExecutor.execute. Returns the stored execution {id, runId, toolId,
// permission, status, input, output, ...}. Throws ToolError.
QVariantMap execute(Context context, const QString &toolId, const QVariant &input);

[[nodiscard]] QVariantList executionsByRun(CoreDatabase &database, const QString &runId);
}
