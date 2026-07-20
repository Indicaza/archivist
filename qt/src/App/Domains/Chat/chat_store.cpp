#include "chat_store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QUuid>

namespace
{
struct JsonReplyResult
{
    bool ok = false;
    QJsonObject object;
    QString errorMessage;
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

bool ChatStore::loadingChats() const
{
    return m_loadingChats;
}

bool ChatStore::loadingMessages() const
{
    return m_loadingMessages;
}

bool ChatStore::responding() const
{
    return m_responding;
}

QString ChatStore::errorMessage() const
{
    return m_errorMessage;
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
    if (m_responding || chatId.isEmpty() || !containsChat(chatId)) {
        return;
    }

    if (chatId == m_selectedChatId) {
        refreshSelectedMessages();
        return;
    }

    setErrorMessage({});
    setMessages({});
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
        setLoadingMessages(false);
        return;
    }

    setErrorMessage({});
    setLoadingMessages(true);

    const QString path = QStringLiteral("/chats/%1/messages")
        .arg(encodedPathSegment(m_selectedChatId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoadingMessages(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setMessages(mapMessages(result.object.value(QStringLiteral("messages")).toArray()));
    });
}

void ChatStore::sendMessage(const QString &content)
{
    const QString trimmedContent = content.trimmed();

    if (trimmedContent.isEmpty() || m_selectedChatId.isEmpty() || m_responding) {
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
            result.object.value(QStringLiteral("model")).toString()
        );
        promoteSelectedChat();
    });
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

void ChatStore::setSelectedChatId(const QString &chatId)
{
    if (m_selectedChatId == chatId) {
        return;
    }

    m_selectedChatId = chatId;
    setCompletionMetadata({}, {});
    emit selectedChatIdChanged();
    emit selectedChatChanged();
}

void ChatStore::setMessages(const QVariantList &messages)
{
    if (m_messages == messages) {
        return;
    }

    m_messages = messages;
    emit messagesChanged();
}

void ChatStore::setLoadingChats(bool loading)
{
    if (m_loadingChats == loading) {
        return;
    }

    m_loadingChats = loading;
    emit loadingChatsChanged();
}

void ChatStore::setLoadingMessages(bool loading)
{
    if (m_loadingMessages == loading) {
        return;
    }

    m_loadingMessages = loading;
    emit loadingMessagesChanged();
}

void ChatStore::setResponding(bool responding)
{
    if (m_responding == responding) {
        return;
    }

    m_responding = responding;
    emit respondingChanged();
}

void ChatStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void ChatStore::setCompletionMetadata(const QString &provider, const QString &model)
{
    if (m_lastProvider == provider && m_lastModel == model) {
        return;
    }

    m_lastProvider = provider;
    m_lastModel = model;
    emit completionMetadataChanged();
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
