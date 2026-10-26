#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <optional>

// Native port of backend/src/api/cognition/edits/services/AIEditTransactionService.ts
// and AIEditSynchronizationService.ts: approve, reject and undo reviewed AI
// edit proposals with rollback, then refresh the Library catalog.
//
// These touch the disk, so they run off the UI thread. Each one throws
// CoreError with the Node message and details when it cannot finish.
namespace AIEditTransactions
{
// Appends one AI Run event (edit.*). The runtime also forwards it to the UI.
using EventSink = std::function<void(const QString &runId, const QString &eventType, const QVariantMap &payload)>;

QVariantMap reject(CoreDatabase &database, const QString &proposalId, const EventSink &sink);

// {proposal, synchronization}
QVariantMap approve(
    CoreDatabase &database,
    const QString &proposalId,
    const std::optional<QStringList> &selectedOperationIds,
    const EventSink &sink
);

// {proposal, synchronization}
QVariantMap undo(CoreDatabase &database, const QString &proposalId, const EventSink &sink);
}
