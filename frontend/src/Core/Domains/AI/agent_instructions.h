#pragma once

#include <QString>
#include <QVariantMap>

// Native port of backend/src/api/agents/services/AgentInstructionBuilder.ts.
namespace AgentInstructions
{
// `agent` is shaped like AgentRepository::agentById.
[[nodiscard]] QString build(const QVariantMap &agent);
}
