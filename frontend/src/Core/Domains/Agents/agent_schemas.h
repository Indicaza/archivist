#pragma once

#include <QVariant>
#include <QVariantMap>
#include <optional>

// Native copy of backend/src/api/agents/schemas/AgentSchemas.ts.
// Each function validates a value and returns it normalized the way zod did
// (strings trimmed, unknown keys dropped), or nullopt when it is invalid.
// `partial` accepts any subset of keys, like zod's .partial().
namespace AgentSchemas
{
[[nodiscard]] std::optional<QVariantMap> identity(const QVariant &value, bool partial);
[[nodiscard]] std::optional<QVariantMap> profession(const QVariant &value, bool partial);
[[nodiscard]] std::optional<QVariantMap> outputContract(const QVariant &value, bool partial);
[[nodiscard]] std::optional<QVariantMap> generation(const QVariant &value, bool partial);
[[nodiscard]] std::optional<QVariantMap> context(const QVariant &value);

// The create/update body: name, description, identity, profession, doctrine,
// outputContract, systemInstructions, generation, context. Missing keys stay
// missing; a present null stays null.
[[nodiscard]] std::optional<QVariantMap> agentInput(const QVariantMap &input);

// Duplicate body: optional trimmed name.
[[nodiscard]] std::optional<QVariantMap> duplicateInput(const QVariantMap &input);

[[nodiscard]] bool isNumber(const QVariant &value);
[[nodiscard]] bool isString(const QVariant &value);
[[nodiscard]] bool isNull(const QVariant &value);
}
