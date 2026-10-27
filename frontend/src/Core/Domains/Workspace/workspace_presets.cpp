#include "Core/Domains/Workspace/workspace_presets.h"

#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_codec.h"

#include <QJsonArray>

namespace Workspace::Presets
{
namespace
{
QJsonObject anchor(const QString &id, const QStringList &roles)
{
    QJsonArray roleArray;
    for (const QString &role : roles) {
        roleArray.append(role);
    }

    return {{QStringLiteral("anchor"), QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("pinned"), true},
        {QStringLiteral("roles"), roleArray},
    }}};
}

QJsonObject split(const QString &orientation, const QList<double> &sizes, const QList<QJsonObject> &children)
{
    QJsonArray sizeArray;
    QJsonArray childArray;

    for (double size : sizes) {
        sizeArray.append(size);
    }
    for (const QJsonObject &child : children) {
        childArray.append(child);
    }

    return {
        {QStringLiteral("split"), orientation},
        {QStringLiteral("sizes"), sizeArray},
        {QStringLiteral("children"), childArray},
    };
}

QJsonObject row(const QList<double> &sizes, const QList<QJsonObject> &children)
{
    return split(QStringLiteral("horizontal"), sizes, children);
}

QJsonObject column(const QList<double> &sizes, const QList<QJsonObject> &children)
{
    return split(QStringLiteral("vertical"), sizes, children);
}

QJsonObject layoutDocument(const QString &name, const QJsonObject &root, const QString &target)
{
    return {
        {QStringLiteral("format"), Codec::LayoutFormat},
        {QStringLiteral("version"), Codec::kLayoutVersion},
        {QStringLiteral("name"), name},
        {QStringLiteral("targetAnchor"), target},
        {QStringLiteral("surfaces"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("main")},
            {QStringLiteral("primary"), true},
            {QStringLiteral("root"), root},
        }}},
    };
}

Preset make(const QString &id, const QString &name, const QString &description, const QJsonObject &root, const QString &target)
{
    return {id, name, description, layoutDocument(name, root, target)};
}
} // namespace

QList<Preset> all()
{
    using namespace ViewTypes;
    // Future Agent Theater views slot into the observatory by type.
    const QString theater = QStringLiteral("archivist.agent-theater");

    return {
        make(
            QStringLiteral("development"),
            QStringLiteral("Development"),
            QStringLiteral("Editors in front, terminal underneath, Chat at the side."),
            row({0.7, 0.3}, {
                column({0.7, 0.3}, {
                    anchor(QStringLiteral("dev.editors"), {Editor, Preview, Diff, Welcome}),
                    anchor(QStringLiteral("dev.terminal"), {Terminal}),
                }),
                anchor(QStringLiteral("dev.chat"), {Chat}),
            }),
            QStringLiteral("dev.editors")
        ),
        make(
            QStringLiteral("ai-command"),
            QStringLiteral("AI Command"),
            QStringLiteral("A large Chat in the middle, agent conversations and output alongside."),
            row({0.22, 0.5, 0.28}, {
                column({0.5, 0.5}, {
                    anchor(QStringLiteral("ai.agents"), {}),
                    anchor(QStringLiteral("ai.terminal"), {Terminal}),
                }),
                anchor(QStringLiteral("ai.chat"), {Chat, Welcome}),
                column({0.55, 0.45}, {
                    anchor(QStringLiteral("ai.review"), {Diff, Editor}),
                    anchor(QStringLiteral("ai.preview"), {Preview}),
                }),
            }),
            QStringLiteral("ai.review")
        ),
        make(
            QStringLiteral("code-review"),
            QStringLiteral("Code Review"),
            QStringLiteral("Diffs up front, the file and the review Chat beside them."),
            row({0.62, 0.38}, {
                anchor(QStringLiteral("review.diffs"), {Diff, Welcome}),
                column({0.5, 0.5}, {
                    anchor(QStringLiteral("review.files"), {Editor, Preview}),
                    anchor(QStringLiteral("review.chat"), {Chat, Terminal}),
                }),
            }),
            QStringLiteral("review.files")
        ),
        make(
            QStringLiteral("research"),
            QStringLiteral("Research"),
            QStringLiteral("Sources on the left, Chat and notes on the right."),
            row({0.55, 0.45}, {
                anchor(QStringLiteral("research.sources"), {Preview, Welcome}),
                column({0.6, 0.4}, {
                    anchor(QStringLiteral("research.chat"), {Chat}),
                    anchor(QStringLiteral("research.notes"), {Editor, Diff, Terminal}),
                }),
            }),
            QStringLiteral("research.sources")
        ),
        make(
            QStringLiteral("creative"),
            QStringLiteral("Creative Workspace"),
            QStringLiteral("A wide writing surface with its preview and a Chat for ideas."),
            row({0.58, 0.42}, {
                anchor(QStringLiteral("creative.draft"), {Editor, Welcome}),
                column({0.55, 0.45}, {
                    anchor(QStringLiteral("creative.preview"), {Preview, Diff}),
                    anchor(QStringLiteral("creative.chat"), {Chat, Terminal}),
                }),
            }),
            QStringLiteral("creative.draft")
        ),
        make(
            QStringLiteral("agent-observatory"),
            QStringLiteral("Agent Observatory"),
            QStringLiteral("Several agent Chats side by side over their terminals and changes."),
            column({0.62, 0.38}, {
                row({1, 1, 1}, {
                    anchor(QStringLiteral("observe.agent-1"), {Chat, theater, Welcome}),
                    anchor(QStringLiteral("observe.agent-2"), {Chat, theater}),
                    anchor(QStringLiteral("observe.agent-3"), {Chat, theater}),
                }),
                row({0.5, 0.5}, {
                    anchor(QStringLiteral("observe.terminals"), {Terminal}),
                    anchor(QStringLiteral("observe.changes"), {Diff, Editor, Preview}),
                }),
            }),
            QStringLiteral("observe.changes")
        ),
    };
}

QJsonObject layout(const QString &presetId)
{
    for (const Preset &preset : all()) {
        if (preset.id == presetId) {
            return preset.layout;
        }
    }
    return {};
}
} // namespace Workspace::Presets
