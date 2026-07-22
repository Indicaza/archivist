#include "chat_store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QSet>
#include <QUuid>

namespace
{
constexpr int initialMessagePageSize = 160;
constexpr int olderMessagePageSize = 120;

struct JsonReplyResult
{
    bool ok = false;
    QJsonObject object;
    QString errorMessage;
};

struct MessagePage
{
    QVariantList messages;
    bool hasMore = false;
    QString nextBeforeMessageId;
};

QString responseErrorMessage(const QJsonObject &object, const QString &fallback)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? fallback : message;
}

JsonReplyResult consumeJsonReply(QNetworkReply *reply)
{
    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject{};

    if (reply->error() != QNetworkReply::NoError) {
        return {
            false,
            object,
            responseErrorMessage(object, reply->errorString()),
        };
    }

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {
            false,
            {},
            QStringLiteral("Archivist API returned an invalid JSON response."),
        };
    }

    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        return {
            false,
            object,
            responseErrorMessage(object, QStringLiteral("Archivist API request failed.")),
        };
    }

    return {true, object, {}};
}

QString encodedPathSegment(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QString displayTimestamp(const QString &isoTimestamp)
{
    QDateTime timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODateWithMs);

    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    }

    if (!timestamp.isValid()) {
        return {};
    }

    return timestamp.toLocalTime().toString(QStringLiteral("h:mm AP"));
}

QVariantMap mapMessage(const QJsonObject &object)
{
    QVariantMap message = object.toVariantMap();
    message.insert(
        QStringLiteral("displayTimestamp"),
        displayTimestamp(object.value(QStringLiteral("createdAt")).toString())
    );
    return message;
}

QVariantList mapMessages(const QJsonArray &array)
{
    QVariantList messages;
    messages.reserve(array.size());

    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            messages.append(mapMessage(value.toObject()));
        }
    }

    return messages;
}

MessagePage mapMessagePage(const QJsonObject &object)
{
    const QJsonObject pagination = object.value(QStringLiteral("pagination")).toObject();

    return {
        mapMessages(object.value(QStringLiteral("messages")).toArray()),
        pagination.value(QStringLiteral("hasMore")).toBool(false),
        pagination.value(QStringLiteral("nextBeforeMessageId")).toString(),
    };
}

QVariantMap optimisticMessage(
    const QString &id,
    const QString &chatId,
    const QString &role,
    const QString &content
)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("chatId"), chatId},
        {QStringLiteral("role"), role},
        {QStringLiteral("content"), content},
        {QStringLiteral("status"), QStringLiteral("streaming")},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("displayTimestamp"), QStringLiteral("Now")},
    };
}

QVariantList withoutChat(const QVariantList &chats, const QString &chatId)
{
    QVariantList result;
    result.reserve(chats.size());

    for (const QVariant &value : chats) {
        if (value.toMap().value(QStringLiteral("id")).toString() != chatId) {
            result.append(value);
        }
    }

    return result;
}


QVariantList withoutAttachment(
    const QVariantList &attachments,
    const QString &attachmentId
)
{
    QVariantList result;
    result.reserve(attachments.size());

    for (const QVariant &value : attachments) {
        if (value.toMap().value(QStringLiteral("id")).toString() != attachmentId) {
            result.append(value);
        }
    }

    return result;
}
}

ChatStore::ChatStore(QObject *parent)
    : QObject(parent)
    , m_baseUrl(QStringLiteral("http://127.0.0.1:3333/api"))
{
}

QVariantList ChatStore::chats() const
{
    return m_chats;
}

QVariantList ChatStore::archivedChats() const
{
    return m_archivedChats;
}

QString ChatStore::selectedChatId() const
{
    return m_selectedChatId;
}

QVariantMap ChatStore::selectedChat() const
{
    for (const QVariant &value : m_chats) {
        const QVariantMap chat = value.toMap();
        if (chat.value(QStringLiteral("id")).toString() == m_selectedChatId) {
            return chat;
        }
    }

    return {};
}

QVariantList ChatStore::messages() const
{
    return m_messages;
}


QVariantList ChatStore::attachments() const
{
    return m_attachments;
}

QVariantList ChatStore::lastSources() const
{
    return m_lastSources;
}

QVariantMap ChatStore::inspectedContext() const
{
    return m_inspectedContext;
}

QString ChatStore::inspectedMessageId() const
{
    return m_inspectedMessageId;
}

bool ChatStore::loadingChats() const
{
    return m_loadingChats;
}

bool ChatStore::loadingArchivedChats() const
{
    return m_loadingArchivedChats;
}

bool ChatStore::loadingMessages() const
{
    return m_loadingMessages;
}


bool ChatStore::loadingAttachments() const
{
    return m_loadingAttachments;
}

bool ChatStore::loadingOlderMessages() const
{
    return m_loadingOlderMessages;
}

bool ChatStore::hasOlderMessages() const
{
    return m_hasOlderMessages;
}

bool ChatStore::responding() const
{
    return m_responding;
}

bool ChatStore::assigningAgent() const
{
    return m_assigningAgent;
}

bool ChatStore::mutating() const
{
    return m_mutating;
}


bool ChatStore::mutatingAttachment() const
{
    return m_mutatingAttachment;
}

bool ChatStore::loadingContext() const
{
    return m_loadingContext;
}

QString ChatStore::errorMessage() const
{
    return m_errorMessage;
}

QString ChatStore::contextErrorMessage() const
{
    return m_contextErrorMessage;
}

QString ChatStore::lastProvider() const
{
    return m_lastProvider;
}

QString ChatStore::lastModel() const
{
    return m_lastModel;
}

QNetworkRequest ChatStore::requestFor(const QString &path) const
{
    QNetworkRequest request{QUrl(m_baseUrl.toString() + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

void ChatStore::refresh()
{
    if (m_loadingChats) {
        return;
    }

    setErrorMessage({});
    setLoadingChats(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/chats")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingChats(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        setChats(result.object.value(QStringLiteral("chats")).toArray().toVariantList());
        fetchAppState();
    });
}

void ChatStore::refreshArchived()
{
    if (m_loadingArchivedChats) {
        return;
    }

    clearError();
    setLoadingArchivedChats(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/chats/archived")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoadingArchivedChats(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setArchivedChats(result.object.value(QStringLiteral("chats")).toArray().toVariantList());
    });
}

void ChatStore::createChat(const QString &libraryId)
{
    if (
        libraryId.isEmpty()
        || m_mutating
        || m_mutatingAttachment
        || m_responding
        || m_assigningAgent
    ) {
        return;
    }

    clearError();
    setMutating(true);

    QJsonObject body;
    body.insert(QStringLiteral("libraryId"), libraryId);

    QNetworkReply *reply = m_network.post(
        requestFor(QStringLiteral("/chats")),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap chat = result.object
            .value(QStringLiteral("chat"))
            .toObject()
            .toVariantMap();
        const QString chatId = chat.value(QStringLiteral("id")).toString();

        if (chatId.isEmpty()) {
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Chat."));
            return;
        }

        upsertActiveChat(chat);
        setMessages({});
        resetMessagePageState();
        setSelectedChatId(chatId);
        emit chatCreated(chat);
    });
}

void ChatStore::fetchAppState()
{
    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/app-state")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingChats(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        const QJsonObject appState = result.object.value(QStringLiteral("appState")).toObject();
        QString nextChatId = appState.value(QStringLiteral("selectedChatId")).toString();

        if (!containsChat(nextChatId)) {
            nextChatId.clear();
        }

        setLoadingChats(false);

        if (nextChatId.isEmpty() && !m_chats.isEmpty()) {
            selectChat(m_chats.first().toMap().value(QStringLiteral("id")).toString());
            return;
        }

        setSelectedChatId(nextChatId);
        refreshSelectedMessages();
    });
}

void ChatStore::selectChat(const QString &chatId)
{
    if (m_responding || m_assigningAgent || m_mutating || m_mutatingAttachment || chatId.isEmpty() || !containsChat(chatId)) {
        return;
    }

    if (chatId == m_selectedChatId) {
        refreshSelectedMessages();
        refreshSelectedAttachments();
        return;
    }

    setErrorMessage({});
    setMessages({});
    setAttachments({});
    resetMessagePageState();
    setLoadingMessages(true);

    QJsonObject body;
    body.insert(QStringLiteral("chatId"), chatId);

    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(QStringLiteral("/chats/selected")),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingMessages(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        setSelectedChatId(result.object.value(QStringLiteral("selectedChatId")).toString());
        setLoadingMessages(false);
        refreshSelectedMessages();
    });
}

void ChatStore::refreshSelectedMessages()
{
    if (m_selectedChatId.isEmpty()) {
        setMessages({});
        resetMessagePageState();
        setLoadingMessages(false);
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setErrorMessage({});
    resetMessagePageState();
    setLoadingMessages(true);

    const QString path = QStringLiteral("/chats/%1/messages?limit=%2")
        .arg(encodedPathSegment(requestedChatId))
        .arg(initialMessagePageSize);
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (requestedChatId != m_selectedChatId) {
            return;
        }

        setLoadingMessages(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const MessagePage page = mapMessagePage(result.object);
        setMessages(page.messages);
        setMessagePageState(page.hasMore, page.nextBeforeMessageId);
    });
}

void ChatStore::refreshSelectedAttachments()
{
    if (m_selectedChatId.isEmpty()) {
        setAttachments({});
        setLoadingAttachments(false);
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setLoadingAttachments(true);

    const QString path = QStringLiteral("/chats/%1/attachments")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (requestedChatId != m_selectedChatId) {
            return;
        }

        setLoadingAttachments(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setAttachments(
            result.object.value(QStringLiteral("attachments")).toArray().toVariantList()
        );
    });
}

void ChatStore::loadOlderMessages()
{
    if (
        m_selectedChatId.isEmpty()
        || m_loadingMessages
        || m_loadingOlderMessages
        || !m_hasOlderMessages
        || m_beforeMessageId.isEmpty()
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;
    const QString requestedBeforeMessageId = m_beforeMessageId;

    setErrorMessage({});
    setLoadingOlderMessages(true);

    const QString path = QStringLiteral("/chats/%1/messages?limit=%2&before=%3")
        .arg(encodedPathSegment(requestedChatId))
        .arg(olderMessagePageSize)
        .arg(encodedPathSegment(requestedBeforeMessageId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedChatId, requestedBeforeMessageId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (
                requestedChatId != m_selectedChatId
                || requestedBeforeMessageId != m_beforeMessageId
            ) {
                return;
            }

            if (!result.ok) {
                setLoadingOlderMessages(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            const MessagePage page = mapMessagePage(result.object);
            QSet<QString> existingIds;
            existingIds.reserve(m_messages.size());

            for (const QVariant &value : m_messages) {
                existingIds.insert(value.toMap().value(QStringLiteral("id")).toString());
            }

            QVariantList prependedMessages;
            prependedMessages.reserve(page.messages.size());

            for (const QVariant &value : page.messages) {
                const QString id = value.toMap().value(QStringLiteral("id")).toString();
                if (!id.isEmpty() && !existingIds.contains(id)) {
                    prependedMessages.append(value);
                }
            }

            setMessagePageState(page.hasMore, page.nextBeforeMessageId);

            if (!prependedMessages.isEmpty()) {
                QVariantList nextMessages = prependedMessages;
                nextMessages.reserve(prependedMessages.size() + m_messages.size());

                for (const QVariant &value : m_messages) {
                    nextMessages.append(value);
                }

                emit olderMessagesWillPrepend(prependedMessages.size());
                m_messages = nextMessages;
                emit messagesChanged();
                emit olderMessagesPrepended(prependedMessages.size());
            }

            setLoadingOlderMessages(false);
        }
    );
}

void ChatStore::sendMessage(const QString &content)
{
    const QString trimmedContent = content.trimmed();

    if (
        trimmedContent.isEmpty()
        || m_selectedChatId.isEmpty()
        || m_responding
        || m_assigningAgent
        || m_mutating
        || m_mutatingAttachment
    ) {
        return;
    }

    setErrorMessage({});
    setResponding(true);

    QVariantList nextMessages = m_messages;
    nextMessages.append(optimisticMessage(
        QStringLiteral("optimistic-user-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        m_selectedChatId,
        QStringLiteral("user"),
        trimmedContent
    ));
    nextMessages.append(optimisticMessage(
        QStringLiteral("optimistic-assistant-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        m_selectedChatId,
        QStringLiteral("assistant"),
        QStringLiteral("Thinking…")
    ));
    setMessages(nextMessages);

    QJsonObject body;
    body.insert(QStringLiteral("content"), trimmedContent);

    const QString path = QStringLiteral("/chats/%1/respond")
        .arg(encodedPathSegment(m_selectedChatId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setResponding(false);

        if (!result.ok) {
            QVariantList failedMessages;
            failedMessages.reserve(m_messages.size());

            for (const QVariant &value : m_messages) {
                QVariantMap message = value.toMap();
                const QString id = message.value(QStringLiteral("id")).toString();

                if (id.startsWith(QStringLiteral("optimistic-assistant-"))) {
                    continue;
                }

                if (id.startsWith(QStringLiteral("optimistic-user-"))) {
                    message.insert(QStringLiteral("status"), QStringLiteral("failed"));
                }

                failedMessages.append(message);
            }

            setMessages(failedMessages);
            setErrorMessage(result.errorMessage);
            return;
        }

        QVariantList storedMessages;
        storedMessages.reserve(m_messages.size());

        for (const QVariant &value : m_messages) {
            const QString id = value.toMap().value(QStringLiteral("id")).toString();
            if (!id.startsWith(QStringLiteral("optimistic-"))) {
                storedMessages.append(value);
            }
        }

        storedMessages.append(mapMessage(
            result.object.value(QStringLiteral("userMessage")).toObject()
        ));
        storedMessages.append(mapMessage(
            result.object.value(QStringLiteral("assistantMessage")).toObject()
        ));
        setMessages(storedMessages);
        setCompletionMetadata(
            result.object.value(QStringLiteral("provider")).toString(),
            result.object.value(QStringLiteral("model")).toString(),
            result.object.value(QStringLiteral("attachmentSources")).toArray().toVariantList()
        );
        promoteSelectedChat();
    });
}

void ChatStore::assignAgentToSelectedChat(const QString &agentId)
{
    if (
        m_selectedChatId.isEmpty()
        || agentId.isEmpty()
        || m_responding
        || m_assigningAgent
        || m_mutating
        || m_mutatingAttachment
        || selectedChat().value(QStringLiteral("agentId")).toString() == agentId
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setErrorMessage({});
    setAssigningAgent(true);

    QJsonObject body;
    body.insert(QStringLiteral("agentId"), agentId);

    const QString path = QStringLiteral("/chats/%1")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setAssigningAgent(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap updatedChat = result.object
            .value(QStringLiteral("chat"))
            .toObject()
            .toVariantMap();

        if (updatedChat.value(QStringLiteral("id")).toString() != requestedChatId) {
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Chat assignment."));
            return;
        }

        replaceChat(updatedChat);
    });
}

void ChatStore::attachFile(const QString &libraryId, const QString &fileId)
{
    if (
        m_selectedChatId.isEmpty()
        || libraryId.isEmpty()
        || fileId.isEmpty()
        || m_responding
        || m_mutating
        || m_mutatingAttachment
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setErrorMessage({});
    setMutatingAttachment(true);

    QJsonObject body;
    body.insert(QStringLiteral("libraryId"), libraryId);
    body.insert(QStringLiteral("fileId"), fileId);

    const QString path = QStringLiteral("/chats/%1/attachments")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutatingAttachment(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (requestedChatId != m_selectedChatId) {
            return;
        }

        const QVariantMap attachment = result.object
            .value(QStringLiteral("attachment"))
            .toObject()
            .toVariantMap();

        if (
            attachment.value(QStringLiteral("chatId")).toString()
            != requestedChatId
        ) {
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Chat attachment."));
            return;
        }

        upsertAttachment(attachment);
        emit attachmentAdded(attachment);
    });
}

void ChatStore::removeAttachment(const QString &attachmentId)
{
    if (
        m_selectedChatId.isEmpty()
        || attachmentId.isEmpty()
        || m_responding
        || m_mutating
        || m_mutatingAttachment
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setErrorMessage({});
    setMutatingAttachment(true);

    const QString path = QStringLiteral("/chats/%1/attachments/%2")
        .arg(
            encodedPathSegment(requestedChatId),
            encodedPathSegment(attachmentId)
        );
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("DELETE")
    );

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedChatId, attachmentId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();
            setMutatingAttachment(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (requestedChatId != m_selectedChatId) {
                return;
            }

            removeAttachmentFromList(attachmentId);
            emit attachmentRemoved(attachmentId);
        }
    );
}

void ChatStore::loadMessageContext(const QString &messageId)
{
    if (m_selectedChatId.isEmpty() || messageId.isEmpty()) {
        clearInspectedContext();
        return;
    }

    setInspectedContext(messageId, {});
    setContextErrorMessage({});
    setLoadingContext(true);

    const QString chatId = m_selectedChatId;
    const QString path = QStringLiteral("/chats/%1/messages/%2/context")
        .arg(encodedPathSegment(chatId), encodedPathSegment(messageId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, messageId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (m_selectedChatId != chatId || m_inspectedMessageId != messageId) {
            return;
        }

        setLoadingContext(false);

        if (!result.ok) {
            setContextErrorMessage(result.errorMessage);
            return;
        }

        setInspectedContext(
            messageId,
            result.object.value(QStringLiteral("contextRun")).toObject().toVariantMap()
        );
    });
}

void ChatStore::clearInspectedContext()
{
    setLoadingContext(false);
    setContextErrorMessage({});
    setInspectedContext({}, {});
}

void ChatStore::updateChat(const QString &chatId, const QVariantMap &input)
{
    if (
        m_mutating
        || m_mutatingAttachment
        || m_responding
        || m_assigningAgent
        || chatId.isEmpty()
        || input.isEmpty()
    ) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/chats/%1").arg(encodedPathSegment(chatId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(QJsonObject::fromVariantMap(input)).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap chat = result.object
            .value(QStringLiteral("chat"))
            .toObject()
            .toVariantMap();

        if (chat.value(QStringLiteral("id")).toString() != chatId) {
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Chat."));
            return;
        }

        upsertActiveChat(chat);
        emit chatUpdated(chat);
    });
}

void ChatStore::archiveChat(const QString &chatId)
{
    if (m_mutating || m_mutatingAttachment || m_responding || m_assigningAgent || chatId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/chats/%1/archive").arg(encodedPathSegment(chatId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap chat = result.object
            .value(QStringLiteral("chat"))
            .toObject()
            .toVariantMap();
        const QString nextSelectedChatId = result.object
            .value(QStringLiteral("selectedChatId"))
            .toString();
        const bool selectionChanged = m_selectedChatId == chatId;

        removeActiveChat(chatId);
        upsertArchivedChat(chat);

        if (selectionChanged) {
            setMessages({});
            setSelectedChatId(nextSelectedChatId);
            refreshSelectedMessages();
        }

        emit chatArchived(chat);
    });
}

void ChatStore::restoreChat(const QString &chatId)
{
    if (m_mutating || m_mutatingAttachment || m_responding || m_assigningAgent || chatId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/chats/%1/restore").arg(encodedPathSegment(chatId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap chat = result.object
            .value(QStringLiteral("chat"))
            .toObject()
            .toVariantMap();

        removeArchivedChat(chatId);
        upsertActiveChat(chat);
        emit chatRestored(chat);
    });
}

void ChatStore::deleteChat(const QString &chatId)
{
    if (m_mutating || m_mutatingAttachment || m_responding || m_assigningAgent || chatId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/chats/%1").arg(encodedPathSegment(chatId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("DELETE")
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QString nextSelectedChatId = result.object
            .value(QStringLiteral("selectedChatId"))
            .toString();
        const bool selectionChanged = m_selectedChatId == chatId;

        removeActiveChat(chatId);
        removeArchivedChat(chatId);

        if (selectionChanged) {
            setMessages({});
            setSelectedChatId(nextSelectedChatId);
            refreshSelectedMessages();
        }

        emit chatDeleted(chatId);
    });
}

void ChatStore::clearError()
{
    setErrorMessage({});
}

void ChatStore::setChats(const QVariantList &chats)
{
    if (m_chats == chats) {
        return;
    }

    m_chats = chats;
    emit chatsChanged();
    emit selectedChatChanged();
}

void ChatStore::setArchivedChats(const QVariantList &chats)
{
    if (m_archivedChats == chats) {
        return;
    }

    m_archivedChats = chats;
    emit archivedChatsChanged();
}

void ChatStore::setSelectedChatId(const QString &chatId)
{
    if (m_selectedChatId == chatId) {
        return;
    }

    m_selectedChatId = chatId;
    resetMessagePageState();
    clearInspectedContext();
    setAttachments({});
    setCompletionMetadata({}, {}, {});
    emit selectedChatIdChanged();
    emit selectedChatChanged();
    refreshSelectedAttachments();
}

void ChatStore::setMessages(const QVariantList &messages)
{
    if (m_messages == messages) {
        return;
    }

    m_messages = messages;
    emit messagesChanged();
}

void ChatStore::setAttachments(const QVariantList &attachments)
{
    if (m_attachments == attachments) {
        return;
    }

    m_attachments = attachments;
    emit attachmentsChanged();
}

void ChatStore::setLoadingChats(bool loading)
{
    if (m_loadingChats == loading) {
        return;
    }

    m_loadingChats = loading;
    emit loadingChatsChanged();
}

void ChatStore::setLoadingArchivedChats(bool loading)
{
    if (m_loadingArchivedChats == loading) {
        return;
    }

    m_loadingArchivedChats = loading;
    emit loadingArchivedChatsChanged();
}

void ChatStore::setLoadingMessages(bool loading)
{
    if (m_loadingMessages == loading) {
        return;
    }

    m_loadingMessages = loading;
    emit loadingMessagesChanged();
}

void ChatStore::setLoadingAttachments(bool loading)
{
    if (m_loadingAttachments == loading) {
        return;
    }

    m_loadingAttachments = loading;
    emit loadingAttachmentsChanged();
}

void ChatStore::setLoadingOlderMessages(bool loading)
{
    if (m_loadingOlderMessages == loading) {
        return;
    }

    m_loadingOlderMessages = loading;
    emit loadingOlderMessagesChanged();
}

void ChatStore::setHasOlderMessages(bool hasOlderMessages)
{
    if (m_hasOlderMessages == hasOlderMessages) {
        return;
    }

    m_hasOlderMessages = hasOlderMessages;
    emit hasOlderMessagesChanged();
}

void ChatStore::setResponding(bool responding)
{
    if (m_responding == responding) {
        return;
    }

    m_responding = responding;
    emit respondingChanged();
}

void ChatStore::setAssigningAgent(bool assigning)
{
    if (m_assigningAgent == assigning) {
        return;
    }

    m_assigningAgent = assigning;
    emit assigningAgentChanged();
}

void ChatStore::setMutating(bool mutating)
{
    if (m_mutating == mutating) {
        return;
    }

    m_mutating = mutating;
    emit mutatingChanged();
}

void ChatStore::setMutatingAttachment(bool mutating)
{
    if (m_mutatingAttachment == mutating) {
        return;
    }

    m_mutatingAttachment = mutating;
    emit mutatingAttachmentChanged();
}

void ChatStore::setLoadingContext(bool loading)
{
    if (m_loadingContext == loading) {
        return;
    }

    m_loadingContext = loading;
    emit loadingContextChanged();
}

void ChatStore::setContextErrorMessage(const QString &message)
{
    if (m_contextErrorMessage == message) {
        return;
    }

    m_contextErrorMessage = message;
    emit contextErrorMessageChanged();
}

void ChatStore::setInspectedContext(
    const QString &messageId,
    const QVariantMap &context
)
{
    if (m_inspectedMessageId == messageId && m_inspectedContext == context) {
        return;
    }

    m_inspectedMessageId = messageId;
    m_inspectedContext = context;
    emit inspectedContextChanged();
}

void ChatStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void ChatStore::setCompletionMetadata(
    const QString &provider,
    const QString &model,
    const QVariantList &sources
)
{
    if (
        m_lastProvider == provider
        && m_lastModel == model
        && m_lastSources == sources
    ) {
        return;
    }

    m_lastProvider = provider;
    m_lastModel = model;
    m_lastSources = sources;
    emit completionMetadataChanged();
}

void ChatStore::setMessagePageState(bool hasMore, const QString &beforeMessageId)
{
    const bool usablePage = hasMore && !beforeMessageId.isEmpty();
    m_beforeMessageId = usablePage ? beforeMessageId : QString{};
    setHasOlderMessages(usablePage);
}

void ChatStore::resetMessagePageState()
{
    m_beforeMessageId.clear();
    setHasOlderMessages(false);
    setLoadingOlderMessages(false);
}

void ChatStore::promoteSelectedChat()
{
    if (m_selectedChatId.isEmpty()) {
        return;
    }

    for (qsizetype index = 0; index < m_chats.size(); ++index) {
        if (m_chats.at(index).toMap().value(QStringLiteral("id")).toString() != m_selectedChatId) {
            continue;
        }

        if (index == 0) {
            return;
        }

        QVariantList nextChats = m_chats;
        const QVariant selected = nextChats.takeAt(index);
        nextChats.prepend(selected);
        setChats(nextChats);
        return;
    }
}

void ChatStore::upsertActiveChat(const QVariantMap &chat)
{
    const QString chatId = chat.value(QStringLiteral("id")).toString();

    if (chatId.isEmpty()) {
        return;
    }

    QVariantList nextChats = withoutChat(m_chats, chatId);
    nextChats.prepend(chat);
    setChats(nextChats);
}

void ChatStore::upsertArchivedChat(const QVariantMap &chat)
{
    const QString chatId = chat.value(QStringLiteral("id")).toString();

    if (chatId.isEmpty()) {
        return;
    }

    QVariantList nextChats = withoutChat(m_archivedChats, chatId);
    nextChats.prepend(chat);
    setArchivedChats(nextChats);
}

void ChatStore::removeActiveChat(const QString &chatId)
{
    setChats(withoutChat(m_chats, chatId));
}

void ChatStore::removeArchivedChat(const QString &chatId)
{
    setArchivedChats(withoutChat(m_archivedChats, chatId));
}

void ChatStore::replaceChat(const QVariantMap &chat)
{
    const QString chatId = chat.value(QStringLiteral("id")).toString();

    if (chatId.isEmpty()) {
        return;
    }

    QVariantList nextChats = m_chats;

    for (qsizetype index = 0; index < nextChats.size(); ++index) {
        if (nextChats.at(index).toMap().value(QStringLiteral("id")).toString() != chatId) {
            continue;
        }

        nextChats[index] = chat;
        setChats(nextChats);
        return;
    }
}

void ChatStore::upsertAttachment(const QVariantMap &attachment)
{
    const QString attachmentId = attachment.value(QStringLiteral("id")).toString();

    if (attachmentId.isEmpty()) {
        return;
    }

    QVariantList nextAttachments;
    nextAttachments.reserve(m_attachments.size() + 1);

    bool replaced = false;

    for (const QVariant &value : m_attachments) {
        if (value.toMap().value(QStringLiteral("id")).toString() == attachmentId) {
            nextAttachments.append(attachment);
            replaced = true;
        } else {
            nextAttachments.append(value);
        }
    }

    if (!replaced) {
        nextAttachments.append(attachment);
    }

    setAttachments(nextAttachments);
}

void ChatStore::removeAttachmentFromList(const QString &attachmentId)
{
    setAttachments(withoutAttachment(m_attachments, attachmentId));
}

bool ChatStore::containsChat(const QString &chatId) const
{
    if (chatId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_chats) {
        if (value.toMap().value(QStringLiteral("id")).toString() == chatId) {
            return true;
        }
    }

    return false;
}
