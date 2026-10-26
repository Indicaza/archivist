#pragma once

#include "Core/Database/core_database.h"
#include "context_compilers.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/chats/services/ChatAttachmentEvidence.ts and
// ChatLibraryRetrievalEvidence.ts. Both build one system message of quoted
// Library evidence placed just before the current user message.
namespace ChatEvidence
{
struct AttachmentEvidence
{
    std::optional<ContextCompilers::SourceMessage> contextMessage;
    QVariantList sources;   // ChatAttachmentSource
    QVariantList outcomes;  // ContextSourceOutcome
    QStringList warnings;
};

struct RetrievalEvidence
{
    std::optional<ContextCompilers::SourceMessage> contextMessage;
    QVariantList manifestSources;
    QVariantList outcomes;
    QStringList warnings;
};

// `currentMessage` is the user message: {id, createdAt}. Reads files from disk.
[[nodiscard]] AttachmentEvidence attachments(CoreDatabase &database, const QString &chatId, const QVariantMap &currentMessage);

[[nodiscard]] RetrievalEvidence libraryRetrieval(
    CoreDatabase &database,
    const QString &chatId,
    const QString &libraryId,
    const QVariantMap &currentMessage,
    const QSet<QString> &excludedFileIds
);
}
