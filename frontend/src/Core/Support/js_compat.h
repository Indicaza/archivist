#pragma once

#include <QChar>
#include <QDateTime>
#include <QString>
#include <QStringView>

// Helpers that reproduce JavaScript string and date behaviour exactly, for
// ports where the result is stored or hashed and must match what the Node
// backend produced (chunk ids, timestamps, trimmed text).
namespace JsCompat
{
// JavaScript's whitespace set for String.prototype.trim and \s.
inline bool isSpace(QChar character)
{
    switch (character.unicode()) {
    case 0x0009:
    case 0x000A:
    case 0x000B:
    case 0x000C:
    case 0x000D:
    case 0x0020:
    case 0x00A0:
    case 0x1680:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
    case 0xFEFF:
        return true;
    default:
        return character.unicode() >= 0x2000 && character.unicode() <= 0x200A;
    }
}

inline QStringView trimmedView(QStringView value)
{
    qsizetype start = 0;
    qsizetype end = value.size();

    while (start < end && isSpace(value.at(start))) {
        ++start;
    }

    while (end > start && isSpace(value.at(end - 1))) {
        --end;
    }

    return value.mid(start, end - start);
}

inline QString trimmed(QStringView value)
{
    return trimmedView(value).toString();
}

inline bool isBlank(QStringView value)
{
    return trimmedView(value).isEmpty();
}

// Date.prototype.toISOString(): UTC with milliseconds, e.g. 2026-10-22T17:03:12.123Z.
inline QString isoTimestamp(const QDateTime &value)
{
    return value.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}
}
