#pragma once

#include "Core/Database/core_database.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Native port of backend/src/core/cognition/conscious/context: the six
// registered Context Compilers, estimateTokens and the Chat-message FTS
// retrieval tool that the auto-balanced compilers use.
namespace ContextCompilers
{
struct SourceMessage
{
    QString id;
    QString role;
    QString content;
    QString createdAt;
};

struct ProviderMessage
{
    QString role;
    QString content;
};

struct Compiled
{
    QList<ProviderMessage> providerMessages;
    // {compiler {id, version, name}, includedMessageIds, omittedMessageIds,
    //  includedMessageCount, omittedMessageCount, estimatedInputTokens,
    //  responseTokenReserve, totalBudget, compilationTimeMs}
    QVariantMap manifest;
    QStringList warnings;
};

// Math.max(1, Math.ceil(length / 4)), or 0 for empty text.
[[nodiscard]] int estimateTokens(const QString &content);

// {id, version, name}; throws when the compiler is not registered.
[[nodiscard]] QVariantMap descriptor(const QString &id, int version);

// Validates the config and fills schema defaults, like registry.parseConfig.
[[nodiscard]] QVariantMap parseConfig(const QString &id, int version, const QVariantMap &config);

// Runs the compiler. `config` must already be parsed.
[[nodiscard]] Compiled compile(
    CoreDatabase &database,
    const QString &id,
    int version,
    const QString &chatId,
    const QString &currentMessageId,
    const QList<SourceMessage> &messages,
    const QVariantMap &config
);

// ChatMessageFtsTool.search: {tool, query, candidates, searchedAt, durationMs, warnings}.
// Candidates: {id, source, content, estimatedTokens, score, reason, metadata}.
[[nodiscard]] QVariantMap searchChatMessages(CoreDatabase &database, const QString &query, const QString &chatId, int limit);
}
