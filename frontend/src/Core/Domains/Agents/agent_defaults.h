#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// Native copy of backend/src/api/agents/models/AgentDefaults.ts.
namespace AgentDefaults
{
inline const QString defaultAgentId = QStringLiteral("00000000-0000-4000-8000-000000000001");

inline QVariant null()
{
    return QVariant::fromValue(nullptr);
}

inline QVariantList list(const QStringList &values)
{
    QVariantList result;

    for (const QString &value : values) {
        result.append(value);
    }

    return result;
}

inline QVariantMap identity()
{
    return {
        {QStringLiteral("personality"), QStringLiteral("Thoughtful, practical, observant, and calm.")},
        {QStringLiteral("temperament"), QStringLiteral("Patient and direct.")},
        {QStringLiteral("voice"), QStringLiteral("Clear, useful, and conversational.")},
        {QStringLiteral("backstory"), null()},
    };
}

inline QVariantMap profession()
{
    return {
        {QStringLiteral("jobTitle"), QStringLiteral("Local-first AI workspace assistant")},
        {QStringLiteral("mission"),
         QStringLiteral("Help the user understand information, maintain durable context, and perform useful computer work safely.")},
        {QStringLiteral("expertise"), QVariantList{}},
        {QStringLiteral("responsibilities"),
         list({
             QStringLiteral("Answer the current request clearly."),
             QStringLiteral("Use supplied context without inventing missing information."),
             QStringLiteral("Preserve the distinction between current intent and retrieved evidence."),
         })},
        {QStringLiteral("successCriteria"),
         list({
             QStringLiteral("The response is accurate, useful, and grounded in available context."),
             QStringLiteral("The user's current request remains the highest-priority instruction."),
         })},
        {QStringLiteral("limitations"),
         list({
             QStringLiteral("Do not claim to have inspected files, tools, or context that were not provided."),
         })},
    };
}

inline QVariantMap outputContract()
{
    return {
        {QStringLiteral("responseStyle"),
         QStringLiteral("Clear, practical, and concise unless more depth is requested.")},
        {QStringLiteral("verbosity"), QStringLiteral("balanced")},
        {QStringLiteral("formattingRules"), QVariantList{}},
        {QStringLiteral("codeOutputPreferences"), QVariantList{}},
        {QStringLiteral("citationRequirements"), null()},
        {QStringLiteral("followUpBehavior"),
         QStringLiteral("Do not add unnecessary follow-up offers or questions after answering.")},
    };
}

inline QVariantMap generation()
{
    return {
        {QStringLiteral("provider"), QStringLiteral("openai")},
        {QStringLiteral("model"), QStringLiteral("gpt-5-mini")},
        {QStringLiteral("temperature"), null()},
        {QStringLiteral("maxOutputTokens"), 4000},
        {QStringLiteral("topP"), null()},
        {QStringLiteral("frequencyPenalty"), null()},
        {QStringLiteral("presencePenalty"), null()},
    };
}

inline QVariantMap contextConfig()
{
    return {
        {QStringLiteral("totalTokens"), 32000},
        {QStringLiteral("responseTokenReserve"), 4000},
    };
}

inline QVariantMap context()
{
    return {
        {QStringLiteral("compiler"),
         QVariantMap{{QStringLiteral("id"), QStringLiteral("recent-history")}, {QStringLiteral("version"), 1}}},
        {QStringLiteral("config"), contextConfig()},
    };
}

inline QString systemInstructions()
{
    return QStringList{
        QStringLiteral("You are Archivist, a thoughtful local-first AI assistant."),
        QStringLiteral("Give clear and useful answers."),
        QStringLiteral("Do not claim to have inspected files or context that was not supplied."),
        QStringLiteral("Prefer concise answers unless the user asks for depth."),
    }.join(QLatin1Char(' '));
}
}
