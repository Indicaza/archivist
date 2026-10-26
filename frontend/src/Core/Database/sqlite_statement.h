#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <optional>

struct sqlite3;
struct sqlite3_stmt;

// Prepared statement with RAII cleanup. Parameters are 1-based like SQLite.
// Every failure throws CoreError with SQLite's message.
class SqliteStatement final
{
public:
    SqliteStatement(sqlite3 *database, const QString &sql);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement &) = delete;
    SqliteStatement &operator=(const SqliteStatement &) = delete;
    SqliteStatement(SqliteStatement &&other) noexcept;
    SqliteStatement &operator=(SqliteStatement &&other) noexcept;

    SqliteStatement &bind(int index, const QVariant &value);
    SqliteStatement &bind(const QString &name, const QVariant &value);
    SqliteStatement &bindAll(const QVariantList &values);

    // Advances one row. Returns false once the statement is done.
    bool step();
    void reset();

    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QString columnName(int column) const;
    [[nodiscard]] QVariant value(int column) const;
    [[nodiscard]] QVariantMap row() const;

    // Convenience runners. Each resets the statement first.
    void run();
    [[nodiscard]] QList<QVariantMap> all();
    [[nodiscard]] std::optional<QVariantMap> one();
    [[nodiscard]] QVariant scalar();

private:
    void check(int resultCode, const char *action) const;

    sqlite3 *m_database = nullptr;
    sqlite3_stmt *m_statement = nullptr;
    QString m_sql;
};
