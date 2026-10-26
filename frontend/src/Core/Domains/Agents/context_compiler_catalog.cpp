#include "context_compiler_catalog.h"

#include "agent_schemas.h"
#include "Core/core_error.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <cmath>

namespace
{
struct IntegerField
{
    QString key;
    qint64 minimum;
    qint64 maximum;
    bool hasDefault = false;
};

struct EnumField
{
    QString key;
    QStringList options;
};

enum class Refinement
{
    ReserveBelowTotal,         // "Response token reserve must be smaller than total tokens."
    AutoBalancedBudgets,       // superRefine used by auto-balanced v1 and v2
    KeywordOrRecencyWeight,    // keyword-recency: weights cannot both be zero
};

struct CompilerSchema
{
    QString id;
    int version;
    QList<IntegerField> integers;
    QList<EnumField> enums;
    QList<Refinement> refinements;
};

const QList<CompilerSchema> &schemas()
{
    static const IntegerField totalTokens{QStringLiteral("totalTokens"), 1000, 200000};
    static const IntegerField reserve{QStringLiteral("responseTokenReserve"), 256, 32000};
    static const EnumField searchScope{
        QStringLiteral("searchScope"), {QStringLiteral("current-chat"), QStringLiteral("all-chats")}
    };

    static const QList<CompilerSchema> list{
        {QStringLiteral("recent-history"), 1, {totalTokens, reserve}, {}, {Refinement::ReserveBelowTotal}},
        {QStringLiteral("contiguous-history"), 1, {totalTokens, reserve}, {}, {Refinement::ReserveBelowTotal}},
        {QStringLiteral("turn-pairs"), 1, {totalTokens, reserve}, {}, {Refinement::ReserveBelowTotal}},
        {QStringLiteral("keyword-recency"),
         1,
         {
             totalTokens,
             reserve,
             {QStringLiteral("recentMessageFloor"), 0, 50},
             {QStringLiteral("candidateLimit"), 1, 500},
             {QStringLiteral("keywordWeight"), 0, 100},
             {QStringLiteral("recencyWeight"), 0, 100},
         },
         {},
         {Refinement::ReserveBelowTotal, Refinement::KeywordOrRecencyWeight}},
        {QStringLiteral("auto-balanced"),
         1,
         {
             totalTokens,
             reserve,
             {QStringLiteral("recentHistoryTokens"), 256, 160000},
             {QStringLiteral("retrievalTokens"), 0, 160000},
             {QStringLiteral("retrievalResultLimit"), 1, 50},
             {QStringLiteral("minimumSearchTermLength"), 2, 20},
         },
         {searchScope},
         {Refinement::AutoBalancedBudgets}},
        {QStringLiteral("auto-balanced"),
         2,
         {
             totalTokens,
             reserve,
             {QStringLiteral("recentHistoryTokens"), 256, 160000},
             {QStringLiteral("retrievalTokens"), 0, 160000},
             {QStringLiteral("retrievalResultLimit"), 1, 50},
             {QStringLiteral("maxEvidenceItems"), 1, 20, true},
             {QStringLiteral("minimumSearchTermLength"), 2, 20},
             {QStringLiteral("minimumTermMatches"), 1, 24, true},
         },
         {searchScope},
         {Refinement::AutoBalancedBudgets}},
    };

    return list;
}

const CompilerSchema *find(const QString &id, int version)
{
    for (const CompilerSchema &schema : schemas()) {
        if (schema.id == id && schema.version == version) {
            return &schema;
        }
    }

    return nullptr;
}

QString receivedType(const QVariant &value)
{
    if (!value.isValid()) {
        return QStringLiteral("undefined");
    }

    if (AgentSchemas::isNull(value)) {
        return QStringLiteral("null");
    }

    switch (value.typeId()) {
    case QMetaType::QString:
        return QStringLiteral("string");
    case QMetaType::Bool:
        return QStringLiteral("boolean");
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        return QStringLiteral("array");
    case QMetaType::QVariantMap:
        return QStringLiteral("object");
    default:
        return AgentSchemas::isNumber(value) ? QStringLiteral("number") : QStringLiteral("object");
    }
}

QString registryKey(const QString &id, int version)
{
    return QStringLiteral("%1:v%2").arg(id).arg(version);
}
}

namespace ContextCompilerCatalog
{
bool has(const QString &id, int version)
{
    return find(id, version) != nullptr;
}

void validateConfig(const QString &id, int version, const QVariantMap &config)
{
    const CompilerSchema *schema = find(id, version);

    if (!schema) {
        throw CoreError(QStringLiteral("Context Compiler \"%1\" is not registered.").arg(registryKey(id, version)));
    }

    QStringList issues;
    bool fatal = false;
    QHash<QString, double> numbers;

    auto addIssue = [&issues](const QString &path, const QString &message) {
        issues.append(QStringLiteral("%1: %2").arg(path, message));
    };

    for (const IntegerField &field : schema->integers) {
        const QVariant value = config.value(field.key);

        if (!value.isValid() && field.hasDefault) {
            continue;
        }

        if (!AgentSchemas::isNumber(value)) {
            addIssue(field.key, QStringLiteral("Invalid input: expected number, received %1").arg(receivedType(value)));
            fatal = true;
            continue;
        }

        const double number = value.toDouble();

        if (std::floor(number) != number) {
            addIssue(field.key, QStringLiteral("Invalid input: expected int, received number"));
            fatal = true;
            continue;
        }

        if (number < static_cast<double>(field.minimum)) {
            addIssue(field.key, QStringLiteral("Too small: expected number to be >=%1").arg(field.minimum));
        } else if (number > static_cast<double>(field.maximum)) {
            addIssue(field.key, QStringLiteral("Too big: expected number to be <=%1").arg(field.maximum));
        }

        numbers.insert(field.key, number);
    }

    for (const EnumField &field : schema->enums) {
        const QVariant value = config.value(field.key);

        if (!AgentSchemas::isString(value) || !field.options.contains(value.toString())) {
            QStringList quoted;

            for (const QString &option : field.options) {
                quoted.append(QStringLiteral("\"%1\"").arg(option));
            }

            addIssue(field.key, QStringLiteral("Invalid option: expected one of %1").arg(quoted.join(QLatin1Char('|'))));
            fatal = true;
        }
    }

    if (!fatal) {
        const double total = numbers.value(QStringLiteral("totalTokens"));
        const double reserve = numbers.value(QStringLiteral("responseTokenReserve"));

        for (const Refinement refinement : schema->refinements) {
            switch (refinement) {
            case Refinement::ReserveBelowTotal:
                if (!(reserve < total)) {
                    addIssue(
                        QStringLiteral("responseTokenReserve"),
                        QStringLiteral("Response token reserve must be smaller than total tokens.")
                    );
                }
                break;
            case Refinement::KeywordOrRecencyWeight:
                if (!(numbers.value(QStringLiteral("keywordWeight")) > 0
                      || numbers.value(QStringLiteral("recencyWeight")) > 0)) {
                    addIssue(
                        QStringLiteral("keywordWeight"),
                        QStringLiteral("Keyword weight and recency weight cannot both be zero.")
                    );
                }
                break;
            case Refinement::AutoBalancedBudgets:
                if (reserve >= total) {
                    addIssue(
                        QStringLiteral("responseTokenReserve"),
                        QStringLiteral("Response token reserve must be smaller than the total token budget.")
                    );
                }

                if (numbers.value(QStringLiteral("recentHistoryTokens")) + numbers.value(QStringLiteral("retrievalTokens"))
                    > total - reserve) {
                    addIssue(
                        QStringLiteral("retrievalTokens"),
                        QStringLiteral(
                            "Recent-history and retrieval budgets cannot exceed the available provider input budget."
                        )
                    );
                }
                break;
            }
        }
    }

    if (!issues.isEmpty()) {
        throw CoreError(
            QStringLiteral("Invalid configuration for Context Compiler \"%1\". %2")
                .arg(registryKey(id, version), issues.join(QLatin1Char(' ')))
        );
    }
}
}
