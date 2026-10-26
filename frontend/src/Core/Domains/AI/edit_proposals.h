#pragma once

#include "Core/Database/core_database.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/cognition/edits/models/AIEditProposal.ts and
// services/AIEditProposalService.ts. Proposals and operations keep the old
// JSON shape. Operation drafts are maps with type, sourcePath,
// destinationPath, expectedHash, beforeContent, afterContent.
namespace AIEditProposals
{
[[nodiscard]] std::optional<QVariantMap> proposalById(CoreDatabase &database, const QString &proposalId);
QVariantMap requireProposal(CoreDatabase &database, const QString &proposalId);
[[nodiscard]] QVariantList proposalsByRun(CoreDatabase &database, const QString &runId);
// Operations come back without before/after content.
[[nodiscard]] QVariantList proposalsByChat(CoreDatabase &database, const QString &chatId);
[[nodiscard]] std::optional<QVariantMap> latestCompletedByLibrary(CoreDatabase &database, const QString &libraryId);

// proposeAIEdit: validates the drafts and creates a proposal, or appends to
// the run's open proposal.
QVariantMap propose(
    CoreDatabase &database,
    const QString &runId,
    const QString &summary,
    const QVariantList &operations,
    const QString &skillId = {}
);

// `selectedOperationIds` empty optional means all operations.
QVariantMap beginExecution(CoreDatabase &database, const QString &proposalId, const std::optional<QStringList> &selectedOperationIds = std::nullopt);
QVariantMap reject(CoreDatabase &database, const QString &proposalId);
QVariantMap markStale(CoreDatabase &database, const QString &proposalId, const QString &errorMessage);
QVariantMap markFailed(CoreDatabase &database, const QString &proposalId, const QString &errorCode, const QString &errorMessage);
// afterHashes: operation id -> hash string or null QVariant.
QVariantMap completeExecution(CoreDatabase &database, const QString &proposalId, const QHash<QString, QVariant> &afterHashes);
QVariantMap beginUndo(CoreDatabase &database, const QString &proposalId);
QVariantMap restoreAfterUndoFailure(CoreDatabase &database, const QString &proposalId);
QVariantMap markUndoFailed(CoreDatabase &database, const QString &proposalId, const QString &errorCode, const QString &errorMessage);
QVariantMap completeUndo(CoreDatabase &database, const QString &proposalId);

// Unique source/destination paths across the operations, in order.
[[nodiscard]] QStringList affectedPaths(const QVariantList &operations);
}
