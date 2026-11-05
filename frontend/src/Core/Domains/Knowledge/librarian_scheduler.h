#pragma once

#include "Core/Database/core_database.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace Knowledge
{
class KnowledgeJobRunner;
}

// Starting and stopping librarian runs. A run is one bounded pass of one
// librarian over one Library: a root job on the Wiki queue (maintenance:
// reconcile-sources, research: discover-related, verification:
// identify-contradictions) plus whatever follow-up jobs it queues, all
// tagged with the run. At most one run per librarian and Library is active.
//
// These functions only touch the database, so the app's main thread can use
// them on its own connection; the runner picks the jobs up when poked.
namespace LibrarianRuns
{
// {run fields..., created}. Throws CoreError (LIBRARIAN_DISABLED,
// LIBRARIAN_UNKNOWN) when the librarian may not run for this Library.
QVariantMap startRun(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request = {});
// Cancels the run's waiting and running jobs and the run. Returns the ids of
// the jobs it cancelled (the runner drops the request of a running one).
QStringList cancelRun(CoreDatabase &database, const QString &runId);
// Turns the Library's librarians off and cancels their active runs.
QStringList disable(CoreDatabase &database, const QString &libraryId);
}

namespace Knowledge
{
struct SchedulerPolicy
{
    // Quiet time after the last Library change before maintenance starts:
    // a burst of saves or a long scan becomes one pass.
    int debounceMs = 20000;
    // How often the scheduler looks at every enabled Library.
    int tickMs = 60000;
    // In idle mode, research and verification run at most this often.
    int idleRepeatMinutes = 360;
};

// Decides when librarians work without being asked, on the Wiki thread:
//   change     a Library's sources changed (noted, or found by comparing the
//              snapshot) and stayed quiet for `debounceMs` -> maintenance
//   idle       mode "idle", nobody used Archivist for the Library's idle
//              minutes and no chat is streaming -> research, verification
//   schedule   mode "scheduled", the interval came round -> all three
// Mode "manual" never starts anything. Paused Libraries, Libraries in
// provider backoff and disabled librarians are skipped. Everything it
// decides is in the database, so a restart picks up where it left off.
class LibrarianScheduler final : public QObject
{
    Q_OBJECT

public:
    LibrarianScheduler(CoreDatabase &database, KnowledgeJobRunner &runner, SchedulerPolicy policy = {}, QObject *parent = nullptr);

    void setClock(std::function<QDateTime()> clock);
    // Settles runs a restart left behind and starts ticking.
    void start();
    // A scan finished, a file was saved, moved or renamed. Coalesced.
    void noteLibraryChanged(const QString &libraryId);
    // The last time a person used Archivist, and whether a chat streams.
    void setActivity(const QDateTime &lastInput, bool interactiveBusy);
    // Looks at every enabled Library now (the timer calls it too).
    void tick();

    // Person's actions, on the Wiki thread.
    QVariantMap startRun(const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request = {});
    void cancelRun(const QString &runId);
    void pause(const QString &libraryId);
    void resume(const QString &libraryId);
    void disable(const QString &libraryId);

    // Runs started by the last tick ({libraryId, librarian, trigger, runId}).
    [[nodiscard]] QVariantList lastStarted() const { return m_lastStarted; }

signals:
    void changed(const QString &libraryId);

private:
    [[nodiscard]] QDateTime now() const;
    [[nodiscard]] bool idleFor(int minutes) const;
    void tryStart(const QString &libraryId, const QString &librarian, const QString &trigger);

    CoreDatabase &m_database;
    KnowledgeJobRunner &m_runner;
    SchedulerPolicy m_policy;
    std::function<QDateTime()> m_clock;
    QTimer m_tick;
    QTimer m_debounce;
    QDateTime m_lastInput;
    bool m_busy = false;
    QVariantList m_lastStarted;
};
}
