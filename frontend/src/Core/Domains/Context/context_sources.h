#pragma once

#include "Core/Database/core_database.h"
#include "context_contracts.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The database side of Context Assembly v3: turns stored Chat messages,
// attachments and retrieval results into ContextItems and SearchCandidates.
// It reads; it never decides what fits. Future retrievers (semantic search,
// Wiki pages, memories) add a function here that returns SearchCandidates.
namespace ContextSources
{
// One stored message: {id, role, content, createdAt}.
struct StoredMessage
{
    QString id;
    QString role;
    QString content;
    QString createdAt;
};

[[nodiscard]] Context::ContextItem currentRequest(const QString &chatId, const StoredMessage &message);

// Messages before the current one, newest first (order 0 = newest). Blank
// messages and the current message are left out.
[[nodiscard]] QList<Context::ContextItem> history(const QString &chatId, const QList<StoredMessage> &messages, const QString &currentMessageId);

struct Attachments
{
    QList<Context::ContextItem> items;
    QList<QPair<Context::ContextItem, QString>> unavailable;
    QStringList warnings;
    QSet<QString> libraryIds;
};

// Reads each attached file. A file longer than `perFileTokens` (estimated) is
// cut to that size and marked truncated; budgeting happens in the assembler.
[[nodiscard]] Attachments attachments(CoreDatabase &database, const QString &chatId, int perFileTokens);

struct Retrieval
{
    QList<Context::SearchCandidate> candidates;
    QStringList warnings;
};

// Library full-text search as SearchCandidates.
[[nodiscard]] Retrieval libraryEvidence(CoreDatabase &database, const QString &libraryId, const QString &query, int limit);

// Older Chat messages that share distinctive terms with the query. `chatId`
// empty searches every Chat.
[[nodiscard]] Retrieval conversationRecall(CoreDatabase &database, const QString &query, const QString &chatId, int limit);
}
