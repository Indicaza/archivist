#include "context_sources.h"

#include "Core/Domains/AI/context_compilers.h"
#include "Core/Domains/AI/library_retrieval.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/core_error.h"

#include <cmath>

using Context::ContextItem;
using Context::EvidenceRef;
using Context::Kind;
using Context::SearchCandidate;
using Context::Trust;

namespace
{
QString metadataString(const QVariantMap &candidate, const char *key)
{
    const QVariant value = candidate.value(QStringLiteral("metadata")).toMap().value(QLatin1String(key));
    return value.typeId() == QMetaType::QString ? value.toString() : QString();
}

int metadataInt(const QVariantMap &candidate, const char *key)
{
    const QVariant value = candidate.value(QStringLiteral("metadata")).toMap().value(QLatin1String(key));
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok && value.typeId() != QMetaType::QString && std::isfinite(number) ? static_cast<int>(number) : 0;
}

ContextItem messageItem(const QString &chatId, const ContextSources::StoredMessage &message, Kind kind)
{
    ContextItem item;
    item.id = QStringLiteral("%1:%2").arg(Context::kindName(kind), message.id);
    item.kind = kind;
    item.role = message.role;
    item.content = message.content;
    item.trust = message.role == QStringLiteral("assistant") ? Trust::ModelOutput
        : message.role == QStringLiteral("user")             ? Trust::UserIntent
                                                             : Trust::Untrusted;
    item.ref.sourceType = QStringLiteral("chat-message");
    item.ref.sourceId = message.id;
    item.ref.chatId = chatId;
    item.ref.createdAt = message.createdAt;
    item.ref.producer = QStringLiteral("chat-history");
    item.dedupeKey = item.ref.key();
    return item;
}
}

namespace ContextSources
{
ContextItem currentRequest(const QString &chatId, const StoredMessage &message)
{
    ContextItem item = messageItem(chatId, message, Kind::CurrentRequest);
    item.trust = Trust::UserIntent;
    item.required = true;
    return item;
}

QList<ContextItem> history(const QString &chatId, const QList<StoredMessage> &messages, const QString &currentMessageId)
{
    QList<ContextItem> items;
    int order = 0;

    for (qsizetype index = messages.size() - 1; index >= 0; --index) {
        const StoredMessage &message = messages.at(index);

        if (message.id == currentMessageId || message.content.trimmed().isEmpty()) {
            continue;
        }

        ContextItem item = messageItem(chatId, message, Kind::ConversationHistory);
        item.order = order++;
        items.append(item);
    }

    return items;
}

Attachments attachments(CoreDatabase &database, const QString &chatId, int perFileTokens)
{
    Attachments result;

    for (const QVariant &value : ChatRepository::attachments(database, chatId)) {
        const QVariantMap attachment = value.toMap();
        ContextItem item;
        item.kind = Kind::Attachment;
        item.trust = Trust::Untrusted;
        item.ref.sourceType = QStringLiteral("attachment");
        item.ref.sourceId = attachment.value(QStringLiteral("id")).toString();
        item.ref.libraryId = attachment.value(QStringLiteral("libraryId")).toString();
        item.ref.libraryName = attachment.value(QStringLiteral("libraryName")).toString();
        item.ref.fileId = attachment.value(QStringLiteral("fileId")).toString();
        item.ref.relativePath = attachment.value(QStringLiteral("relativePath")).toString();
        item.ref.chatId = chatId;
        item.ref.producer = QStringLiteral("attachment");
        item.id = QStringLiteral("attachment:") + item.ref.sourceId;
        // A whole attached file covers retrieved excerpts of the same file.
        item.dedupeKey = QStringLiteral("library-file:") + item.ref.fileId;
        item.order = static_cast<int>(result.items.size() + result.unavailable.size());

        if (!item.ref.libraryId.isEmpty()) {
            result.libraryIds.insert(item.ref.libraryId);
        }

        const QString fileStatus = attachment.value(QStringLiteral("fileStatus")).toString();

        if (fileStatus != QStringLiteral("available")) {
            const QString reason = fileStatus == QStringLiteral("missing")
                ? QStringLiteral("The attached file is missing from its Library.")
                : QStringLiteral("The attached file is not currently readable.");
            item.truncatedNote = reason;
            result.unavailable.append({item, Context::Omission::unavailable});
            result.warnings.append(QStringLiteral("Attached file %1 was skipped: %2").arg(item.ref.relativePath, reason));
            continue;
        }

        try {
            const QString content = LibraryFiles::readText(database, item.ref.libraryId, item.ref.fileId)
                                        .value(QStringLiteral("content"))
                                        .toString();
            const qsizetype maximumCharacters = static_cast<qsizetype>(std::max(0, perFileTokens)) * 4;

            if (content.size() > maximumCharacters) {
                item.content = content.left(maximumCharacters);
                item.truncatedNote = QStringLiteral("The file was limited to approximately %1 tokens.").arg(perFileTokens);
            } else {
                item.content = content;
            }

            result.items.append(item);
        } catch (const std::exception &error) {
            QString reason = QString::fromUtf8(error.what());

            if (const auto *coreError = dynamic_cast<const CoreError *>(&error)) {
                reason = coreError->message();
            }

            if (reason.trimmed().isEmpty()) {
                reason = QStringLiteral("The file could not be read.");
            }

            result.unavailable.append({item, Context::Omission::unavailable});
            result.warnings.append(QStringLiteral("Attached file %1 was skipped: %2").arg(item.ref.relativePath, reason));
        }
    }

    return result;
}

Retrieval libraryEvidence(CoreDatabase &database, const QString &libraryId, const QString &query, int limit)
{
    Retrieval result;

    if (libraryId.isEmpty()) {
        return result;
    }

    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library || !library->value(QStringLiteral("archivedAt")).isNull()) {
        result.warnings.append(QStringLiteral("Automatic Library retrieval was skipped because this Chat's Library is unavailable."));
        return result;
    }

    const QString libraryName = library->value(QStringLiteral("name")).toString();
    const QVariantMap search = LibraryRetrieval::search(database, query, libraryId, limit);
    static const QSet<QString> benignWarnings{
        QStringLiteral("No indexed Library text matched the search query."),
        QStringLiteral("The query did not contain any searchable terms."),
    };

    for (const QVariant &warning : search.value(QStringLiteral("warnings")).toList()) {
        if (!benignWarnings.contains(warning.toString())) {
            result.warnings.append(warning.toString());
        }
    }

    for (const QVariant &value : search.value(QStringLiteral("candidates")).toList()) {
        const QVariantMap candidate = value.toMap();
        SearchCandidate item;
        item.kind = Kind::LibraryEvidence;
        item.content = candidate.value(QStringLiteral("content")).toString();
        item.score = candidate.value(QStringLiteral("score")).toDouble();
        item.reason = candidate.value(QStringLiteral("reason")).toString();
        item.ref.sourceType = QStringLiteral("library-chunk");
        item.ref.sourceId = candidate.value(QStringLiteral("id")).toString();
        item.ref.libraryId = metadataString(candidate, "libraryId");
        item.ref.libraryName = libraryName;
        item.ref.fileId = metadataString(candidate, "fileId");
        item.ref.relativePath = metadataString(candidate, "relativePath");
        item.ref.startLine = metadataInt(candidate, "startLine");
        item.ref.endLine = metadataInt(candidate, "endLine");
        item.ref.producer = LibraryRetrieval::toolId;

        if (item.ref.libraryId.isEmpty()) {
            item.ref.libraryId = libraryId;
        }

        result.candidates.append(item);
    }

    return result;
}

Retrieval hybridLibraryEvidence(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &query,
    const QVector<float> &queryVector,
    const ::Retrieval::HybridPolicy &policy,
    int limit
)
{
    Retrieval result;

    if (libraryId.isEmpty()) {
        return result;
    }

    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library || !library->value(QStringLiteral("archivedAt")).isNull()) {
        result.warnings.append(QStringLiteral("Automatic Library retrieval was skipped because this Chat's Library is unavailable."));
        return result;
    }

    const QString libraryName = library->value(QStringLiteral("name")).toString();
    const ::Retrieval::HybridResult search = ::Retrieval::searchLibrary(database, libraryId, query, queryVector, policy, limit);
    static const QSet<QString> benignWarnings{
        QStringLiteral("No indexed Library text matched the search query."),
        QStringLiteral("The query did not contain any searchable terms."),
    };

    for (const QString &warning : search.warnings) {
        if (!benignWarnings.contains(warning)) {
            result.warnings.append(warning);
        }
    }

    const bool hybrid = search.diagnostics.value(QStringLiteral("mode")).toString() == QStringLiteral("hybrid");

    for (const ::Retrieval::HybridHit &hit : search.hits) {
        SearchCandidate item;
        item.kind = Kind::LibraryEvidence;
        item.content = hit.content;
        item.score = hit.score;
        item.reason = hit.reason;
        item.ref.sourceType = QStringLiteral("library-chunk");
        item.ref.sourceId = hit.chunkId;
        item.ref.libraryId = hit.libraryId.isEmpty() ? libraryId : hit.libraryId;
        item.ref.libraryName = libraryName;
        item.ref.fileId = hit.fileId;
        item.ref.relativePath = hit.relativePath;
        item.ref.startLine = hit.startLine;
        item.ref.endLine = hit.endLine;
        // Who found it, so the Inspector can tell keyword from semantic hits.
        item.ref.producer = !hybrid                ? LibraryRetrieval::toolId
            : hit.lexicalRank > 0 && hit.semanticRank > 0 ? QStringLiteral("library-hybrid")
            : hit.semanticRank > 0                        ? QStringLiteral("library-semantic")
                                                          : LibraryRetrieval::toolId;
        result.candidates.append(item);
    }

    result.diagnostics = search.diagnostics;
    result.diagnostics.insert(QStringLiteral("policy"), policy.toVariant());
    return result;
}

Retrieval conversationRecall(CoreDatabase &database, const QString &query, const QString &chatId, int limit)
{
    Retrieval result;
    const QStringList terms = ContextCompilers::distinctiveSearchTerms(query, 3);

    if (terms.isEmpty()) {
        return result;
    }

    const QVariantMap search = ContextCompilers::searchChatMessages(database, terms.join(QLatin1Char(' ')), chatId, limit);

    for (const QVariant &value : search.value(QStringLiteral("candidates")).toList()) {
        const QVariantMap candidate = value.toMap();
        SearchCandidate item;
        item.kind = Kind::ConversationRecall;
        item.content = candidate.value(QStringLiteral("content")).toString();
        item.score = candidate.value(QStringLiteral("score")).toDouble();
        item.reason = candidate.value(QStringLiteral("reason")).toString();
        item.originalRole = metadataString(candidate, "role");
        item.ref.sourceType = QStringLiteral("chat-message");
        item.ref.sourceId = metadataString(candidate, "messageId");
        item.ref.chatId = metadataString(candidate, "chatId");
        item.ref.createdAt = metadataString(candidate, "createdAt");
        item.ref.producer = QStringLiteral("chat-message-fts");

        if (!item.ref.sourceId.isEmpty()) {
            result.candidates.append(item);
        }
    }

    return result;
}
}
