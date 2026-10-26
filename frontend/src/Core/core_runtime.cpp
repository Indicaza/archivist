#include "core_runtime.h"

#include "core_error.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QtGlobal>
#include <memory>

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
        const CoreRuntime::WriteCallback &done
    )
    {
        CoreResult result;

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

        if (!done || !context) {
            return;
        }

        QMetaObject::invokeMethod(
            context.data(),
            [done, result]() { done(result); },
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

void CoreRuntime::write(WriteJob job, QObject *context, WriteCallback done)
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

    QMetaObject::invokeMethod(
        writer,
        [writer, job = std::move(job), guardedContext, done = std::move(done)]() {
            writer->run(job, guardedContext, done);
        },
        Qt::QueuedConnection
    );
}

void CoreRuntime::read(const ReadJob &job, QObject *context, WriteCallback done)
{
    CoreResult result;

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

    if (!done || !context) {
        return;
    }

    QMetaObject::invokeMethod(
        context,
        [done = std::move(done), result]() { done(result); },
        Qt::QueuedConnection
    );
}

void CoreRuntime::run(TaskJob job, QObject *context, WriteCallback done)
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

    m_taskPool.start([path, job = std::move(job), guardedContext, done = std::move(done)]() {
        const CoreResult result = runCaught([&]() { return job(taskConnection(path)); });

        if (!done || !guardedContext) {
            return;
        }

        QMetaObject::invokeMethod(
            guardedContext.data(),
            [done, result]() { done(result); },
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
