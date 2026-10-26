#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QVariantList>

// Small helpers shared by the core domains. Results are shaped like the JSON
// the Node API returned (camelCase keys, null for SQL NULL) so the C++ stores
// and QML see the same data either way.
namespace CoreValues
{
// SQL NULL becomes a QVariant holding nullptr, which QML reads as `null`
// exactly like a JSON null did.
inline QVariant nullable(const QVariant &value)
{
    return value.isNull() || !value.isValid() ? QVariant::fromValue(nullptr) : value;
}

inline QVariant nullableString(const QString &value)
{
    return value.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(value);
}

inline bool isNullish(const QVariant &value)
{
    return !value.isValid() || value.isNull()
        || value.typeId() == QMetaType::Nullptr
        || (value.typeId() == QMetaType::QString && value.toString().isEmpty());
}

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline bool isUuid(const QString &value)
{
    return value.size() == 36 && !QUuid::fromString(value).isNull();
}

inline QString placeholders(qsizetype count)
{
    QStringList marks;
    marks.reserve(count);

    for (qsizetype index = 0; index < count; ++index) {
        marks.append(QStringLiteral("?"));
    }

    return marks.join(QStringLiteral(", "));
}

inline QStringList uniqueIds(const QStringList &ids)
{
    QStringList result;
    QSet<QString> seen;

    for (const QString &id : ids) {
        if (!seen.contains(id)) {
            seen.insert(id);
            result.append(id);
        }
    }

    return result;
}

inline QStringList toStringList(const QVariant &value)
{
    QStringList result;

    for (const QVariant &item : value.toList()) {
        result.append(item.toString());
    }

    return result;
}

inline QVariantList toVariantList(const QStringList &values)
{
    QVariantList result;
    result.reserve(values.size());

    for (const QString &value : values) {
        result.append(value);
    }

    return result;
}
}
