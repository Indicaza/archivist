#pragma once

#include "Core/core_runtime.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantMap>
#include <functional>
#include <optional>

class AIWorker;

// The in-process replacement for the Node AI runtime: chat runs, the model
// tool loop, AI edit review, and the run event stream.
//
// Everything runs on one "ArchivistAI" thread with its own database
// connection and network manager. Results come back on the caller's thread.
// Every persisted run event is also emitted through runEvent() in order, which
// replaces the old SSE stream.
class AIRuntime final : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const CoreResult &)>;

    [[nodiscard]] static AIRuntime *active();

    explicit AIRuntime(QObject *parent = nullptr);
    ~AIRuntime() override;

    // Starts the AI thread and settles runs a previous process left running.
    bool start(const QString &databasePath);
    void stop();
    [[nodiscard]] bool ready() const;

    // POST /chats/:id/runs -> {run, userMessage, assistantMessage}
    void startChatRun(const QString &chatId, const QString &content, QObject *context, Callback done);
    // POST /runs/:id/cancel -> run
    void cancelRun(const QString &runId, QObject *context, Callback done = {});
    // GET /chats/:id/runs -> runs
    void runsForChat(const QString &chatId, QObject *context, Callback done);
    // GET /runs/:id -> run; with events after `afterSequence` -> {run, events}
    void runWithEvents(const QString &runId, int afterSequence, QObject *context, Callback done);

    // /cognition/edits
    void editProposalsForChat(const QString &chatId, QObject *context, Callback done);
    void editProposal(const QString &proposalId, QObject *context, Callback done);
    // -> {proposal, synchronization}
    void approveEditProposal(const QString &proposalId, const std::optional<QStringList> &operationIds, QObject *context, Callback done);
    // -> {proposal}
    void rejectEditProposal(const QString &proposalId, QObject *context, Callback done);
    // -> {proposal, synchronization}
    void undoEditProposal(const QString &proposalId, QObject *context, Callback done);

signals:
    // {id, runId, sequence, eventType, payload, createdAt}
    void runEvent(const QVariantMap &event);

private:
    void invoke(std::function<QVariant(AIWorker &)> job, QObject *context, Callback done);

    QThread m_thread;
    QPointer<AIWorker> m_worker;
    bool m_ready = false;
};
