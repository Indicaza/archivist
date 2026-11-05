#include "librarian_scheduler.h"

#include "knowledge_jobs.h"
#include "knowledge_types.h"
#include "librarian_store.h"
#include "librarian_types.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <QDebug>

using namespace Librarian;

namespace LibrarianRuns
{
QVariantMap startRun(CoreDatabase &database, const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request)
{
    if (!Kind::isValid(librarian)) {
        throw CoreError(QStringLiteral("Unknown librarian: %1").arg(librarian), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_UNKNOWN")}});
    }

    return Transactions::join(database, [&]() {
        const auto settings = LibrarianStore::settings(database, libraryId);

        if (!settings.enabled) {
            throw CoreError(QStringLiteral("Turn on librarians for this Library first."), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_DISABLED")}});
        }

        if (!settings.allows(librarian)) {
            throw CoreError(QStringLiteral("The %1 librarian is turned off for this Library.").arg(librarian), {{QStringLiteral("code"), QStringLiteral("LIBRARIAN_DISABLED")}});
        }

        QVariantMap runRequest;
        const QString topic = request.value(QStringLiteral("topic")).toString().simplified();

        if (librarian == Kind::research && !topic.isEmpty()) {
            runRequest.insert(QStringLiteral("topic"), topic.left(300));
        }

        const LibrarianStore::RunStart start = LibrarianStore::startRun(database, libraryId, librarian, trigger, runRequest);
        QVariantMap run = start.run;
        run.insert(QStringLiteral("created"), start.created);

        if (!start.created) {
            return run;
        }

        const QString runId = run.value(QStringLiteral("id")).toString();
        KnowledgeJobs::JobOptions options;
        options.librarian = librarian;
        options.runId = runId;
        options.priority = Priority::forKind(librarian);
        // The librarian's name is part of the request so a person's own
        // "Check sources" job is never mistaken for maintenance.
        QVariantMap jobRequest{{QStringLiteral("librarian"), librarian}};
        QString jobType = Knowledge::JobType::reconcileSources;

        if (librarian == Kind::research) {
            jobType = Knowledge::JobType::discoverRelated;
            jobRequest.insert(runRequest);
        } else if (librarian == Kind::verification) {
            jobType = Knowledge::JobType::identifyContradictions;
        }

        const QVariantMap job = KnowledgeJobs::enqueue(database, libraryId, jobType, jobRequest, options);

        if (!job.value(QStringLiteral("created")).toBool()) {
            // The same work is already queued (an earlier run that was not
            // settled): this run has nothing of its own to do.
            LibrarianStore::noteRun(database, runId, {{QStringLiteral("duplicateOf"), job.value(QStringLiteral("id"))}});
            LibrarianStore::finishRun(database, runId, RunStatus::completed);
            run = *LibrarianStore::run(database, runId);
            run.insert(QStringLiteral("created"), false);
        }

        return run;
    });
}

QStringList cancelRun(CoreDatabase &database, const QString &runId)
{
    return Transactions::join(database, [&]() {
        QStringList cancelled;

        for (const QVariant &value : KnowledgeJobs::runJobs(database, runId)) {
            const QVariantMap job = value.toMap();
            const QString status = job.value(QStringLiteral("status")).toString();

            if ((status == Knowledge::JobStatus::queued || status == Knowledge::JobStatus::running) && KnowledgeJobs::cancel(database, job.value(QStringLiteral("id")).toString())) {
                cancelled.append(job.value(QStringLiteral("id")).toString());
            }
        }

        LibrarianStore::finishRun(database, runId, RunStatus::cancelled, QStringLiteral("Cancelled."));
        return cancelled;
    });
}

QStringList disable(CoreDatabase &database, const QString &libraryId)
{
    return Transactions::join(database, [&]() {
        LibrarianStore::updateSettings(database, libraryId, {{QStringLiteral("enabled"), false}});
        QStringList cancelled;

        for (const QString &librarian : Kind::all()) {
            if (const auto run = LibrarianStore::activeRun(database, libraryId, librarian)) {
                cancelled += cancelRun(database, run->value(QStringLiteral("id")).toString());
            }
        }

        return cancelled;
    });
}
}

namespace Knowledge
{
LibrarianScheduler::LibrarianScheduler(CoreDatabase &database, KnowledgeJobRunner &runner, SchedulerPolicy policy, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_runner(runner)
    , m_policy(policy)
{
    m_tick.setInterval(std::max(1000, m_policy.tickMs));
    connect(&m_tick, &QTimer::timeout, this, &LibrarianScheduler::tick);
    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, &LibrarianScheduler::tick);
    connect(&m_runner, &KnowledgeJobRunner::runFinished, this, [this](const QString &libraryId, const QString &) { emit changed(libraryId); });
}

void LibrarianScheduler::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime LibrarianScheduler::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

void LibrarianScheduler::start()
{
    try {
        // Runs whose jobs all finished before a restart are closed now;
        // runs with queued jobs continue as the runner works through them.
        m_database.transaction([&]() { LibrarianStore::settleRuns(m_database); });
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Librarian]" << error.what();
    }

    if (!m_lastInput.isValid()) {
        m_lastInput = now();
    }

    m_tick.start();
}

void LibrarianScheduler::noteLibraryChanged(const QString &libraryId)
{
    if (libraryId.isEmpty()) {
        return;
    }

    try {
        if (!LibrarianStore::settings(m_database, libraryId).enabled) {
            return;
        }

        m_database.transaction([&]() { LibrarianStore::noteChange(m_database, libraryId, now()); });
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Librarian]" << error.what();
        return;
    }

    // Each change restarts the quiet period: a burst becomes one pass.
    m_debounce.start(std::max(0, m_policy.debounceMs));
}

void LibrarianScheduler::setActivity(const QDateTime &lastInput, bool interactiveBusy)
{
    if (lastInput.isValid() && (!m_lastInput.isValid() || lastInput > m_lastInput)) {
        m_lastInput = lastInput;
    }

    m_busy = interactiveBusy;
    m_runner.setInteractiveBusy(interactiveBusy);
}

bool LibrarianScheduler::idleFor(int minutes) const
{
    return !m_busy && m_lastInput.isValid() && m_lastInput.secsTo(now()) >= static_cast<qint64>(minutes) * 60;
}

void LibrarianScheduler::tryStart(const QString &libraryId, const QString &librarian, const QString &trigger)
{
    try {
        const QVariantMap run = m_database.transaction([&]() { return LibrarianRuns::startRun(m_database, libraryId, librarian, trigger); });

        if (run.value(QStringLiteral("created")).toBool()) {
            m_lastStarted.append(QVariantMap{
                {QStringLiteral("libraryId"), libraryId},
                {QStringLiteral("librarian"), librarian},
                {QStringLiteral("trigger"), trigger},
                {QStringLiteral("runId"), run.value(QStringLiteral("id"))},
            });
            LiveEvents::marker(QStringLiteral("librarian"), QStringLiteral("start"), {{QStringLiteral("librarian"), librarian}, {QStringLiteral("trigger"), trigger}});
            emit changed(libraryId);
        }
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Librarian]" << librarian << error.what();
    }
}

void LibrarianScheduler::tick()
{
    m_lastStarted.clear();
    QStringList libraries;

    try {
        libraries = LibrarianStore::enabledLibraries(m_database);
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Librarian]" << error.what();
        return;
    }

    const QDateTime current = now();

    for (const QString &libraryId : std::as_const(libraries)) {
        LibrarianStore::Settings settings;

        try {
            settings = LibrarianStore::settings(m_database, libraryId);
        } catch (const std::exception &) {
            continue;
        }

        if (settings.paused || settings.mode == Mode::manual) {
            continue;
        }

        if (!settings.backoffUntil.isEmpty()) {
            const QDateTime until = QDateTime::fromString(settings.backoffUntil, Qt::ISODateWithMs);

            if (until.isValid() && until > current) {
                continue;
            }
        }

        // Change-triggered maintenance: a noted change after its quiet
        // period, or a difference from the snapshot (changes made while
        // Archivist was closed, or by a scan of another Library).
        if (settings.maintenance && !LibrarianStore::activeRun(m_database, libraryId, Kind::maintenance)) {
            bool quiet = true;

            if (!settings.changeNotedAt.isEmpty()) {
                const QDateTime noted = QDateTime::fromString(settings.changeNotedAt, Qt::ISODateWithMs);
                quiet = !noted.isValid() || noted.msecsTo(current) >= m_policy.debounceMs;
            }

            if (quiet) {
                bool changed = false;

                try {
                    changed = LibrarianStore::diffSources(m_database, libraryId).any();
                } catch (const std::exception &) {
                }

                if (changed) {
                    tryStart(libraryId, Kind::maintenance, Trigger::change);
                } else if (!settings.changeNotedAt.isEmpty()) {
                    // A hint that changed nothing the librarians cite.
                    m_database.transaction([&]() { LibrarianStore::clearChange(m_database, libraryId); });
                }
            }
        }

        if (settings.mode == Mode::idle && idleFor(settings.idleMinutes)) {
            for (const QString &librarian : {Kind::research, Kind::verification}) {
                const QString last = settings.lastRunAt(librarian);
                const QDateTime lastAt = QDateTime::fromString(last, Qt::ISODateWithMs);

                if (settings.allows(librarian) && (!lastAt.isValid() || lastAt.secsTo(current) >= static_cast<qint64>(m_policy.idleRepeatMinutes) * 60)) {
                    tryStart(libraryId, librarian, Trigger::idle);
                }
            }
        }

        if (settings.mode == Mode::scheduled) {
            const QDateTime next = QDateTime::fromString(settings.nextScheduledAt, Qt::ISODateWithMs);

            if (!next.isValid()) {
                // The first interval starts counting when the mode is chosen.
                m_database.transaction([&]() { LibrarianStore::setNextScheduled(m_database, libraryId, current.addSecs(static_cast<qint64>(settings.scheduleMinutes) * 60)); });
            } else if (next <= current) {
                for (const QString &librarian : Kind::all()) {
                    if (settings.allows(librarian)) {
                        tryStart(libraryId, librarian, Trigger::schedule);
                    }
                }

                m_database.transaction([&]() { LibrarianStore::setNextScheduled(m_database, libraryId, current.addSecs(static_cast<qint64>(settings.scheduleMinutes) * 60)); });
            }
        }
    }

    if (!m_lastStarted.isEmpty()) {
        m_runner.poke();
    }
}

QVariantMap LibrarianScheduler::startRun(const QString &libraryId, const QString &librarian, const QString &trigger, const QVariantMap &request)
{
    const QVariantMap run = m_database.transaction([&]() { return LibrarianRuns::startRun(m_database, libraryId, librarian, trigger, request); });
    m_runner.poke();
    emit changed(libraryId);
    return run;
}

void LibrarianScheduler::cancelRun(const QString &runId)
{
    const QStringList jobs = m_database.transaction([&]() { return LibrarianRuns::cancelRun(m_database, runId); });

    for (const QString &jobId : jobs) {
        m_runner.cancelJob(jobId);
    }
}

void LibrarianScheduler::pause(const QString &libraryId)
{
    m_database.transaction([&]() { LibrarianStore::updateSettings(m_database, libraryId, {{QStringLiteral("paused"), true}}); });
    m_runner.suspendLibrarians(libraryId);
    emit changed(libraryId);
}

void LibrarianScheduler::resume(const QString &libraryId)
{
    m_database.transaction([&]() { LibrarianStore::updateSettings(m_database, libraryId, {{QStringLiteral("paused"), false}}); });
    m_runner.poke();
    emit changed(libraryId);
}

void LibrarianScheduler::disable(const QString &libraryId)
{
    const QStringList jobs = m_database.transaction([&]() { return LibrarianRuns::disable(m_database, libraryId); });

    for (const QString &jobId : jobs) {
        m_runner.cancelJob(jobId);
    }

    m_runner.suspendLibrarians(libraryId);
    emit changed(libraryId);
}
}
