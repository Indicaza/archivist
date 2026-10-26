#include "agent_schemas.h"

#include <QStringList>
#include <QVariantList>
#include <cmath>
#include <functional>

namespace AgentSchemas
{
bool isNull(const QVariant &value)
{
    return value.typeId() == QMetaType::Nullptr || (value.isValid() && value.isNull() && !isString(value));
}

bool isString(const QVariant &value)
{
    return value.typeId() == QMetaType::QString;
}

bool isNumber(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double:
        return std::isfinite(value.toDouble());
    default:
        return false;
    }
}
}

using namespace AgentSchemas;

namespace
{
// One field check: returns the normalized value or nullopt.
using FieldCheck = std::function<std::optional<QVariant>(const QVariant &)>;

struct Field
{
    QString key;
    FieldCheck check;
};

bool isMap(const QVariant &value)
{
    return value.typeId() == QMetaType::QVariantMap;
}

bool isList(const QVariant &value)
{
    return value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList;
}

std::optional<QVariant> trimmedString(const QVariant &value, int minimum, int maximum)
{
    if (!isString(value)) {
        return std::nullopt;
    }

    const QString trimmed = value.toString().trimmed();

    if (trimmed.size() < minimum || trimmed.size() > maximum) {
        return std::nullopt;
    }

    return QVariant(trimmed);
}

FieldCheck nullableTrimmedString(int maximum)
{
    return [maximum](const QVariant &value) -> std::optional<QVariant> {
        if (isNull(value)) {
            return QVariant::fromValue(nullptr);
        }

        return trimmedString(value, 0, maximum);
    };
}

FieldCheck requiredString(int minimum, int maximum)
{
    return [minimum, maximum](const QVariant &value) { return trimmedString(value, minimum, maximum); };
}

FieldCheck stringList()
{
    return [](const QVariant &value) -> std::optional<QVariant> {
        if (!isList(value)) {
            return std::nullopt;
        }

        const QVariantList items = value.toList();

        if (items.size() > 100) {
            return std::nullopt;
        }

        QVariantList normalized;

        for (const QVariant &item : items) {
            const auto text = trimmedString(item, 1, 500);

            if (!text) {
                return std::nullopt;
            }

            normalized.append(*text);
        }

        return QVariant(normalized);
    };
}

FieldCheck oneOf(const QStringList &options)
{
    return [options](const QVariant &value) -> std::optional<QVariant> {
        if (!isString(value) || !options.contains(value.toString())) {
            return std::nullopt;
        }

        return value;
    };
}

QVariant numberValue(double number)
{
    if (std::floor(number) == number && std::abs(number) < 9.0e15) {
        return QVariant(static_cast<qlonglong>(number));
    }

    return QVariant(number);
}

FieldCheck number(double minimum, double maximum, bool integer, bool nullable)
{
    return [=](const QVariant &value) -> std::optional<QVariant> {
        if (nullable && isNull(value)) {
            return QVariant::fromValue(nullptr);
        }

        if (!isNumber(value)) {
            return std::nullopt;
        }

        const double number = value.toDouble();

        if ((integer && std::floor(number) != number) || number < minimum || number > maximum) {
            return std::nullopt;
        }

        return numberValue(number);
    };
}

std::optional<QVariantMap> object(const QVariant &value, const QList<Field> &fields, bool partial)
{
    if (!isMap(value)) {
        return std::nullopt;
    }

    const QVariantMap input = value.toMap();
    QVariantMap output;

    for (const Field &field : fields) {
        const auto found = input.constFind(field.key);

        if (found == input.constEnd() || !found->isValid()) {
            if (partial) {
                continue;
            }

            return std::nullopt;
        }

        const auto checked = field.check(*found);

        if (!checked) {
            return std::nullopt;
        }

        output.insert(field.key, *checked);
    }

    return output;
}

const QList<Field> &identityFields()
{
    static const QList<Field> fields{
        {QStringLiteral("personality"), nullableTrimmedString(2000)},
        {QStringLiteral("temperament"), nullableTrimmedString(2000)},
        {QStringLiteral("voice"), nullableTrimmedString(2000)},
        {QStringLiteral("backstory"), nullableTrimmedString(10000)},
    };
    return fields;
}

const QList<Field> &professionFields()
{
    static const QList<Field> fields{
        {QStringLiteral("jobTitle"), nullableTrimmedString(500)},
        {QStringLiteral("mission"), nullableTrimmedString(10000)},
        {QStringLiteral("expertise"), stringList()},
        {QStringLiteral("responsibilities"), stringList()},
        {QStringLiteral("successCriteria"), stringList()},
        {QStringLiteral("limitations"), stringList()},
    };
    return fields;
}

const QList<Field> &outputContractFields()
{
    static const QList<Field> fields{
        {QStringLiteral("responseStyle"), nullableTrimmedString(5000)},
        {QStringLiteral("verbosity"),
         oneOf({QStringLiteral("concise"), QStringLiteral("balanced"), QStringLiteral("detailed")})},
        {QStringLiteral("formattingRules"), stringList()},
        {QStringLiteral("codeOutputPreferences"), stringList()},
        {QStringLiteral("citationRequirements"), nullableTrimmedString(5000)},
        {QStringLiteral("followUpBehavior"), nullableTrimmedString(5000)},
    };
    return fields;
}

const QList<Field> &generationFields()
{
    static const QList<Field> fields{
        {QStringLiteral("provider"), oneOf({QStringLiteral("openai"), QStringLiteral("local")})},
        {QStringLiteral("model"), requiredString(1, 200)},
        {QStringLiteral("temperature"), number(0, 2, false, true)},
        {QStringLiteral("maxOutputTokens"), number(1, 128000, true, true)},
        {QStringLiteral("topP"), number(0, 1, false, true)},
        {QStringLiteral("frequencyPenalty"), number(-2, 2, false, true)},
        {QStringLiteral("presencePenalty"), number(-2, 2, false, true)},
    };
    return fields;
}

// A present key whose value is null or a valid nested object.
using Normalizer = std::function<std::optional<QVariant>(const QVariant &)>;

Normalizer partialObject(std::optional<QVariantMap> (*schema)(const QVariant &, bool))
{
    return [schema](const QVariant &value) -> std::optional<QVariant> {
        const auto result = schema(value, true);
        return result ? std::optional<QVariant>(QVariant(*result)) : std::nullopt;
    };
}
}

namespace AgentSchemas
{
std::optional<QVariantMap> identity(const QVariant &value, bool partial)
{
    return object(value, identityFields(), partial);
}

std::optional<QVariantMap> profession(const QVariant &value, bool partial)
{
    return object(value, professionFields(), partial);
}

std::optional<QVariantMap> outputContract(const QVariant &value, bool partial)
{
    return object(value, outputContractFields(), partial);
}

std::optional<QVariantMap> generation(const QVariant &value, bool partial)
{
    return object(value, generationFields(), partial);
}

std::optional<QVariantMap> context(const QVariant &value)
{
    if (!isMap(value)) {
        return std::nullopt;
    }

    const QVariantMap input = value.toMap();
    const QVariant compilerValue = input.value(QStringLiteral("compiler"));
    const QVariant configValue = input.value(QStringLiteral("config"));

    if (!isMap(compilerValue) || !isMap(configValue)) {
        return std::nullopt;
    }

    const QVariantMap compiler = compilerValue.toMap();
    const auto id = trimmedString(compiler.value(QStringLiteral("id")), 1, 100);
    const auto version = number(1, 9.0e15, true, false)(compiler.value(QStringLiteral("version")));

    if (!id || !version) {
        return std::nullopt;
    }

    return QVariantMap{
        {QStringLiteral("compiler"), QVariantMap{{QStringLiteral("id"), *id}, {QStringLiteral("version"), *version}}},
        {QStringLiteral("config"), configValue.toMap()},
    };
}

std::optional<QVariantMap> agentInput(const QVariantMap &input)
{
    const QList<QPair<QString, Normalizer>> fields{
        {QStringLiteral("name"), requiredString(1, 120)},
        {QStringLiteral("description"), nullableTrimmedString(5000)},
        {QStringLiteral("identity"), partialObject(&AgentSchemas::identity)},
        {QStringLiteral("profession"), partialObject(&AgentSchemas::profession)},
        {QStringLiteral("doctrine"), nullableTrimmedString(20000)},
        {QStringLiteral("outputContract"), partialObject(&AgentSchemas::outputContract)},
        {QStringLiteral("systemInstructions"), nullableTrimmedString(50000)},
        {QStringLiteral("generation"), partialObject(&AgentSchemas::generation)},
        {QStringLiteral("context"),
         [](const QVariant &value) -> std::optional<QVariant> {
             const auto result = AgentSchemas::context(value);
             return result ? std::optional<QVariant>(QVariant(*result)) : std::nullopt;
         }},
    };

    QVariantMap output;

    for (const auto &[key, normalize] : fields) {
        const auto found = input.constFind(key);

        if (found == input.constEnd() || !found->isValid()) {
            continue;
        }

        const auto normalized = normalize(*found);

        if (!normalized) {
            return std::nullopt;
        }

        output.insert(key, *normalized);
    }

    return output;
}

std::optional<QVariantMap> duplicateInput(const QVariantMap &input)
{
    QVariantMap output;
    const auto found = input.constFind(QStringLiteral("name"));

    if (found != input.constEnd() && found->isValid()) {
        const auto name = trimmedString(*found, 1, 120);

        if (!name) {
            return std::nullopt;
        }

        output.insert(QStringLiteral("name"), *name);
    }

    return output;
}
}
