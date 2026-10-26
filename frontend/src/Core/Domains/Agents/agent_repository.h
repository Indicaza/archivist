#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/agents (model + controller validation).
// Agents come back shaped like the old JSON: id, name, description, identity,
// profession, doctrine, outputContract, systemInstructions, generation,
// context {compiler {id, version}, config}, isBuiltIn, archivedAt, createdAt,
// updatedAt.
namespace AgentRepository
{
[[nodiscard]] QVariantList activeAgents(CoreDatabase &database);
[[nodiscard]] QVariantList archivedAgents(CoreDatabase &database);
[[nodiscard]] std::optional<QVariantMap> agentById(CoreDatabase &database, const QString &agentId);

// `input` matches the old POST/PATCH body. For update, a missing key keeps
// the current value; nested objects merge into the current ones.
// `modelCachePath` is ai-model-cache.json; empty means next to the database.
QVariantMap createAgent(CoreDatabase &database, const QVariantMap &input, const QString &modelCachePath = {});
QVariantMap updateAgent(
    CoreDatabase &database,
    const QString &agentId,
    const QVariantMap &input,
    const QString &modelCachePath = {}
);
QVariantMap duplicateAgent(CoreDatabase &database, const QString &agentId, const QVariantMap &input = {});
QVariantMap archiveAgent(CoreDatabase &database, const QString &agentId);
QVariantMap restoreAgent(CoreDatabase &database, const QString &agentId);
// Returns the number of Chats moved to the default Agent.
int deleteAgent(CoreDatabase &database, const QString &agentId);
}
