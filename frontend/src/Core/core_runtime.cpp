#include "core_runtime.h"

#include "core_error.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/app_profile.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QtGlobal>
#include <memory>

namespace
{
// Observatory timing for one database job: queue wait, execution and the
// wait for the callback, each a span under one db.<kind> span. Nothing is
// recorded unless a recording is running.
QString operationName(const char *operation, const QObject *context)
{
    if (operation) {
        return QString::fromLatin1(operation);
    }
    return context ? QString::fromLatin1(context->metaObject()->className()) : QStringLiteral("unnamed");
}

QVariantMap resultFields(const CoreResult &result)
{
    QVariantMap fields;
    if (result.value.typeId() == QMetaType::QVariantList) {
        fields.insert(QStringLiteral("rows"), result.value.toList().size());
    }
    if (!result.ok) {
        fields.insert(QStringLiteral("error"), result.errorDetails.isEmpty() ? QStringLiteral("failed") : QStringLiteral("core-error"));
    }
    return fields;
}

struct DbTrace
{
    quint64 job = 0;
    qint64 queuedNs = -1;

    static DbTrace queued(const char *kind, const char *operation, const QObject *context)
    {
        DbTrace trace;
        if (!LiveEvents::recording()) {
            return trace;
        }
        trace.queuedNs = LiveEvents::nowNs();
        trace.job = LiveEvents::beginAt(trace.queuedNs, QStringLiteral("db"), QString::fromLatin1(kind),
                                        {{QStringLiteral("op"), operationName(operation, context)}});
        return trace;
    }

    // On the executing thread: the queue wait ends, execution starts.
    qint64 started() const
    {
        if (!job) {
            return -1;
        }
        const qint64 now = LiveEvents::nowNs();
        LiveEvents::span(QStringLiteral("db"), QStringLiteral("queue-wait"), queuedNs, now, -1, {}, job);
        return now;
    }

    void executed(qint64 startNs, const CoreResult &result) const
    {
        if (job && startNs >= 0) {
            LiveEvents::span(QStringLiteral("db"), QStringLiteral("execute"), startNs, LiveEvents::nowNs(), result.ok ? 1 : 0, resultFields(result), job);
        }
    }

    // Wraps the callback so the time it waited for its thread is measured
    // and the job span ends when it runs.
    CoreRuntime::WriteCallback deliver(CoreRuntime::WriteCallback done) const
    {
        if (!job) {
            return done;
        }
        const quint64 id = job;
        return [id, done = std::move(done), postedNs = LiveEvents::nowNs()](const CoreResult &result) {
            LiveEvents::span(QStringLiteral("db"), QStringLiteral("callback-delay"), postedNs, LiveEvents::nowNs(), -1, {}, id);
            LiveEvents::end(id, result.ok ? 1 : 0, resultFields(result));
            LiveEvents::ParentScope parent(id);
            if (done) {
                done(result);
            }
        };
    }

    void finishedWithoutCallback(const CoreResult &result) const
    {
        LiveEvents::end(job, result.ok ? 1 : 0, resultFields(result));
    }
};
} // namespace

class CoreWriteWorker final : public QObject
{
public:
    explicit CoreWriteWorker(QString databasePath)
        : m_databasePath(std::move(databasePath))
    {
    }

    void run(
        const CoreRuntime::WriteJob &job,
        const QPointer<QObject> &context,
        const CoreRuntime::WriteCallback &done,
        const DbTrace &trace
    )
    {
        CoreResult result;
        const qint64 startNs = trace.started();

        try {
            if (!m_database.isOpen()) {
                m_database.open(m_databasePath);
            }

            result.value = m_database.transaction([&]() { return job(m_database); });
            result.ok = true;
        } catch (const CoreError &error) {
            result.errorMessage = error.message();
            result.errorDetails = error.details();
        } catch (const std::exception &error) {
            result.errorMessage = QString::fromUtf8(error.what());
        }
        trace.executed(startNs, result);

        if (!done || !context) {
            trace.finishedWithoutCallback(result);
            return;
        }

        QMetaObject::invokeMethod(
            context.data(),
            [callback = trace.deliver(done), result]() { callback(result); },
            Qt::QueuedConnection
        );
    }

private:
    QString m_databasePath;
    CoreDatabase m_database;
};

namespace
{
CoreRuntime *activeRuntime = nullptr;


CoreResult runCaught(const std::function<QVariant()> &work)
{
    CoreResult result;

    try {
        result.value = work();
        result.ok = true;
    } catch (const CoreError &error) {
        result.errorMessage = error.message();
        result.errorDetails = error.details();
    } catch (const std::exception &error) {
        result.errorMessage = QString::fromUtf8(error.what());
    }

    return result;
}

// One connection per pool thread, reopened if the database path changes.
CoreDatabase &taskConnection(const QString &path)
{
    thread_local std::unique_ptr<CoreDatabase> connection;

    if (!connection || !connection->isOpen() || connection->path() != path) {
        connection = std::make_unique<CoreDatabase>();
        connection->open(path);
    }

    return *connection;
}
}

CoreRuntime *CoreRuntime::active()
{
    return activeRuntime && activeRuntime->ready() ? activeRuntime : nullptr;
}

CoreRuntime::CoreRuntime(QObject *parent)
    : QObject(parent)
{
    activeRuntime = this;
    m_writerThread.setObjectName(QStringLiteral("ArchivistCoreWriter"));
    m_taskPool.setMaxThreadCount(4);
    m_taskPool.setExpiryTimeout(30000);
}

CoreRuntime::~CoreRuntime()
{
    stop();

    if (activeRuntime == this) {
        activeRuntime = nullptr;
    }
}

bool CoreRuntime::start(const QString &databasePath)
{
    stop();

    try {
        m_reader.open(databasePath);
        m_reader.runMigrations();
        m_schemaVersion = m_reader.userVersion();
    } catch (const CoreError &error) {
        fail(error.message());
        return false;
    }

    m_writer = new CoreWriteWorker(m_reader.path());
    m_writer->moveToThread(&m_writerThread);
    connect(&m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
    m_writerThread.start();

    m_ready = true;
    m_errorMessage.clear();
    emit stateChanged();
    return true;
}

void CoreRuntime::stop()
{
    m_taskPool.waitForDone();

    if (m_writerThread.isRunning()) {
        m_writerThread.quit();
        m_writerThread.wait();
    }

    m_writer = nullptr;
    m_reader.close();

    if (m_ready) {
        m_ready = false;
        emit stateChanged();
    }
}

bool CoreRuntime::ready() const
{
    return m_ready;
}

QString CoreRuntime::databasePath() const
{
    return m_reader.path();
}

int CoreRuntime::schemaVersion() const
{
    return m_schemaVersion;
}

QString CoreRuntime::errorMessage() const
{
    return m_errorMessage;
}

CoreDatabase &CoreRuntime::reader()
{
    return m_reader;
}

void CoreRuntime::write(WriteJob job, QObject *context, WriteCallback done, const char *operation)
{
    if (!m_ready || !m_writer) {
        if (done && context) {
            QMetaObject::invokeMethod(
                context,
                [done]() {
                    done({false, {}, QStringLiteral("The Archivist core is not running.")});
                },
                Qt::QueuedConnection
            );
        }
        return;
    }

    QPointer<QObject> guardedContext(context);
    CoreWriteWorker *writer = m_writer;
    const DbTrace trace = DbTrace::queued("write", operation, context);

    QMetaObject::invokeMethod(
        writer,
        [writer, job = std::move(job), guardedContext, done = std::move(done), trace]() {
            writer->run(job, guardedContext, done, trace);
        },
        Qt::QueuedConnection
    );
}

void CoreRuntime::read(const ReadJob &job, QObject *context, WriteCallback done, const char *operation)
{
    CoreResult result;
    // Synchronous on the calling thread (the UI thread for stores): its
    // execute span is on that thread, which is what makes it blocking.
    const DbTrace trace = DbTrace::queued("read", operation, context);
    const qint64 startNs = trace.job ? LiveEvents::nowNs() : -1;

    if (!m_ready) {
        result.errorMessage = QStringLiteral("The Archivist core is not running.");
    } else {
        try {
            result.value = job(m_reader);
            result.ok = true;
        } catch (const CoreError &error) {
            result.errorMessage = error.message();
            result.errorDetails = error.details();
        } catch (const std::exception &error) {
            result.errorMessage = QString::fromUtf8(error.what());
        }
    }
    trace.executed(startNs, result);

    if (!done || !context) {
        trace.finishedWithoutCallback(result);
        return;
    }

    QMetaObject::invokeMethod(
        context,
        [callback = trace.deliver(std::move(done)), result]() { callback(result); },
        Qt::QueuedConnection
    );
}

void CoreRuntime::run(TaskJob job, QObject *context, WriteCallback done, const char *operation)
{
    if (!m_ready) {
        if (done && context) {
            QMetaObject::invokeMethod(
                context,
                [done]() { done({false, {}, QStringLiteral("The Archivist core is not running.")}); },
                Qt::QueuedConnection
            );
        }
        return;
    }

    QPointer<QObject> guardedContext(context);
    const QString path = m_reader.path();
    const DbTrace trace = DbTrace::queued("run", operation, context);

    m_taskPool.start([path, job = std::move(job), guardedContext, done = std::move(done), trace]() {
        const qint64 startNs = trace.started();
        const CoreResult result = runCaught([&]() { return job(taskConnection(path)); });
        trace.executed(startNs, result);

        if (!done || !guardedContext) {
            trace.finishedWithoutCallback(result);
            return;
        }

        QMetaObject::invokeMethod(
            guardedContext.data(),
            [callback = trace.deliver(done), result]() { callback(result); },
            Qt::QueuedConnection
        );
    });
}

QString CoreRuntime::defaultDatabasePath()
{
    const QString configured = qEnvironmentVariable("ARCHIVIST_DB_PATH").trimmed();

    if (!configured.isEmpty()) {
        return configured;
    }

    // A test profile never falls back to the repository's database.
    if (AppProfile::active()) {
        return QDir(AppProfile::directory()).filePath(QStringLiteral("archivist.db"));
    }

#ifdef ARCHIVIST_REPOSITORY_ROOT
    const QString repositoryDatabase = QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT))
        .filePath(QStringLiteral("backend/data/archivist.db"));

    if (QFileInfo::exists(repositoryDatabase)) {
        return repositoryDatabase;
    }
#endif

    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("archivist.db"));
}

void CoreRuntime::fail(const QString &message)
{
    m_reader.close();
    m_ready = false;
    m_errorMessage = message;
    emit stateChanged();
}
