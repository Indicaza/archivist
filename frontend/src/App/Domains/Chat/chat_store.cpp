#include "chat_store.h"

#include <algorithm>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QSet>
#include <QTimer>
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
    m_runDeltaFlushTimer.setInterval(120);
    m_runDeltaFlushTimer.setSingleShot(true);

    connect(
        &m_runDeltaFlushTimer,
        &QTimer::timeout,
        this,
        &ChatStore::flushPendingRunDelta
    );
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

QString ChatStore::activeRunId() const
{
    return m_activeRunId;
}

QString ChatStore::activeRunAssistantMessageId() const
{
    return m_activeRunAssistantMessageId;
}

QString ChatStore::activeRunContent() const
{
    return m_activeRunContent;
}

QString ChatStore::runPhase() const
{
    return m_runPhase;
}

QString ChatStore::runPhaseLabel() const
{
    if (m_cancellingRun) {
        return QStringLiteral("Stopping Run…");
    }

    if (m_runPhase == QStringLiteral("run.started")) {
        return QStringLiteral("Starting Run…");
    }

    if (m_runPhase == QStringLiteral("retrieval.started")) {
        return QStringLiteral("Searching Library…");
    }

    if (m_runPhase == QStringLiteral("retrieval.completed")) {
        return QStringLiteral("Reviewing evidence…");
    }

    if (m_runPhase == QStringLiteral("context.started")) {
        return QStringLiteral("Compiling context…");
    }

    if (m_runPhase == QStringLiteral("context.completed")) {
        return QStringLiteral("Context ready…");
    }

    if (
        m_runPhase == QStringLiteral("tool.requested")
        || m_runPhase == QStringLiteral("tool.started")
    ) {
        const QString toolName = m_runActivity
            .value(QStringLiteral("activeToolName"))
            .toString();

        return toolName.isEmpty()
            ? QStringLiteral("Using Library tools…")
            : QStringLiteral("Using %1…").arg(toolName);
    }

    if (m_runPhase == QStringLiteral("tool.completed")) {
        return QStringLiteral("Reviewing tool result…");
    }

    if (
        m_runPhase == QStringLiteral("tool.failed")
        || m_runPhase == QStringLiteral("tool.cancelled")
    ) {
        return QStringLiteral("Continuing after tool issue…");
    }

    if (m_runPhase == QStringLiteral("model.started")) {
        return QStringLiteral("Planning response…");
    }

    if (m_runPhase == QStringLiteral("model.delta")) {
        return QStringLiteral("Writing response…");
    }

    if (m_runPhase == QStringLiteral("model.completed")) {
        return QStringLiteral("Finalizing response…");
    }

    return m_responding
        ? QStringLiteral("Archivist is working…")
        : QString{};
}

QVariantMap ChatStore::runActivity() const
{
    return m_runActivity;
}

bool ChatStore::cancellingRun() const
{
    return m_cancellingRun;
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

void ChatStore::resumeActiveRunForSelectedChat()
{
    if (m_selectedChatId.isEmpty() || m_responding) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;
    const QString path = QStringLiteral("/chats/%1/runs")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (
            !result.ok
            || requestedChatId != m_selectedChatId
            || m_responding
        ) {
            return;
        }

        const QJsonArray runs = result.object.value(QStringLiteral("runs")).toArray();

        for (const QJsonValue &value : runs) {
            const QJsonObject run = value.toObject();

            if (run.value(QStringLiteral("status")).toString() != QStringLiteral("running")) {
                continue;
            }

            setActiveRun(run);
            setCompletionMetadata(
                run.value(QStringLiteral("provider")).toString(),
                run.value(QStringLiteral("model")).toString()
            );
            subscribeToRunEvents(m_activeRunId);
            return;
        }
    });
}

void ChatStore::subscribeToRunEvents(const QString &runId, int afterSequence)
{
    if (runId.isEmpty() || runId != m_activeRunId) {
        return;
    }

    if (m_runEventReply) {
        m_runEventReply->disconnect(this);
        m_runEventReply->abort();
        m_runEventReply->deleteLater();
        m_runEventReply = nullptr;
    }

    m_runEventBuffer.clear();

    const QString path = QStringLiteral("/runs/%1/events?after=%2")
        .arg(encodedPathSegment(runId))
        .arg(std::max(0, afterSequence));
    QNetworkRequest request = requestFor(path);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("text/event-stream"));

    QNetworkReply *reply = m_network.get(request);
    m_runEventReply = reply;
    ++m_runDiagnosticSubscriptions;

    qInfo().noquote()
        << "[AIRunClient] subscribe"
        << "runId=" + runId
        << "afterSequence=" + QString::number(std::max(0, afterSequence))
        << "subscription=" + QString::number(m_runDiagnosticSubscriptions);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, runId]() {
        if (reply != m_runEventReply || runId != m_activeRunId) {
            reply->readAll();
            return;
        }

        processRunEventStream();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, runId]() {
        const bool currentReply = reply == m_runEventReply;

        if (currentReply) {
            processRunEventStream();
            m_runEventReply = nullptr;
        }

        reply->deleteLater();

        if (
            !currentReply
            || runId != m_activeRunId
            || m_runSnapshotPending
        ) {
            return;
        }

        requestRunSnapshot(runId, true);
    });
}

void ChatStore::processRunEventStream()
{
    if (!m_runEventReply) {
        return;
    }

    m_runEventBuffer.append(m_runEventReply->readAll());
    m_runEventBuffer.replace(QByteArrayLiteral("\r\n"), QByteArrayLiteral("\n"));

    int boundary = m_runEventBuffer.indexOf(QByteArrayLiteral("\n\n"));

    while (boundary >= 0) {
        const QByteArray eventBlock = m_runEventBuffer.left(boundary);
        m_runEventBuffer.remove(0, boundary + 2);

        if (!eventBlock.trimmed().isEmpty()) {
            processRunEventBlock(eventBlock);
        }

        boundary = m_runEventBuffer.indexOf(QByteArrayLiteral("\n\n"));
    }
}

void ChatStore::processRunEventBlock(const QByteArray &eventBlock)
{
    QByteArray data;
    QString declaredEventType;

    for (const QByteArray &line : eventBlock.split('\n')) {
        if (line.startsWith(QByteArrayLiteral("event:"))) {
            declaredEventType = QString::fromUtf8(line.mid(6).trimmed());
            continue;
        }

        if (!line.startsWith(QByteArrayLiteral("data:"))) {
            continue;
        }

        if (!data.isEmpty()) {
            data.append('\n');
        }

        data.append(line.mid(5).trimmed());
    }

    if (data.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    QJsonObject event = document.object();

    if (event.value(QStringLiteral("eventType")).toString().isEmpty()) {
        event.insert(QStringLiteral("eventType"), declaredEventType);
    }

    handleRunEvent(event);
}

void ChatStore::handleRunEvent(const QJsonObject &event)
{
    const QString runId = event.value(QStringLiteral("runId")).toString();
    const int sequence = event.value(QStringLiteral("sequence")).toInt();
    const QString eventType = event.value(QStringLiteral("eventType")).toString();

    if (
        runId.isEmpty()
        || runId != m_activeRunId
        || eventType.isEmpty()
        || sequence <= m_lastRunEventSequence
    ) {
        return;
    }

    m_lastRunEventSequence = sequence;
    m_runReconnectAttempts = 0;
    ++m_runDiagnosticEventCount;

    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();

    setRunPhase(eventType);

    if (
        eventType != QStringLiteral("model.delta")
        || !m_runActivity
            .value(QStringLiteral("modelOutputStarted"))
            .toBool()
    ) {
        updateRunActivity(eventType, payload);
    }

    if (eventType == QStringLiteral("model.delta")) {
        const QString delta = payload.value(QStringLiteral("delta")).toString();
        ++m_runDiagnosticDeltaCount;
        m_runDiagnosticDeltaCharacters += delta.size();
        m_pendingRunDelta.append(delta);

        if (!m_runDeltaFlushTimer.isActive()) {
            m_runDeltaFlushTimer.start();
        }

        return;
    }

    qInfo().noquote()
        << "[AIRunClient] event"
        << "runId=" + runId
        << "sequence=" + QString::number(sequence)
        << "event=" + eventType
        << "elapsedMs=" + QString::number(
            m_activeRunClock.isValid() ? m_activeRunClock.elapsed() : -1
        );

    if (
        eventType == QStringLiteral("run.completed")
        || eventType == QStringLiteral("run.cancelled")
        || eventType == QStringLiteral("run.failed")
    ) {
        flushPendingRunDelta();
    }

    if (eventType == QStringLiteral("run.completed")) {
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("complete")
        );
        requestRunSnapshot(runId, false);
        return;
    }

    if (eventType == QStringLiteral("run.cancelled")) {
        setCancellingRun(false);
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("cancelled")
        );
        requestRunSnapshot(runId, false);
        return;
    }

    if (eventType == QStringLiteral("run.failed")) {
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("failed")
        );

        const QString message = payload.value(QStringLiteral("message")).toString();

        if (!message.isEmpty()) {
            setErrorMessage(message);
        }

        requestRunSnapshot(runId, false);
    }
}

void ChatStore::requestRunSnapshot(
    const QString &runId,
    bool reconnectIfRunning
)
{
    if (
        runId.isEmpty()
        || runId != m_activeRunId
        || m_runSnapshotPending
    ) {
        return;
    }

    m_runSnapshotPending = true;

    const QString path = QStringLiteral("/runs/%1")
        .arg(encodedPathSegment(runId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, runId, reconnectIfRunning]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (runId != m_activeRunId) {
                return;
            }

            m_runSnapshotPending = false;

            if (!result.ok) {
                if (reconnectIfRunning && m_runReconnectAttempts < 3) {
                    ++m_runReconnectAttempts;
                    const int delay = 300 * m_runReconnectAttempts;

                    QTimer::singleShot(delay, this, [this, runId]() {
                        if (runId == m_activeRunId && m_responding) {
                            subscribeToRunEvents(runId, m_lastRunEventSequence);
                        }
                    });
                    return;
                }

                setErrorMessage(
                    QStringLiteral(
                        "Lost the live AI Run connection. Refresh this Chat to reconnect."
                    )
                );
                clearActiveRun();
                return;
            }

            const QJsonObject run = result.object
                .value(QStringLiteral("run"))
                .toObject();
            const QString status = run.value(QStringLiteral("status")).toString();

            applyRunSnapshot(run);

            if (
                status == QStringLiteral("running")
                && reconnectIfRunning
                && runId == m_activeRunId
            ) {
                if (m_runReconnectAttempts >= 3) {
                    setErrorMessage(
                        QStringLiteral(
                            "Lost the live AI Run connection. Refresh this Chat to reconnect."
                        )
                    );
                    clearActiveRun();
                    return;
                }

                ++m_runReconnectAttempts;
                const int delay = 300 * m_runReconnectAttempts;

                QTimer::singleShot(delay, this, [this, runId]() {
                    if (runId == m_activeRunId && m_responding) {
                        subscribeToRunEvents(runId, m_lastRunEventSequence);
                    }
                });
            }
        }
    );
}

void ChatStore::applyRunSnapshot(const QJsonObject &run)
{
    const QString runId = run.value(QStringLiteral("id")).toString();

    if (runId.isEmpty() || runId != m_activeRunId) {
        return;
    }

    const QString assistantMessageId = run
        .value(QStringLiteral("assistantMessageId"))
        .toString();

    if (!assistantMessageId.isEmpty()) {
        m_activeRunAssistantMessageId = assistantMessageId;
    }

    const QJsonValue finalResponse = run.value(QStringLiteral("finalResponse"));

    if (finalResponse.isString()) {
        const QString responseContent = finalResponse.toString();

        if (m_activeRunContent != responseContent) {
            m_activeRunContent = responseContent;
            emit activeRunContentChanged();
        }

        updateMessageContent(
            m_activeRunAssistantMessageId,
            responseContent
        );
    }

    const QString phase = run.value(QStringLiteral("phase")).toString();

    if (!phase.isEmpty()) {
        setRunPhase(phase);
    }

    setCompletionMetadata(
        run.value(QStringLiteral("provider")).toString(),
        run.value(QStringLiteral("model")).toString()
    );

    const QString status = run.value(QStringLiteral("status")).toString();

    if (status == QStringLiteral("running")) {
        setResponding(true);
        return;
    }

    qInfo().noquote()
        << "[AIRunClient] terminal"
        << "runId=" + runId
        << "status=" + status
        << "phase=" + phase
        << "elapsedMs=" + QString::number(
            m_activeRunClock.isValid() ? m_activeRunClock.elapsed() : -1
        )
        << "events=" + QString::number(m_runDiagnosticEventCount)
        << "deltas=" + QString::number(m_runDiagnosticDeltaCount)
        << "characters=" + QString::number(m_runDiagnosticDeltaCharacters)
        << "subscriptions=" + QString::number(m_runDiagnosticSubscriptions);

    if (status == QStringLiteral("completed")) {
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("complete")
        );
    } else if (status == QStringLiteral("cancelled")) {
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("cancelled")
        );
    } else {
        updateMessageStatus(
            m_activeRunAssistantMessageId,
            QStringLiteral("failed")
        );

        const QString message = run.value(QStringLiteral("errorMessage")).toString();

        if (!message.isEmpty()) {
            setErrorMessage(message);
        }
    }

    promoteSelectedChat();
    clearActiveRun();
}

void ChatStore::setActiveRun(const QJsonObject &run)
{
    const QString runId = run.value(QStringLiteral("id")).toString();

    if (runId.isEmpty()) {
        return;
    }

    const bool changed = runId != m_activeRunId;
    const QString assistantMessageId = run
        .value(QStringLiteral("assistantMessageId"))
        .toString();

    m_activeRunId = runId;
    m_activeRunAssistantMessageId = assistantMessageId;
    m_runPhase = run.value(QStringLiteral("phase")).toString();
    m_cancellingRun = false;
    m_runSnapshotPending = false;

    if (changed) {
        QString initialContent;

        m_runActivity = {
            {
                QStringLiteral("provider"),
                run.value(QStringLiteral("provider")).toString()
            },
            {
                QStringLiteral("model"),
                run.value(QStringLiteral("model")).toString()
            },
        };

        for (const QVariant &value : m_messages) {
            const QVariantMap message = value.toMap();

            if (
                message.value(QStringLiteral("id")).toString()
                == assistantMessageId
            ) {
                initialContent = message
                    .value(QStringLiteral("content"))
                    .toString();
                break;
            }
        }

        m_runDeltaFlushTimer.stop();
        m_pendingRunDelta.clear();
        m_activeRunContent = initialContent;
        m_lastRunEventSequence = 0;
        m_runReconnectAttempts = 0;
        m_runDiagnosticEventCount = 0;
        m_runDiagnosticDeltaCount = 0;
        m_runDiagnosticDeltaCharacters = 0;
        m_runDiagnosticSubscriptions = 0;
        m_runEventBuffer.clear();
        m_activeRunClock.restart();
        emit activeRunContentChanged();

        qInfo().noquote()
            << "[AIRunClient] attached"
            << "runId=" + runId
            << "assistantMessageId=" + assistantMessageId
            << "phase=" + m_runPhase;
    }

    setResponding(
        run.value(QStringLiteral("status")).toString()
            == QStringLiteral("running")
    );
    emit activeRunChanged();
}

void ChatStore::setRunPhase(const QString &phase)
{
    if (m_runPhase == phase) {
        return;
    }

    m_runPhase = phase;
    emit activeRunChanged();
}

void ChatStore::updateRunActivity(
    const QString &eventType,
    const QJsonObject &payload
)
{
    QVariantMap nextActivity = m_runActivity;

    if (eventType == QStringLiteral("run.started")) {
        nextActivity.insert(
            QStringLiteral("attachedFiles"),
            payload.value(QStringLiteral("attachedFiles")).toArray().toVariantList()
        );
        nextActivity.insert(
            QStringLiteral("provider"),
            payload.value(QStringLiteral("provider")).toString()
        );
        nextActivity.insert(
            QStringLiteral("model"),
            payload.value(QStringLiteral("model")).toString()
        );
    } else if (eventType == QStringLiteral("retrieval.started")) {
        nextActivity.insert(QStringLiteral("retrievalStarted"), true);
    } else if (eventType == QStringLiteral("retrieval.completed")) {
        nextActivity.insert(QStringLiteral("retrievalComplete"), true);
        nextActivity.insert(
            QStringLiteral("attachedSourceCount"),
            payload.value(QStringLiteral("attachedSourceCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("attachedSources"),
            payload.value(QStringLiteral("attachedSources")).toArray().toVariantList()
        );
        nextActivity.insert(
            QStringLiteral("retrievedSourceCount"),
            payload.value(QStringLiteral("retrievedSourceCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("retrievedFileCount"),
            payload.value(QStringLiteral("retrievedFileCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("retrievedSources"),
            payload.value(QStringLiteral("retrievedSources")).toArray().toVariantList()
        );
        nextActivity.insert(
            QStringLiteral("warningCount"),
            payload.value(QStringLiteral("warningCount")).toInt()
        );
    } else if (eventType == QStringLiteral("context.started")) {
        nextActivity.insert(QStringLiteral("contextStarted"), true);
    } else if (eventType == QStringLiteral("context.completed")) {
        nextActivity.insert(QStringLiteral("contextComplete"), true);
        nextActivity.insert(
            QStringLiteral("includedMessageCount"),
            payload.value(QStringLiteral("includedMessageCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("omittedMessageCount"),
            payload.value(QStringLiteral("omittedMessageCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("estimatedInputTokens"),
            payload.value(QStringLiteral("estimatedInputTokens")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("sourceCount"),
            payload.value(QStringLiteral("sourceCount")).toInt()
        );
        nextActivity.insert(
            QStringLiteral("warningCount"),
            payload.value(QStringLiteral("warningCount")).toInt()
        );
    } else if (eventType == QStringLiteral("model.started")) {
        nextActivity.insert(QStringLiteral("modelStarted"), true);
        nextActivity.insert(
            QStringLiteral("provider"),
            payload.value(QStringLiteral("provider")).toString()
        );
        nextActivity.insert(
            QStringLiteral("model"),
            payload.value(QStringLiteral("model")).toString()
        );
        nextActivity.insert(
            QStringLiteral("toolsEnabled"),
            payload.value(QStringLiteral("toolsEnabled")).toBool(false)
        );
        nextActivity.insert(
            QStringLiteral("availableToolCount"),
            payload.value(QStringLiteral("toolCount")).toInt()
        );
    } else if (eventType == QStringLiteral("model.delta")) {
        nextActivity.insert(QStringLiteral("modelOutputStarted"), true);
    } else if (
        eventType == QStringLiteral("tool.requested")
        || eventType == QStringLiteral("tool.started")
        || eventType == QStringLiteral("tool.completed")
        || eventType == QStringLiteral("tool.failed")
        || eventType == QStringLiteral("tool.cancelled")
    ) {
        QVariantList executions = nextActivity
            .value(QStringLiteral("toolExecutions"))
            .toList();
        const QString executionId = payload
            .value(QStringLiteral("executionId"))
            .toString();
        int executionIndex = -1;

        for (qsizetype index = 0; index < executions.size(); ++index) {
            if (
                executions.at(index)
                    .toMap()
                    .value(QStringLiteral("executionId"))
                    .toString()
                == executionId
            ) {
                executionIndex = static_cast<int>(index);
                break;
            }
        }

        QVariantMap execution = executionIndex >= 0
            ? executions.at(executionIndex).toMap()
            : QVariantMap{};
        execution.insert(QStringLiteral("executionId"), executionId);
        execution.insert(
            QStringLiteral("toolId"),
            payload.value(QStringLiteral("toolId")).toString()
        );

        const QString toolName = payload
            .value(QStringLiteral("toolName"))
            .toString();

        if (!toolName.isEmpty()) {
            execution.insert(QStringLiteral("toolName"), toolName);
        }

        if (eventType == QStringLiteral("tool.requested")) {
            execution.insert(QStringLiteral("status"), QStringLiteral("requested"));
        } else if (eventType == QStringLiteral("tool.started")) {
            execution.insert(QStringLiteral("status"), QStringLiteral("running"));
            execution.insert(
                QStringLiteral("input"),
                payload.value(QStringLiteral("input")).toObject().toVariantMap()
            );
        } else if (eventType == QStringLiteral("tool.completed")) {
            execution.insert(QStringLiteral("status"), QStringLiteral("completed"));
            execution.insert(
                QStringLiteral("output"),
                payload.value(QStringLiteral("output")).toObject().toVariantMap()
            );
            execution.insert(
                QStringLiteral("durationMs"),
                payload.value(QStringLiteral("durationMs")).toDouble()
            );
        } else if (eventType == QStringLiteral("tool.cancelled")) {
            execution.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
            execution.insert(
                QStringLiteral("errorCode"),
                payload.value(QStringLiteral("errorCode")).toString()
            );
            execution.insert(
                QStringLiteral("message"),
                payload.value(QStringLiteral("message")).toString()
            );
        } else {
            execution.insert(QStringLiteral("status"), QStringLiteral("failed"));
            execution.insert(
                QStringLiteral("errorCode"),
                payload.value(QStringLiteral("errorCode")).toString()
            );
            execution.insert(
                QStringLiteral("message"),
                payload.value(QStringLiteral("message")).toString()
            );
        }

        if (executionIndex >= 0) {
            executions[executionIndex] = execution;
        } else {
            executions.append(execution);
        }

        nextActivity.insert(QStringLiteral("toolExecutions"), executions);
        nextActivity.insert(QStringLiteral("toolCallCount"), executions.size());

        if (
            eventType == QStringLiteral("tool.requested")
            || eventType == QStringLiteral("tool.started")
        ) {
            nextActivity.insert(QStringLiteral("activeToolExecutionId"), executionId);
            nextActivity.insert(
                QStringLiteral("activeToolName"),
                execution.value(QStringLiteral("toolName")).toString()
            );
        } else if (
            nextActivity
                .value(QStringLiteral("activeToolExecutionId"))
                .toString()
            == executionId
        ) {
            nextActivity.remove(QStringLiteral("activeToolExecutionId"));
            nextActivity.remove(QStringLiteral("activeToolName"));
        }
    } else if (eventType == QStringLiteral("model.completed")) {
        nextActivity.insert(QStringLiteral("modelComplete"), true);
        nextActivity.insert(
            QStringLiteral("toolCallCount"),
            payload.value(QStringLiteral("toolCallCount")).toInt(
                nextActivity.value(QStringLiteral("toolCallCount")).toInt()
            )
        );
        nextActivity.insert(
            QStringLiteral("toolRoundCount"),
            payload.value(QStringLiteral("toolRoundCount")).toInt()
        );
    }

    if (nextActivity == m_runActivity) {
        return;
    }

    m_runActivity = nextActivity;
    emit activeRunChanged();
}

void ChatStore::setCancellingRun(bool cancelling)
{
    if (m_cancellingRun == cancelling) {
        return;
    }

    m_cancellingRun = cancelling;
    emit activeRunChanged();
}

void ChatStore::clearActiveRun()
{
    if (m_runEventReply) {
        m_runEventReply->disconnect(this);
        m_runEventReply->abort();
        m_runEventReply->deleteLater();
        m_runEventReply = nullptr;
    }

    const bool hadRun = !m_activeRunId.isEmpty()
        || !m_runPhase.isEmpty()
        || !m_runActivity.isEmpty()
        || m_cancellingRun;
    const bool hadActiveContent = !m_activeRunContent.isEmpty();

    if (
        !m_activeRunAssistantMessageId.isEmpty()
        && hadActiveContent
    ) {
        updateMessageContent(
            m_activeRunAssistantMessageId,
            m_activeRunContent
        );
    }

    m_runDeltaFlushTimer.stop();
    m_activeRunId.clear();
    m_activeRunAssistantMessageId.clear();
    m_activeRunContent.clear();
    m_runPhase.clear();
    m_runActivity.clear();
    m_pendingRunDelta.clear();
    m_runEventBuffer.clear();
    m_lastRunEventSequence = 0;
    m_runReconnectAttempts = 0;
    m_runDiagnosticEventCount = 0;
    m_runDiagnosticDeltaCount = 0;
    m_runDiagnosticDeltaCharacters = 0;
    m_runDiagnosticSubscriptions = 0;
    m_activeRunClock.invalidate();
    m_cancellingRun = false;
    m_runSnapshotPending = false;
    setResponding(false);

    if (hadActiveContent) {
        emit activeRunContentChanged();
    }

    if (hadRun) {
        emit activeRunChanged();
    }
}

void ChatStore::flushPendingRunDelta()
{
    if (m_pendingRunDelta.isEmpty()) {
        return;
    }

    m_activeRunContent.append(m_pendingRunDelta);
    m_pendingRunDelta.clear();
    emit activeRunContentChanged();
}

void ChatStore::updateMessageContent(
    const QString &messageId,
    const QString &content
)
{
    if (messageId.isEmpty()) {
        return;
    }

    QVariantList nextMessages = m_messages;

    for (qsizetype index = 0; index < nextMessages.size(); ++index) {
        QVariantMap message = nextMessages.at(index).toMap();

        if (message.value(QStringLiteral("id")).toString() != messageId) {
            continue;
        }

        message.insert(QStringLiteral("content"), content);
        nextMessages[index] = message;
        setMessages(nextMessages);
        return;
    }
}

void ChatStore::updateMessageStatus(
    const QString &messageId,
    const QString &status
)
{
    if (messageId.isEmpty() || status.isEmpty()) {
        return;
    }

    QVariantList nextMessages = m_messages;

    for (qsizetype index = 0; index < nextMessages.size(); ++index) {
        QVariantMap message = nextMessages.at(index).toMap();

        if (message.value(QStringLiteral("id")).toString() != messageId) {
            continue;
        }

        message.insert(QStringLiteral("status"), status);
        nextMessages[index] = message;
        setMessages(nextMessages);
        return;
    }
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

void ChatStore::createChat(
    const QString &libraryId,
    const QString &agentId
)
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
    if (!agentId.isEmpty()) {
        body.insert(QStringLiteral("agentId"), agentId);
    }

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
        resumeActiveRunForSelectedChat();
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

    const QString requestedChatId = m_selectedChatId;
    const QString optimisticUserId = QStringLiteral("optimistic-user-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString optimisticAssistantId = QStringLiteral("optimistic-assistant-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    setErrorMessage({});
    setResponding(true);
    setRunPhase(QStringLiteral("run.started"));

    QVariantList nextMessages = m_messages;
    nextMessages.append(optimisticMessage(
        optimisticUserId,
        requestedChatId,
        QStringLiteral("user"),
        trimmedContent
    ));
    nextMessages.append(optimisticMessage(
        optimisticAssistantId,
        requestedChatId,
        QStringLiteral("assistant"),
        QString{}
    ));
    setMessages(nextMessages);

    QJsonObject body;
    body.insert(QStringLiteral("content"), trimmedContent);

    const QString path = QStringLiteral("/chats/%1/runs")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [
            this,
            reply,
            requestedChatId,
            optimisticUserId,
            optimisticAssistantId
        ]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (requestedChatId != m_selectedChatId) {
                clearActiveRun();
                return;
            }

            if (!result.ok) {
                QVariantList failedMessages;
                failedMessages.reserve(m_messages.size());

                for (const QVariant &value : m_messages) {
                    QVariantMap message = value.toMap();
                    const QString id = message
                        .value(QStringLiteral("id"))
                        .toString();

                    if (id == optimisticAssistantId) {
                        continue;
                    }

                    if (id == optimisticUserId) {
                        message.insert(
                            QStringLiteral("status"),
                            QStringLiteral("failed")
                        );
                    }

                    failedMessages.append(message);
                }

                setMessages(failedMessages);
                clearActiveRun();
                setErrorMessage(result.errorMessage);
                return;
            }

            QVariantList storedMessages;
            storedMessages.reserve(m_messages.size());

            for (const QVariant &value : m_messages) {
                const QString id = value
                    .toMap()
                    .value(QStringLiteral("id"))
                    .toString();

                if (
                    id != optimisticUserId
                    && id != optimisticAssistantId
                ) {
                    storedMessages.append(value);
                }
            }

            const QJsonObject userMessage = result.object
                .value(QStringLiteral("userMessage"))
                .toObject();
            const QJsonObject assistantMessage = result.object
                .value(QStringLiteral("assistantMessage"))
                .toObject();
            const QJsonObject run = result.object
                .value(QStringLiteral("run"))
                .toObject();

            storedMessages.append(mapMessage(userMessage));
            storedMessages.append(mapMessage(assistantMessage));
            setMessages(storedMessages);
            setActiveRun(run);
            setCompletionMetadata(
                run.value(QStringLiteral("provider")).toString(),
                run.value(QStringLiteral("model")).toString()
            );

            if (m_activeRunId.isEmpty()) {
                clearActiveRun();
                setErrorMessage(
                    QStringLiteral("Archivist API returned an invalid AI Run.")
                );
                return;
            }

            subscribeToRunEvents(m_activeRunId);
        }
    );
}

void ChatStore::cancelActiveRun()
{
    if (
        m_activeRunId.isEmpty()
        || !m_responding
        || m_cancellingRun
    ) {
        return;
    }

    const QString runId = m_activeRunId;
    setCancellingRun(true);

    qInfo().noquote()
        << "[AIRunClient] cancel requested"
        << "runId=" + runId
        << "characters=" + QString::number(m_activeRunContent.size())
        << "elapsedMs=" + QString::number(
            m_activeRunClock.isValid() ? m_activeRunClock.elapsed() : -1
        );

    const QString path = QStringLiteral("/runs/%1/cancel")
        .arg(encodedPathSegment(runId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, runId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (runId != m_activeRunId) {
            return;
        }

        if (!result.ok) {
            setCancellingRun(false);
            setErrorMessage(result.errorMessage);
            requestRunSnapshot(runId, true);
            return;
        }

        applyRunSnapshot(
            result.object.value(QStringLiteral("run")).toObject()
        );
    });
}

void ChatStore::finishMessageReveal(const QString &messageId)
{
    if (messageId.isEmpty()) {
        return;
    }

    QVariantList nextMessages = m_messages;

    for (qsizetype index = 0; index < nextMessages.size(); ++index) {
        QVariantMap message = nextMessages.at(index).toMap();

        if (
            message.value(QStringLiteral("id")).toString() != messageId
            || !message.value(QStringLiteral("animateReveal")).toBool()
        ) {
            continue;
        }

        message.remove(QStringLiteral("animateReveal"));
        nextMessages[index] = message;
        setMessages(nextMessages);
        return;
    }
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

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestedChatId, agentId]() {
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
        emit agentAssigned(agentId);
    });
}

void ChatStore::attachAgentToSelectedChat(const QString &agentId)
{
    if (
        m_selectedChatId.isEmpty()
        || agentId.isEmpty()
        || m_responding
        || m_assigningAgent
        || m_mutating
        || m_mutatingAttachment
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;

    setErrorMessage({});
    setAssigningAgent(true);

    QJsonObject body;
    body.insert(QStringLiteral("agentId"), agentId);

    const QString path = QStringLiteral("/chats/%1/agents")
        .arg(encodedPathSegment(requestedChatId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
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
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Agent roster."));
            return;
        }

        replaceChat(updatedChat);
    });
}

void ChatStore::detachAgentFromSelectedChat(const QString &agentId)
{
    if (
        m_selectedChatId.isEmpty()
        || agentId.isEmpty()
        || m_responding
        || m_assigningAgent
        || m_mutating
        || m_mutatingAttachment
    ) {
        return;
    }

    const QString requestedChatId = m_selectedChatId;
    const QString path = QStringLiteral("/chats/%1/agents/%2")
        .arg(
            encodedPathSegment(requestedChatId),
            encodedPathSegment(agentId)
        );

    setErrorMessage({});
    setAssigningAgent(true);

    QNetworkReply *reply = m_network.deleteResource(requestFor(path));

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
            setErrorMessage(QStringLiteral("Archivist API returned an invalid Agent roster."));
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
