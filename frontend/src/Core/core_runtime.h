#pragma once

#include "Database/core_database.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QVariant>
#include <QVariantMap>
#include <functional>
#include <memory>

struct CoreResult
{
    bool ok = false;
    QVariant value;
    QString errorMessage;
    QVariantMap errorDetails;
};

class CoreWriteWorker;

// The in-process replacement for the Node API's data layer.
//
// - reader(): a connection owned by the UI thread for fast reads. WAL mode lets
//   it read while a write is in progress.
// - write(): runs a job on a dedicated writer thread with its own connection,
//   inside one IMMEDIATE transaction, then delivers the result on the caller's
//   thread. A job that throws is rolled back and reported as an error.
class CoreRuntime final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY stateChanged)
    Q_PROPERTY(int schemaVersion READ schemaVersion NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)

public:
    using WriteJob = std::function<QVariant(CoreDatabase &)>;
    using ReadJob = std::function<QVariant(CoreDatabase &)>;
    using WriteCallback = std::function<void(const CoreResult &)>;
    using TaskJob = std::function<QVariant(CoreDatabase &)>;

    // The running core, or nullptr when it failed to start. Stores use this
    // and fall back to the Node API while a domain is mid-migration.
    [[nodiscard]] static CoreRuntime *active();

    explicit CoreRuntime(QObject *parent = nullptr);
    ~CoreRuntime() override;

    // Opens the database, applies pending migrations and starts the writer.
    // Returns false and sets errorMessage on failure; the app keeps running.
    bool start(const QString &databasePath);
    void stop();

    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] int schemaVersion() const;
    [[nodiscard]] QString errorMessage() const;

    [[nodiscard]] CoreDatabase &reader();

    // `operation` (a string literal) names the job on Observatory traces;
    // without one the context's class name is used. Never parameters.
    void write(WriteJob job, QObject *context, WriteCallback done = {}, const char *operation = nullptr);

    // Runs `job` now on the UI-thread reader and delivers the result on the
    // next event-loop turn, matching the timing callers had with HTTP. This
    // blocks the calling thread for the whole query; traces flag it.
    void read(const ReadJob &job, QObject *context, WriteCallback done, const char *operation = nullptr);

    // Runs `job` on a background thread with that thread's own connection and
    // no surrounding transaction. For slow work (filesystem walks, file reads,
    // git) that must not block the UI or hold the write lock; the job opens
    // short transactions itself with CoreDatabase::transaction().
    void run(TaskJob job, QObject *context, WriteCallback done, const char *operation = nullptr);

    // Where archivist.db lives: ARCHIVIST_DB_PATH, then the repository's
    // backend/data/archivist.db during development, then the user's app data.
    [[nodiscard]] static QString defaultDatabasePath();

signals:
    void stateChanged();

private:
    void fail(const QString &message);

    CoreDatabase m_reader;
    QThread m_writerThread;
    QThreadPool m_taskPool;
    CoreWriteWorker *m_writer = nullptr;
    bool m_ready = false;
    int m_schemaVersion = 0;
    QString m_errorMessage;
};
