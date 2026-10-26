#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>

// JSON helpers for values stored as *_json columns or sent to providers.
// Shapes match JSON.stringify / JSON.parse in the Node backend.
namespace JsonValues
{
inline QByteArray toJson(const QVariant &value)
{
    const QJsonValue json = QJsonValue::fromVariant(value);

    if (json.isObject()) {
        return QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact);
    }

    if (json.isArray()) {
        return QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact);
    }

    // Scalars: wrap in an array and strip the brackets.
    QByteArray wrapped = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2);
}

inline QString toJsonString(const QVariant &value)
{
    return QString::fromUtf8(toJson(value));
}

// JSON.parse; returns an invalid QVariant when the text is not JSON.
inline QVariant parse(const QString &text, bool *ok = nullptr)
{
    const QByteArray wrapped = QByteArrayLiteral("[") + text.toUtf8() + QByteArrayLiteral("]");
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(wrapped, &error);
    const bool parsed = error.error == QJsonParseError::NoError && document.isArray() && document.array().size() == 1;

    if (ok) {
        *ok = parsed;
    }

    return parsed ? document.array().at(0).toVariant() : QVariant();
}

// Parses a stored payload, returning {} for anything that is not an object.
inline QVariantMap parseObject(const QString &text)
{
    const QVariant value = parse(text);
    return value.typeId() == QMetaType::QVariantMap ? value.toMap() : QVariantMap();
}

inline QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// new Date(value).getTime() for ISO timestamps; NaN-like failures return nullopt-ish via ok.
inline qint64 isoMilliseconds(const QString &value, bool *ok = nullptr)
{
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);

    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }

    if (ok) {
        *ok = parsed.isValid();
    }

    return parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
}
}
