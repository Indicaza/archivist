#include "ai_tools_input.h"
#include "ai_tools_read.h"

#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"

#include <cmath>

namespace AITools::Input
{
void invalid(const QString &path, const QString &message)
{
    throw InvalidInput{{
        {QStringLiteral("code"), QStringLiteral("custom")},
        {QStringLiteral("path"), path.isEmpty() ? QVariantList() : QVariantList{path}},
        {QStringLiteral("message"), message},
    }};
}

QVariantMap strictObject(const QVariant &input, const QStringList &keys)
{
    if (input.typeId() != QMetaType::QVariantMap) {
        invalid({}, QStringLiteral("Expected object."));
    }

    const QVariantMap map = input.toMap();

    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
        if (!keys.contains(iterator.key())) {
            invalid(iterator.key(), QStringLiteral("Unrecognized key."));
        }
    }

    return map;
}

QString stringField(const QVariantMap &map, const QString &key, int minimum, int maximum, bool trim, std::optional<QString> fallback)
{
    const QVariant value = map.value(key);

    if (!value.isValid() && fallback) {
        return *fallback;
    }

    if (value.typeId() != QMetaType::QString) {
        invalid(key, QStringLiteral("Expected string."));
    }

    const QString text = trim ? JsCompat::trimmed(value.toString()) : value.toString();

    if (text.size() < minimum || text.size() > maximum) {
        invalid(key, QStringLiteral("String length is out of range."));
    }

    return text;
}

int intField(const QVariantMap &map, const QString &key, qint64 minimum, qint64 maximum, std::optional<int> fallback)
{
    const QVariant value = map.value(key);

    if (!value.isValid() && fallback) {
        return *fallback;
    }

    if (!ReadTools::isNumber(value)) {
        invalid(key, QStringLiteral("Expected number."));
    }

    const double number = value.toDouble();

    if (!std::isfinite(number) || std::floor(number) != number) {
        invalid(key, QStringLiteral("Expected integer."));
    }

    if (number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        invalid(key, QStringLiteral("Number is out of range."));
    }

    return static_cast<int>(number);
}

QString enumField(const QVariantMap &map, const QString &key, const QStringList &values)
{
    const QString value = stringField(map, key, 1, 64);
    if (!values.contains(value)) {
        invalid(key, QStringLiteral("Expected one of: %1.").arg(values.join(QStringLiteral(", "))));
    }
    return value;
}

bool boolField(const QVariantMap &map, const QString &key)
{
    const QVariant value = map.value(key);
    if (value.typeId() != QMetaType::Bool) {
        invalid(key, QStringLiteral("Expected boolean."));
    }
    return value.toBool();
}

QVariantMap objectSchema(const QVariantMap &properties, const QStringList &required)
{
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), CoreValues::toVariantList(required)},
        {QStringLiteral("additionalProperties"), false},
    };
}

QVariantMap stringSchema(int minimum, int maximum, const QString &description)
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("string")}};

    if (minimum > 0) {
        schema.insert(QStringLiteral("minLength"), minimum);
    }

    if (maximum > 0) {
        schema.insert(QStringLiteral("maxLength"), maximum);
    }

    if (!description.isEmpty()) {
        schema.insert(QStringLiteral("description"), description);
    }

    return schema;
}

QVariantMap integerSchema(int minimum, int maximum)
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), minimum}};

    if (maximum > 0) {
        schema.insert(QStringLiteral("maximum"), maximum);
    }

    return schema;
}

QVariantMap enumSchema(const QStringList &values, const QString &description)
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), CoreValues::toVariantList(values)}};
    if (!description.isEmpty()) {
        schema.insert(QStringLiteral("description"), description);
    }
    return schema;
}

QVariantMap pick(const QVariantMap &map, const QStringList &keys)
{
    QVariantMap result;

    for (const QString &key : keys) {
        result.insert(key, map.value(key));
    }

    return result;
}
} // namespace AITools::Input
