#include "core_database.h"

#include "../core_error.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <sqlite3.h>

namespace
{
struct MigrationEntry
{
    int version = 0;
    QString file;
    bool foreignKeysDisabled = false;
    bool checkForeignKeys = false;
};

QList<MigrationEntry> readManifest(const QString &directory)
{
    QFile manifestFile(QDir(directory).filePath(QStringLiteral("migrations.json")));

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        throw CoreError(
            QStringLiteral("Missing migration manifest in %1").arg(directory)
        );
    }

    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QList<MigrationEntry> entries;

    for (const QJsonValue &value : manifest.value(QStringLiteral("migrations")).toArray()) {
        const QJsonObject object = value.toObject();
        entries.append({
            object.value(QStringLiteral("version")).toInt(),
            object.value(QStringLiteral("file")).toString(),
            object.value(QStringLiteral("foreignKeysDisabled")).toBool(),
            object.value(QStringLiteral("checkForeignKeys")).toBool(),
        });
    }

    std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
        return left.version < right.version;
    });

    return entries;
}
}

CoreDatabase::~CoreDatabase()
{
    close();
}

void CoreDatabase::open(const QString &path)
{
    close();

    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    const QByteArray utf8Path = info.absoluteFilePath().toUtf8();
    const int resultCode = sqlite3_open_v2(
        utf8Path.constData(),
        &m_database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr
    );

    if (resultCode != SQLITE_OK) {
        const QString message = m_database
            ? QString::fromUtf8(sqlite3_errmsg(m_database))
            : QStringLiteral("out of memory");
        close();
        throw CoreError(QStringLiteral("Could not open %1: %2").arg(path, message));
    }

    m_path = info.absoluteFilePath();
    sqlite3_busy_timeout(m_database, 5000);
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
}

void CoreDatabase::close()
{
    if (m_database) {
        sqlite3_close_v2(m_database);
        m_database = nullptr;
    }

    m_path.clear();
}

bool CoreDatabase::isOpen() const
{
    return m_database != nullptr;
}

const QString &CoreDatabase::path() const
{
    return m_path;
}

sqlite3 *CoreDatabase::handle() const
{
    return m_database;
}

void CoreDatabase::exec(const QString &sql)
{
    if (!m_database) {
        throw CoreError(QStringLiteral("The database is not open."));
    }

    char *error = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int resultCode = sqlite3_exec(m_database, utf8.constData(), nullptr, nullptr, &error);

    if (resultCode != SQLITE_OK) {
        const QString message = error
            ? QString::fromUtf8(error)
            : QString::fromUtf8(sqlite3_errmsg(m_database));
        sqlite3_free(error);
        throw CoreError(
            QStringLiteral("SQLite exec failed (%1): %2 · %3")
                .arg(resultCode)
                .arg(message, sql.simplified().left(160))
        );
    }
}

SqliteStatement CoreDatabase::prepare(const QString &sql)
{
    if (!m_database) {
        throw CoreError(QStringLiteral("The database is not open."));
    }

    return SqliteStatement(m_database, sql);
}

int CoreDatabase::changes() const
{
    return m_database ? sqlite3_changes(m_database) : 0;
}

bool CoreDatabase::inTransaction() const
{
    return m_database && sqlite3_get_autocommit(m_database) == 0;
}

int CoreDatabase::userVersion()
{
    return prepare(QStringLiteral("PRAGMA user_version")).scalar().toInt();
}

bool CoreDatabase::hasCompileOption(const QString &option)
{
    return prepare(QStringLiteral("SELECT sqlite_compileoption_used(?)"))
        .bind(1, option)
        .scalar()
        .toInt() == 1;
}

bool CoreDatabase::tableExists(const QString &name)
{
    return prepare(QStringLiteral(
               "SELECT 1 FROM sqlite_master WHERE type IN ('table', 'view') AND name = ? LIMIT 1"
           ))
        .bind(1, name)
        .one()
        .has_value();
}

int CoreDatabase::latestBundledVersion(const QString &directory)
{
    const QList<MigrationEntry> entries = readManifest(directory);
    return entries.isEmpty() ? 0 : entries.last().version;
}

int CoreDatabase::runMigrations(const QString &directory)
{
    const int currentVersion = userVersion();
    const QList<MigrationEntry> entries = readManifest(directory);
    int applied = 0;

    for (const MigrationEntry &entry : entries) {
        if (entry.version <= currentVersion) {
            continue;
        }

        QFile sqlFile(QDir(directory).filePath(entry.file));

        if (!sqlFile.open(QIODevice::ReadOnly)) {
            throw CoreError(QStringLiteral("Missing migration %1").arg(entry.file));
        }

        const QString sql = QString::fromUtf8(sqlFile.readAll());
        const bool foreignKeysWereEnabled =
            prepare(QStringLiteral("PRAGMA foreign_keys")).scalar().toInt() == 1;
        const bool disableForeignKeys = entry.foreignKeysDisabled && foreignKeysWereEnabled;

        // PRAGMA foreign_keys is a no-op inside a transaction, so it is
        // switched around the migration like the Node runner does.
        if (disableForeignKeys) {
            exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
        }

        try {
            transaction([&]() {
                exec(sql);

                if (entry.checkForeignKeys) {
                    const QList<QVariantMap> violations =
                        prepare(QStringLiteral("PRAGMA foreign_key_check")).all();

                    if (!violations.isEmpty()) {
                        throw CoreError(
                            QStringLiteral("Migration %1 produced %2 foreign-key violation(s).")
                                .arg(entry.version)
                                .arg(violations.size())
                        );
                    }
                }

                exec(QStringLiteral("PRAGMA user_version = %1").arg(entry.version));
            });
        } catch (...) {
            if (disableForeignKeys) {
                exec(QStringLiteral("PRAGMA foreign_keys = ON"));
            }

            throw;
        }

        if (disableForeignKeys) {
            exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        }

        ++applied;
    }

    return applied;
}

void CoreDatabase::rollbackQuietly()
{
    if (m_database && !sqlite3_get_autocommit(m_database)) {
        sqlite3_exec(m_database, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}
