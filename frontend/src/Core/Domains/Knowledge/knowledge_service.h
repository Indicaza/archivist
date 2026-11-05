#pragma once

#include "Core/Database/core_database.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

namespace Knowledge
{
class KnowledgeWorker;

// The app's handle on the Living Wiki (QML singleton "Wiki").
//
// Jobs (writing a page with the model, reconciling sources) run on their own
// "ArchivistWiki" thread with their own database connection and network
// manager, so the UI never waits on a model. Reads and the person's review
// actions (approve, reject, edit, verify, restore) are short transactions on
// this thread's own connection.
//
// A person's Wiki jobs start from their actions. Librarian work (see
// LIBRARIAN.md) runs on the same thread only for Libraries where a person
// turned librarians on, and model calls only with their consent; it never
// changes a page without their approval. Failures come back as
// {ok: false, code, message}; reads return empty values.
class KnowledgeService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // Bumped whenever pages, proposals or jobs change, so QML bindings
    // re-read what they show.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit KnowledgeService(QObject *parent = nullptr);
    ~KnowledgeService() override;

    bool start(const QString &databasePath);
    void stop();

    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] int revision() const { return m_revision; }

    // ---- Reads ----
    Q_INVOKABLE QVariantMap overview(const QString &libraryId);
    Q_INVOKABLE QVariantList pages(const QString &libraryId, bool includeInactive = false);
    // The page with claims, citations, relations and revisions.
    Q_INVOKABLE QVariantMap page(const QString &pageId);
    Q_INVOKABLE QVariantList revisions(const QString &pageId);
    Q_INVOKABLE QVariantMap pageRevision(const QString &revisionId);
    Q_INVOKABLE QVariantList proposals(const QString &libraryId, const QString &status = QStringLiteral("pending"));
    Q_INVOKABLE QVariantMap proposal(const QString &proposalId);
    Q_INVOKABLE QVariantList jobs(const QString &libraryId, int limit = 30);

    // ---- Jobs (user-initiated) ----
    // request: {topic, fileIds, targetPageId, model, maxPassages}.
    Q_INVOKABLE QVariantMap generate(const QString &libraryId, const QVariantMap &request);
    Q_INVOKABLE QVariantMap expand(const QString &libraryId, const QString &pageId);
    Q_INVOKABLE QVariantMap reconcile(const QString &libraryId);
    Q_INVOKABLE QVariantMap cancelJob(const QString &jobId);
    Q_INVOKABLE QVariantMap retryJob(const QString &jobId);

    // ---- Review ----
    Q_INVOKABLE QVariantMap approve(const QString &proposalId, const QString &note = {});
    Q_INVOKABLE QVariantMap reject(const QString &proposalId, const QString &note = {});
    Q_INVOKABLE QVariantMap createPage(const QString &libraryId, const QString &title, const QString &pageType, const QString &content);
    Q_INVOKABLE QVariantMap editPage(const QString &pageId, const QString &title, const QString &pageType, const QString &content, int baseRevision);
    // action: "verify", "retract" or "reopen".
    Q_INVOKABLE QVariantMap reviewClaim(const QString &claimId, const QString &action, const QString &note = {});
    Q_INVOKABLE QVariantMap restoreRevision(const QString &pageId, const QString &revisionId);
    Q_INVOKABLE QVariantMap proposeDeprecation(const QString &pageId, const QString &reason);
    Q_INVOKABLE QVariantMap proposeMerge(const QString &sourcePageId, const QString &targetPageId, const QString &reason);

    // ---- Librarians ----
    // {settings, usage: {today, last30Days}, runs, activeRuns, findings
    //  (counts by kind), openFindings, pendingProposals, topics (counts by
    //  status), interactiveBusy}
    Q_INVOKABLE QVariantMap librarianStatus(const QString &libraryId);
    // `status` empty: open and proposed.
    Q_INVOKABLE QVariantList librarianFindings(const QString &libraryId, const QString &status = {});
    // Pending proposals the librarians made, newest first.
    Q_INVOKABLE QVariantList librarianProposals(const QString &libraryId);
    Q_INVOKABLE QVariantList librarianTopics(const QString &libraryId, const QString &status = {});
    Q_INVOKABLE QVariantMap setLibrarianEnabled(const QString &libraryId, bool enabled);
    // Permission to send Library text to OpenAI for librarian research.
    Q_INVOKABLE QVariantMap setLibrarianConsent(const QString &libraryId, bool consented);
    // {mode, scheduleMinutes, idleMinutes, maintenance, research,
    //  verification, model, maxCallsPerRun, maxTokensPerRun,
    //  dailyCallBudget, dailyTokenBudget, maxDepth}
    Q_INVOKABLE QVariantMap updateLibrarianSettings(const QString &libraryId, const QVariantMap &changes);
    // librarian: "maintenance", "research" or "verification"; topic only for research.
    Q_INVOKABLE QVariantMap startLibrarian(const QString &libraryId, const QString &librarian, const QString &topic = {});
    Q_INVOKABLE QVariantMap pauseLibrarians(const QString &libraryId);
    Q_INVOKABLE QVariantMap resumeLibrarians(const QString &libraryId);
    Q_INVOKABLE QVariantMap cancelLibrarianRun(const QString &runId);
    Q_INVOKABLE QVariantMap dismissFinding(const QString &findingId);
    Q_INVOKABLE QVariantMap reopenFinding(const QString &findingId);

    // From the app: a Library's files changed (scan, save, move, AI edit).
    Q_INVOKABLE void noteLibraryChanged(const QString &libraryId);
    // From the app: a chat is streaming, so librarian jobs wait.
    void setInteractiveBusy(bool busy);

signals:
    void readyChanged();
    void changed(const QString &libraryId);
    // Wiki content changed: the semantic index should pick it up.
    void knowledgeChanged(const QString &libraryId);

protected:
    // Installed on the application: any key, mouse or wheel input counts as
    // activity for the librarians' idle mode.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    template<typename Work>
    QVariantMap act(const QString &libraryId, Work &&work);
    void reportActivity(bool force);
    void bump(const QString &libraryId, bool content);
    void pokeWorker();

    QThread m_thread;
    QPointer<KnowledgeWorker> m_worker;
    CoreDatabase m_database;
    bool m_ready = false;
    int m_revision = 0;
    bool m_interactiveBusy = false;
    QDateTime m_lastInput;
    QDateTime m_lastReported;
};
}
