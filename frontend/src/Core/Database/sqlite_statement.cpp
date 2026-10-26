#include "sqlite_statement.h"

#include "../core_error.h"

#include <sqlite3.h>
#include <utility>

SqliteStatement::SqliteStatement(sqlite3 *database, const QString &sql)
    : m_database(database)
    , m_sql(sql)
{
    const QByteArray utf8 = sql.toUtf8();
    const int resultCode = sqlite3_prepare_v2(
        m_database,
        utf8.constData(),
        static_cast<int>(utf8.size()),
        &m_statement,
        nullptr
    );

    check(resultCode, "prepare");
}

SqliteStatement::~SqliteStatement()
{
    if (m_statement) {
        sqlite3_finalize(m_statement);
    }
}

SqliteStatement::SqliteStatement(SqliteStatement &&other) noexcept
    : m_database(std::exchange(other.m_database, nullptr))
    , m_statement(std::exchange(other.m_statement, nullptr))
    , m_sql(std::move(other.m_sql))
{
}

SqliteStatement &SqliteStatement::operator=(SqliteStatement &&other) noexcept
{
    if (this != &other) {
        if (m_statement) {
            sqlite3_finalize(m_statement);
        }

        m_database = std::exchange(other.m_database, nullptr);
        m_statement = std::exchange(other.m_statement, nullptr);
        m_sql = std::move(other.m_sql);
    }

    return *this;
}

void SqliteStatement::check(int resultCode, const char *action) const
{
    if (resultCode == SQLITE_OK || resultCode == SQLITE_ROW || resultCode == SQLITE_DONE) {
        return;
    }

    throw CoreError(
        QStringLiteral("SQLite %1 failed (%2): %3 · %4")
            .arg(QString::fromLatin1(action))
            .arg(resultCode)
            .arg(QString::fromUtf8(sqlite3_errmsg(m_database)))
            .arg(m_sql.simplified().left(160))
    );
}

SqliteStatement &SqliteStatement::bind(int index, const QVariant &value)
{
    int resultCode = SQLITE_OK;

    if (!value.isValid() || value.isNull()) {
        resultCode = sqlite3_bind_null(m_statement, index);
    } else {
        switch (value.typeId()) {
        case QMetaType::Bool:
            resultCode = sqlite3_bind_int(m_statement, index, value.toBool() ? 1 : 0);
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::LongLong:
        case QMetaType::ULong:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
            resultCode = sqlite3_bind_int64(m_statement, index, value.toLongLong());
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            resultCode = sqlite3_bind_double(m_statement, index, value.toDouble());
            break;
        case QMetaType::QByteArray: {
            const QByteArray bytes = value.toByteArray();
            resultCode = sqlite3_bind_blob64(
                m_statement,
                index,
                bytes.constData(),
                static_cast<sqlite3_uint64>(bytes.size()),
                SQLITE_TRANSIENT
            );
            break;
        }
        default: {
            const QByteArray text = value.toString().toUtf8();
            resultCode = sqlite3_bind_text64(
                m_statement,
                index,
                text.constData(),
                static_cast<sqlite3_uint64>(text.size()),
                SQLITE_TRANSIENT,
                SQLITE_UTF8
            );
            break;
        }
        }
    }

    check(resultCode, "bind");
    return *this;
}

SqliteStatement &SqliteStatement::bind(const QString &name, const QVariant &value)
{
    const QByteArray key = name.toUtf8();
    int index = sqlite3_bind_parameter_index(m_statement, key.constData());

    if (index == 0 && !name.isEmpty() && name.front() != QLatin1Char(':')
        && name.front() != QLatin1Char('@') && name.front() != QLatin1Char('$')) {
        index = sqlite3_bind_parameter_index(
            m_statement,
            QByteArray(":" + key).constData()
        );
    }

    if (index == 0) {
        throw CoreError(QStringLiteral("Unknown SQL parameter %1").arg(name));
    }

    return bind(index, value);
}

SqliteStatement &SqliteStatement::bindAll(const QVariantList &values)
{
    for (qsizetype index = 0; index < values.size(); ++index) {
        bind(static_cast<int>(index + 1), values.at(index));
    }

    return *this;
}

bool SqliteStatement::step()
{
    const int resultCode = sqlite3_step(m_statement);
    check(resultCode, "step");
    return resultCode == SQLITE_ROW;
}

void SqliteStatement::reset()
{
    sqlite3_reset(m_statement);
}

int SqliteStatement::columnCount() const
{
    return sqlite3_column_count(m_statement);
}

QString SqliteStatement::columnName(int column) const
{
    return QString::fromUtf8(sqlite3_column_name(m_statement, column));
}

QVariant SqliteStatement::value(int column) const
{
    switch (sqlite3_column_type(m_statement, column)) {
    case SQLITE_INTEGER:
        return QVariant::fromValue<qint64>(sqlite3_column_int64(m_statement, column));
    case SQLITE_FLOAT:
        return sqlite3_column_double(m_statement, column);
    case SQLITE_TEXT:
        return QString::fromUtf8(
            reinterpret_cast<const char *>(sqlite3_column_text(m_statement, column)),
            sqlite3_column_bytes(m_statement, column)
        );
    case SQLITE_BLOB:
        return QByteArray(
            static_cast<const char *>(sqlite3_column_blob(m_statement, column)),
            sqlite3_column_bytes(m_statement, column)
        );
    default:
        return {};
    }
}

QVariantMap SqliteStatement::row() const
{
    QVariantMap result;
    const int count = columnCount();

    for (int column = 0; column < count; ++column) {
        result.insert(columnName(column), value(column));
    }

    return result;
}

void SqliteStatement::run()
{
    reset();

    while (step()) {
    }

    reset();
}

QList<QVariantMap> SqliteStatement::all()
{
    reset();
    QList<QVariantMap> rows;

    while (step()) {
        rows.append(row());
    }

    reset();
    return rows;
}

std::optional<QVariantMap> SqliteStatement::one()
{
    reset();
    std::optional<QVariantMap> result;

    if (step()) {
        result = row();
    }

    reset();
    return result;
}

QVariant SqliteStatement::scalar()
{
    reset();
    QVariant result;

    if (step() && columnCount() > 0) {
        result = value(0);
    }

    reset();
    return result;
}
