#include "context_contracts.h"

namespace Context
{
QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Instructions:
        return QStringLiteral("instructions");
    case Kind::ToolDefinitions:
        return QStringLiteral("tool-definitions");
    case Kind::CurrentRequest:
        return QStringLiteral("current-request");
    case Kind::ConversationHistory:
        return QStringLiteral("conversation-history");
    case Kind::ConversationRecall:
        return QStringLiteral("conversation-recall");
    case Kind::Attachment:
        return QStringLiteral("attachment");
    case Kind::LibraryEvidence:
        return QStringLiteral("library-evidence");
    case Kind::WikiKnowledge:
        return QStringLiteral("wiki-knowledge");
    case Kind::Memory:
        return QStringLiteral("memory");
    case Kind::ToolResult:
        return QStringLiteral("tool-result");
    }

    return QStringLiteral("unknown");
}

QString trustName(Trust trust)
{
    switch (trust) {
    case Trust::Authoritative:
        return QStringLiteral("authoritative");
    case Trust::UserIntent:
        return QStringLiteral("user-intent");
    case Trust::ModelOutput:
        return QStringLiteral("model-output");
    case Trust::Untrusted:
        return QStringLiteral("untrusted");
    }

    return QStringLiteral("untrusted");
}

Trust trustFor(Kind kind)
{
    switch (kind) {
    case Kind::Instructions:
    case Kind::ToolDefinitions:
        return Trust::Authoritative;
    case Kind::CurrentRequest:
        return Trust::UserIntent;
    case Kind::ConversationHistory:
        // Per message: user turns are UserIntent, assistant turns ModelOutput.
        return Trust::UserIntent;
    case Kind::ConversationRecall:
    case Kind::Attachment:
    case Kind::LibraryEvidence:
    case Kind::WikiKnowledge:
    case Kind::Memory:
    case Kind::ToolResult:
        return Trust::Untrusted;
    }

    return Trust::Untrusted;
}

QString EvidenceRef::key() const
{
    return sourceType + QLatin1Char(':') + sourceId;
}

QString EvidenceRef::label() const
{
    if (relativePath.isEmpty()) {
        return {};
    }

    QString text = (libraryName.isEmpty() ? QString() : libraryName + QLatin1Char('/')) + relativePath;

    if (startLine > 0) {
        text += QStringLiteral(":%1").arg(startLine);

        if (endLine > startLine) {
            text += QStringLiteral("-%1").arg(endLine);
        }
    }

    return text;
}

QVariantMap EvidenceRef::toVariant() const
{
    QVariantMap map{
        {QStringLiteral("sourceType"), sourceType},
        {QStringLiteral("sourceId"), sourceId},
        {QStringLiteral("producer"), producer},
    };
    const auto put = [&map](const char *key, const QString &value) {
        if (!value.isEmpty()) {
            map.insert(QLatin1String(key), value);
        }
    };
    put("libraryId", libraryId);
    put("libraryName", libraryName);
    put("fileId", fileId);
    put("relativePath", relativePath);
    put("chatId", chatId);
    put("createdAt", createdAt);

    if (!attributes.isEmpty()) {
        map.insert(QStringLiteral("attributes"), attributes);
    }

    if (startLine > 0) {
        map.insert(QStringLiteral("startLine"), startLine);
        map.insert(QStringLiteral("endLine"), endLine);
    }

    return map;
}

bool AccessScope::allows(const EvidenceRef &ref) const
{
    if (!restricted || ref.libraryId.isEmpty()) {
        return true;
    }

    return libraryIds.contains(ref.libraryId);
}
}
