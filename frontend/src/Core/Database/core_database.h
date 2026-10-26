#pragma once

#include "sqlite_statement.h"

#include <QString>
#include <QStringList>
#include <type_traits>
#include <utility>

struct sqlite3;

// One SQLite connection to archivist.db with the same settings the Node
// backend uses (WAL, foreign keys, 5s busy timeout). Not thread-safe: each
// thread owns its own CoreDatabase.
class CoreDatabase final
{
public:
    static constexpr const char *migrationResourceDirectory = ":/archivist/core/migrations";

    CoreDatabase() = default;
    ~CoreDatabase();

    CoreDatabase(const CoreDatabase &) = delete;
    CoreDatabase &operator=(const CoreDatabase &) = delete;

    void open(const QString &path);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const QString &path() const;
    [[nodiscard]] sqlite3 *handle() const;

    void exec(const QString &sql);
    [[nodiscard]] SqliteStatement prepare(const QString &sql);

    [[nodiscard]] int userVersion();
    // Rows changed by the most recent INSERT, UPDATE or DELETE.
    [[nodiscard]] int changes() const;
    // True while a transaction is open on this connection.
    [[nodiscard]] bool inTransaction() const;
    [[nodiscard]] bool hasCompileOption(const QString &option);
    [[nodiscard]] bool tableExists(const QString &name);

    // Applies every bundled migration newer than PRAGMA user_version and
    // returns how many ran. Each migration runs in its own transaction.
    int runMigrations(const QString &directory = QString::fromLatin1(migrationResourceDirectory));
    [[nodiscard]] static int latestBundledVersion(
        const QString &directory = QString::fromLatin1(migrationResourceDirectory)
    );

    // Runs `work` inside BEGIN IMMEDIATE ... COMMIT and rolls back on throw.
    template<typename Work>
    auto transaction(Work &&work) -> decltype(work())
    {
        exec(QStringLiteral("BEGIN IMMEDIATE"));

        try {
            if constexpr (std::is_void_v<decltype(work())>) {
                std::forward<Work>(work)();
                exec(QStringLiteral("COMMIT"));
            } else {
                auto result = std::forward<Work>(work)();
                exec(QStringLiteral("COMMIT"));
                return result;
            }
        } catch (...) {
            rollbackQuietly();
            throw;
        }
    }

private:
    void rollbackQuietly();

    sqlite3 *m_database = nullptr;
    QString m_path;
};
