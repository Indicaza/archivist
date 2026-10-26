#include "agent_instructions.h"

#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Support/js_compat.h"

#include <QStringList>

namespace
{
QString text(const QVariantMap &map, const char *key)
{
    const QVariant value = map.value(QLatin1String(key));
    return value.typeId() == QMetaType::QString ? value.toString() : QString();
}

// value.trim().replace(/\s+/g, " ") with JavaScript's whitespace set.
QString normalizeInstruction(const QString &value)
{
    QString result;
    bool inSpace = false;

    for (const QChar character : JsCompat::trimmedView(value)) {
        if (JsCompat::isSpace(character)) {
            inSpace = true;
            continue;
        }

        if (inSpace) {
            result.append(QLatin1Char(' '));
            inSpace = false;
        }

        result.append(character);
    }

    return result;
}

void addSection(QStringList &sections, const QString &heading, const QStringList &lines)
{
    QStringList content;

    for (const QString &line : lines) {
        const QString trimmed = JsCompat::trimmed(line);

        if (!trimmed.isEmpty()) {
            content.append(trimmed);
        }
    }

    if (content.isEmpty()) {
        return;
    }

    content.prepend(QStringLiteral("## ") + heading);
    sections.append(content.join(QLatin1Char('\n')));
}

void addListSection(QStringList &sections, const QString &heading, const QVariant &items)
{
    QStringList content;

    for (const QVariant &item : items.toList()) {
        const QString trimmed = JsCompat::trimmed(item.toString());

        if (!trimmed.isEmpty()) {
            content.append(QStringLiteral("- ") + trimmed);
        }
    }

    if (content.isEmpty()) {
        return;
    }

    content.prepend(QStringLiteral("## ") + heading);
    sections.append(content.join(QLatin1Char('\n')));
}

QString labeled(const QString &label, const QString &value)
{
    return value.isEmpty() ? QString() : label + value;
}

QString effectiveSystemInstructions(const QVariantMap &agent)
{
    const QString instructions = JsCompat::trimmed(text(agent, "systemInstructions"));

    if (instructions.isEmpty()) {
        return {};
    }

    // Custom Agents that inherited Archivist Default's instructions must not
    // take on the built-in identity.
    const bool inheritedDefault = normalizeInstruction(instructions) == normalizeInstruction(AgentDefaults::systemInstructions());

    if (!agent.value(QStringLiteral("isBuiltIn")).toBool() && inheritedDefault) {
        return {};
    }

    return instructions;
}
}

namespace AgentInstructions
{
QString build(const QVariantMap &agent)
{
    const QString name = text(agent, "name");
    const QVariantMap identity = agent.value(QStringLiteral("identity")).toMap();
    const QVariantMap profession = agent.value(QStringLiteral("profession")).toMap();
    const QVariantMap output = agent.value(QStringLiteral("outputContract")).toMap();
    QStringList sections;

    addSection(sections, QStringLiteral("Operating Authority"), {
        QStringList{
            QStringLiteral("You are %1.").arg(name),
            QStringLiteral("The Agent profile below defines your active identity, temperament, voice, behavior, profession, and response contract."),
            QStringLiteral("Enact that profile consistently in every response, including casual conversation."),
            QStringLiteral("Do not revert to a generic friendly, neutral, or conventionally helpful assistant persona when that would conflict with this Agent."),
            QStringLiteral("Behavioral fields such as personality, temperament, voice, doctrine, and response style are operational instructions, not decorative metadata."),
        }.join(QLatin1Char(' ')),
    });

    addSection(sections, QStringLiteral("Agent Identity"), {
        QStringLiteral("Name: ") + name,
        labeled(QStringLiteral("Description: "), text(agent, "description")),
        labeled(QStringLiteral("Personality: "), text(identity, "personality")),
        labeled(QStringLiteral("Temperament: "), text(identity, "temperament")),
        labeled(QStringLiteral("Voice: "), text(identity, "voice")),
        labeled(QStringLiteral("Background: "), text(identity, "backstory")),
    });

    addSection(sections, QStringLiteral("Behavioral Enforcement"), {
        text(identity, "personality").isEmpty()
            ? QString()
            : QStringLiteral("Express the configured personality directly rather than merely understanding or describing it."),
        text(identity, "temperament").isEmpty()
            ? QString()
            : QStringLiteral("Let the configured temperament affect word choice, patience, warmth, bluntness, humor, and willingness to engage."),
        text(identity, "voice").isEmpty() ? QString() : QStringLiteral("Write in the configured voice throughout the response."),
        QStringList{
            QStringLiteral("Do not soften, sanitize, or replace the configured personality merely because another style would be more conventionally assistant-like."),
            QStringLiteral("Still follow platform safety requirements and do not fabricate facts or capabilities."),
        }.join(QLatin1Char(' ')),
    });

    addSection(sections, QStringLiteral("Profession"), {
        labeled(QStringLiteral("Job title: "), text(profession, "jobTitle")),
        labeled(QStringLiteral("Mission: "), text(profession, "mission")),
    });

    addListSection(sections, QStringLiteral("Expertise"), profession.value(QStringLiteral("expertise")));
    addListSection(sections, QStringLiteral("Responsibilities"), profession.value(QStringLiteral("responsibilities")));
    addListSection(sections, QStringLiteral("Success Criteria"), profession.value(QStringLiteral("successCriteria")));
    addListSection(sections, QStringLiteral("Limitations and Deferral"), profession.value(QStringLiteral("limitations")));
    addSection(sections, QStringLiteral("Working Doctrine"), {text(agent, "doctrine")});

    addSection(sections, QStringLiteral("Output Contract"), {
        labeled(QStringLiteral("Response style: "), text(output, "responseStyle")),
        QStringLiteral("Verbosity preference: ") + output.value(QStringLiteral("verbosity")).toString(),
        labeled(QStringLiteral("Citation requirements: "), text(output, "citationRequirements")),
        labeled(QStringLiteral("Follow-up behavior: "), text(output, "followUpBehavior")),
    });

    addListSection(sections, QStringLiteral("Formatting Rules"), output.value(QStringLiteral("formattingRules")));
    addListSection(sections, QStringLiteral("Code Output Preferences"), output.value(QStringLiteral("codeOutputPreferences")));
    addSection(sections, QStringLiteral("Additional Explicit Instructions"), {effectiveSystemInstructions(agent)});

    addSection(sections, QStringLiteral("Final Behavioral Check"), {
        QStringList{
            QStringLiteral("Before answering, verify that the response sounds and behaves like %1.").arg(name),
            QStringLiteral("The response should visibly reflect the configured personality, temperament, voice, doctrine, and output contract."),
            QStringLiteral("A response that could have been produced unchanged by a generic assistant does not satisfy this Agent profile."),
        }.join(QLatin1Char(' ')),
    });

    return JsCompat::trimmed(sections.join(QStringLiteral("\n\n")));
}
}
