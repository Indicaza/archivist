#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/cognition/runs/models/AIRun.ts.
// Runs and events keep the old JSON shape: runs carry contextCompiler
// {id, version}; events carry {id, runId, sequence, eventType, payload, createdAt}.
namespace AIRunRepository
{
[[nodiscard]] bool isTerminal(const QString &status);
[[nodiscard]] std::optional<QVariantMap> runById(CoreDatabase &database, const QString &runId);
QVariantMap requireRun(CoreDatabase &database, const QString &runId);
[[nodiscard]] QVariantList runsByChat(CoreDatabase &database, const QString &chatId, int limit = 50);
[[nodiscard]] std::optional<QVariantMap> activeRunByChat(CoreDatabase &database, const QString &chatId);

// input: chatId, libraryId (may be null), agentId, userMessageId,
// assistantMessageId, contextCompiler {id, version}, provider, model.
// Throws with details {runId} when the Chat already has an active run.
QVariantMap createRun(CoreDatabase &database, const QVariantMap &input);

// Appends an event (sequence = max + 1) and moves the run phase for phase events.
QVariantMap appendEvent(CoreDatabase &database, const QString &runId, const QString &eventType, const QVariantMap &payload = {});
[[nodiscard]] QVariantList events(CoreDatabase &database, const QString &runId, int afterSequence = 0);

QVariantMap completeRun(CoreDatabase &database, const QString &runId, const QString &finalResponse, const QVariant &contextRunId);
QVariantMap cancelRun(CoreDatabase &database, const QString &runId, const QString &finalResponse);
QVariantMap failRun(
    CoreDatabase &database,
    const QString &runId,
    const QString &errorCode,
    const QString &errorMessage,
    const QString &finalResponse
);

// Settles runs left 'running' by a previous process. Returns how many.
int recoverInterruptedRuns(CoreDatabase &database);
}
