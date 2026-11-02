#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <optional>

// Input validation (the zod schemas of the Node tools) and JSON schema
// builders shared by the AI tool families. Internal to Core/Domains/AI.
namespace AITools::Input
{
// Thrown by the parsers; the executor turns it into an invalid_input
// ToolError carrying the issue.
struct InvalidInput
{
    QVariantMap issue;
};

// ---- parsing ----
[[noreturn]] void invalid(const QString &path, const QString &message);
[[nodiscard]] QVariantMap strictObject(const QVariant &input, const QStringList &keys);
[[nodiscard]] QString stringField(
    const QVariantMap &map,
    const QString &key,
    int minimum,
    int maximum,
    bool trim = false,
    std::optional<QString> fallback = std::nullopt
);
[[nodiscard]] int intField(const QVariantMap &map, const QString &key, qint64 minimum, qint64 maximum, std::optional<int> fallback = std::nullopt);
[[nodiscard]] QString enumField(const QVariantMap &map, const QString &key, const QStringList &values);
[[nodiscard]] bool boolField(const QVariantMap &map, const QString &key);

// ---- provider-facing JSON schemas ----
[[nodiscard]] QVariantMap objectSchema(const QVariantMap &properties, const QStringList &required);
[[nodiscard]] QVariantMap stringSchema(int minimum, int maximum, const QString &description = {});
[[nodiscard]] QVariantMap integerSchema(int minimum, int maximum = 0);
[[nodiscard]] QVariantMap enumSchema(const QStringList &values, const QString &description = {});

// ---- summaries ----
// The listed keys of map (missing keys become invalid QVariants, as before).
[[nodiscard]] QVariantMap pick(const QVariantMap &map, const QStringList &keys);
} // namespace AITools::Input
