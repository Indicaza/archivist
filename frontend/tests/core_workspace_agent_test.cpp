// Agent-facing workspace operations: semantic intents, placement and
// putting arrangements back. Deterministic: no model, no UI.

#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_arrangements.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_intents.h"
#include "Core/Domains/Workspace/workspace_model.h"
#include "Core/Domains/Workspace/workspace_placement.h"
#include "Core/Domains/Workspace/workspace_presets.h"
#include "Core/Domains/Workspace/workspace_resources.h"

#include <QJsonDocument>
#include <QSet>
#include <QtTest>

using namespace Workspace;

namespace
{
WorkspaceModel::IdGenerator counter()
{
    auto next = std::make_shared<int>(0);
    return [next](const QString &prefix) { return prefix + QString::number(++*next); };
}

QVariantMap fileRow(const QString &path)
{
    return {
        {QStringLiteral("id"), QStringLiteral("f-") + QString(path).replace(QLatin1Char('/'), QLatin1Char('-'))},
        {QStringLiteral("name"), path.section(QLatin1Char('/'), -1)},
        {QStringLiteral("relativePath"), path},
        {QStringLiteral("extension"), path.section(QLatin1Char('.'), -1)},
    };
}

QVariantMap resourceMap(const ResourceRef &resource)
{
    return {
        {QStringLiteral("kind"), resource.kind},
        {QStringLiteral("id"), resource.id},
        {QStringLiteral("collectionId"), resource.collectionId},
        {QStringLiteral("libraryId"), resource.libraryId},
        {QStringLiteral("details"), resource.details},
    };
}

QVariantMap fileItem(const QString &path, const QString &libraryId = QStringLiteral("lib-main"))
{
    const ResourceRef resource = Resources::document(QStringLiteral("col"), libraryId, fileRow(path));
    return {
        {QStringLiteral("typeId"), Resources::viewForFile(path, {}).typeId},
        {QStringLiteral("resource"), resourceMap(resource)},
        {QStringLiteral("title"), path.section(QLatin1Char('/'), -1)},
    };
}

ResourceRef doc(const QString &path, const QString &libraryId = QStringLiteral("lib-main"))
{
    return Resources::document(QStringLiteral("col"), libraryId, fileRow(path));
}

ViewRecord viewOf(const QVariantMap &item)
{
    ViewRecord view;
    view.typeId = item.value(QStringLiteral("typeId")).toString();
    const QVariantMap resource = item.value(QStringLiteral("resource")).toMap();
    view.resource.kind = resource.value(QStringLiteral("kind")).toString();
    view.resource.id = resource.value(QStringLiteral("id")).toString();
    view.resource.collectionId = resource.value(QStringLiteral("collectionId")).toString();
    view.resource.libraryId = resource.value(QStringLiteral("libraryId")).toString();
    view.resource.details = resource.value(QStringLiteral("details")).toMap();
    view.title = item.value(QStringLiteral("title")).toString();
    return view;
}

ViewRecord chatView(const QString &chatId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Chat;
    view.resource = Resources::chat(chatId);
    view.title = QStringLiteral("Investigation");
    return view;
}

ViewRecord terminalView(const QString &sessionId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Terminal;
    view.resource = Resources::terminal(QStringLiteral("col"), QStringLiteral("lib-main"), sessionId);
    view.title = QStringLiteral("Terminal");
    return view;
}

QVariantMap show(const QVariantList &items, const QString &where = {}, const QString &purpose = {}, bool focus = false)
{
    return {
        {QStringLiteral("action"), QStringLiteral("show")},
        {QStringLiteral("items"), items},
        {QStringLiteral("where"), where},
        {QStringLiteral("purpose"), purpose},
        {QStringLiteral("focus"), focus},
    };
}

QByteArray bytes(const WorkspaceModel &model)
{
    return QJsonDocument(Codec::encodeSnapshot(model)).toJson(QJsonDocument::Compact);
}

double share(const WorkspaceModel &model, const QString &anchorId, const QSizeF &size = QSizeF(1440, 900))
{
    const QRectF rect = model.computeGeometry(model.surfaceOfAnchor(anchorId), size, {}).anchors.value(anchorId);
    return rect.width() * rect.height() / (size.width() * size.height());
}
} // namespace

class CoreWorkspaceAgentTest final : public QObject
{
    Q_OBJECT

private:
    ViewTypeRegistry m_registry = ViewTypeRegistry::withBuiltIns();

    Intents::Environment environment(bool mayMoveFocus = false) const
    {
        Intents::Environment env;
        env.registry = &m_registry;
        env.mayMoveFocus = mayMoveFocus;
        return env;
    }

    // editor (with main.cpp) | chat
    struct Desk
    {
        WorkspaceModel model{counter()};
        QString editors;
        QString chat;
        QString mainView;
        QString chatViewId;
    };

    Desk desk() const
    {
        Desk d;
        d.editors = d.model.anchorIds().first();
        d.mainView = d.model.openView(viewOf(fileItem(QStringLiteral("src/main.cpp"))), d.editors).createdId;
        d.chat = d.model.createAnchor(d.editors, Edge::Right).createdId;
        d.chatViewId = d.model.openView(chatView(QStringLiteral("chat-1")), d.chat).createdId;
        d.model.setSplitSizes(d.model.node(d.model.leafOfAnchor(d.editors))->parentId, {0.65, 0.35});
        d.model.focusAnchor(d.editors);
        return d;
    }

private slots:
    // 1. "Open these three files beside our conversation."
    void opensThreeFilesBesideTheChat()
    {
        Desk d = desk();
        const QStringList anchorsBefore = d.model.anchorIds();

        const Intents::Outcome outcome = Intents::perform(
            d.model,
            show({fileItem(QStringLiteral("src/a.cpp")), fileItem(QStringLiteral("src/b.cpp")), fileItem(QStringLiteral("docs/c.md"))},
                 QStringLiteral("beside our conversation")),
            environment()
        );

        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QCOMPARE(outcome.opened.size(), 3);
        // Next to the chat, all in one panel: the editor is already there,
        // so no new split was needed.
        QCOMPARE(d.model.anchorIds(), anchorsBefore);
        for (const QString &viewId : outcome.opened) {
            QCOMPARE(d.model.anchorOfView(viewId), d.editors);
        }
        // The chat keeps only its conversation.
        QCOMPARE(d.model.anchor(d.chat)->views, QStringList{d.chatViewId});
        QVERIFY(d.model.validate().isEmpty());
    }

    void opensBesideTheBottomConversationWithoutSplitting()
    {
        WorkspaceModel model(counter());
        const QString editors = model.anchorIds().first();

        const Intents::Outcome outcome = Intents::perform(
            model,
            show({fileItem(QStringLiteral("src/a.cpp")), fileItem(QStringLiteral("src/b.cpp")), fileItem(QStringLiteral("src/c.cpp"))},
                 QStringLiteral("beside the chat")),
            environment()
        );

        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QCOMPARE(model.anchorIds(), QStringList{editors});
        QCOMPARE(model.anchor(editors)->views.size(), 3);
        // The conversation is in the bottom workbench: keep it open.
        QCOMPARE(outcome.commandSurface, QStringLiteral("chat"));
        // The first file is the one in front.
        QCOMPARE(model.anchor(editors)->activeViewId, outcome.opened.first());
    }

    void splitsBesideAChatWhenNothingElseFits()
    {
        WorkspaceModel model(counter());
        const QString chat = model.anchorIds().first();
        model.openView(chatView(QStringLiteral("chat-1")), chat);

        const Intents::Outcome outcome = Intents::perform(
            model, show({fileItem(QStringLiteral("src/a.cpp")), fileItem(QStringLiteral("src/b.cpp"))}, QStringLiteral("beside the chat")), environment()
        );

        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QCOMPARE(model.anchorIds().size(), 2);
        QCOMPARE(outcome.createdAnchors.size(), 1);
        const QString files = outcome.createdAnchors.first();
        QCOMPARE(model.anchor(files)->views.size(), 2);
        QCOMPARE(model.neighborAnchor(chat, Edge::Right, QSizeF(1440, 900), {}), files);
        // Made for this request, so it folds away when emptied.
        QVERIFY(!model.anchor(files)->pinned);
    }

    // 2. Reuse open views instead of duplicating them.
    void reusesViewsThatAreAlreadyOpen()
    {
        Desk d = desk();
        const int viewsBefore = d.model.viewIds().size();

        const Intents::Outcome outcome = Intents::perform(d.model, show({fileItem(QStringLiteral("src/main.cpp"))}), environment());

        QVERIFY(outcome.ok);
        QVERIFY(outcome.opened.isEmpty());
        QCOMPARE(outcome.revealed, QStringList{d.mainView});
        QCOMPARE(d.model.viewIds().size(), viewsBefore);

        // Asked for somewhere specific, the open view moves there instead
        // of being duplicated.
        const Intents::Outcome moved = Intents::perform(d.model, show({fileItem(QStringLiteral("src/main.cpp"))}, QStringLiteral("right")), environment());
        QVERIFY2(moved.ok, qPrintable(moved.message));
        QCOMPARE(d.model.viewIds().size(), viewsBefore);
        QVERIFY(d.model.anchorOfView(d.mainView) != d.editors);
    }

    // 3. "Make the code editor larger."
    void enlargesAndShrinksAnArea()
    {
        Desk d = desk();
        const double before = share(d.model, d.editors);

        Intents::Outcome outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), QStringLiteral("the code")}, {QStringLiteral("change"), QStringLiteral("larger")}},
            environment()
        );
        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        const double after = share(d.model, d.editors);
        QVERIFY2(after > before + 0.08, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(after)));
        QVERIFY(outcome.notes.first().contains(QStringLiteral("larger")));

        outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), QStringLiteral("chat")}, {QStringLiteral("change"), QStringLiteral("0.5")}},
            environment()
        );
        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QVERIFY(qAbs(share(d.model, d.chat) - 0.5) < 0.02);
    }

    void resizingSparesLockedNeighbours()
    {
        Desk d = desk();
        const QString notes = d.model.createAnchor(d.chat, Edge::Right).createdId;
        d.model.openView(viewOf(fileItem(QStringLiteral("notes.md"))), notes);
        d.model.setAnchorLayoutLocked(notes, true);
        const double locked = share(d.model, notes);

        const Intents::Outcome outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), d.editors}, {QStringLiteral("change"), QStringLiteral("larger")}},
            environment()
        );
        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QVERIFY(qAbs(share(d.model, notes) - locked) < 0.001);
    }

    // 5. Locked panels refuse agents, and agents can't unlock them.
    void lockedAreasRefuseAgents()
    {
        Desk d = desk();
        d.model.setAnchorContentLocked(d.chat, true);
        d.model.setAnchorLayoutLocked(d.chat, true);
        const QByteArray before = bytes(d.model);

        Intents::Outcome outcome = Intents::perform(d.model, show({fileItem(QStringLiteral("a.cpp"))}, QStringLiteral("Chat")), environment());
        QVERIFY(!outcome.ok);
        QCOMPARE(outcome.code, QStringLiteral("CONTENT_LOCKED"));

        outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), QStringLiteral("the chat")}, {QStringLiteral("change"), QStringLiteral("larger")}},
            environment()
        );
        QVERIFY(!outcome.ok);
        QCOMPARE(outcome.code, QStringLiteral("LAYOUT_LOCKED"));

        outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("target"), QStringLiteral("the chat")}, {QStringLiteral("to"), QStringLiteral("left")}},
            environment()
        );
        QVERIFY(!outcome.ok);
        QCOMPARE(outcome.code, QStringLiteral("LAYOUT_LOCKED"));

        QCOMPARE(bytes(d.model), before);
        QCOMPARE(d.model.setAnchorLayoutLocked(d.chat, false, Origin::Agent).code, QStringLiteral("UNLOCK_REFUSED"));

        // "Keep this panel where it is" locks it.
        d.model.focusAnchor(d.editors);
        outcome = Intents::perform(d.model, {{QStringLiteral("action"), QStringLiteral("keep")}, {QStringLiteral("target"), QStringLiteral("this panel")}}, environment());
        QVERIFY(outcome.ok);
        QVERIFY(d.model.anchor(d.editors)->layoutLocked);
    }

    // 6 (model side). Focus stays with the person unless they asked.
    void focusStaysUnlessAsked()
    {
        Desk d = desk();
        d.model.focusAnchor(d.chat);

        Intents::Outcome outcome = Intents::perform(d.model, show({fileItem(QStringLiteral("x.cpp"))}), environment(true));
        QVERIFY(outcome.ok);
        QCOMPARE(d.model.focusedAnchorId(), d.chat);

        outcome = Intents::perform(d.model, show({fileItem(QStringLiteral("y.cpp"))}, {}, {}, true), environment(false));
        QCOMPARE(d.model.focusedAnchorId(), d.chat);

        outcome = Intents::perform(d.model, show({fileItem(QStringLiteral("z.cpp"))}, {}, {}, true), environment(true));
        QVERIFY(outcome.ok);
        QCOMPARE(d.model.focusedAnchorId(), d.editors);
    }

    // 7. Files from different worktrees stay in their own worktree.
    void worktreesKeepTheirBindings()
    {
        Desk d = desk();
        d.model.setAnchorLibrary(d.editors, QStringLiteral("lib-main"));

        const Intents::Outcome outcome = Intents::perform(
            d.model,
            show({fileItem(QStringLiteral("src/main.cpp"), QStringLiteral("lib-feature")), fileItem(QStringLiteral("src/util.cpp"), QStringLiteral("lib-feature"))}),
            environment()
        );

        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        // The same path in another worktree is another resource: a new view,
        // not the main checkout's main.cpp.
        QCOMPARE(outcome.opened.size(), 2);
        for (const QString &viewId : outcome.opened) {
            QCOMPARE(d.model.view(viewId)->resource.libraryId, QStringLiteral("lib-feature"));
            QVERIFY(d.model.view(viewId)->resource.id.contains(QStringLiteral(":lib-feature:")));
        }
        QCOMPARE(d.model.view(d.mainView)->resource.libraryId, QStringLiteral("lib-main"));
        QCOMPARE(d.model.anchor(d.editors)->libraryId, QStringLiteral("lib-main"));
    }

    // 8 (model side). Moving things never touches terminals or chats.
    void rearrangingKeepsTerminalsAndChats()
    {
        Desk d = desk();
        const QString terminalId = d.model.openView(terminalView(QStringLiteral("t1")), d.editors).createdId;
        const ViewRecord terminal = *d.model.view(terminalId);
        const ViewRecord chat = *d.model.view(d.chatViewId);

        Intents::Outcome outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("target"), QStringLiteral("the terminal")}, {QStringLiteral("to"), QStringLiteral("bottom")}},
            environment()
        );
        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("move")}, {QStringLiteral("target"), QStringLiteral("chat")}, {QStringLiteral("to"), QStringLiteral("left")}},
            environment()
        );
        QVERIFY2(outcome.ok, qPrintable(outcome.message));

        QCOMPARE(d.model.view(terminalId)->resource, terminal.resource);
        QCOMPARE(d.model.view(d.chatViewId)->resource, chat.resource);
        QVERIFY(d.model.anchorOfView(terminalId) != d.editors);
        QVERIFY(d.model.validate().isEmpty());
    }

    // 12. Unknown or unsupported requests change nothing.
    void unsupportedRequestsLeaveNothingBehind()
    {
        Desk d = desk();
        const QByteArray before = bytes(d.model);

        QCOMPARE(Intents::perform(d.model, {{QStringLiteral("action"), QStringLiteral("dance")}}, environment()).code, QStringLiteral("UNSUPPORTED"));

        QVariantMap unknownType = fileItem(QStringLiteral("graph.kg"));
        unknownType.insert(QStringLiteral("typeId"), QStringLiteral("plugin.acme.graph"));
        QCOMPARE(Intents::perform(d.model, show({unknownType}), environment()).code, QStringLiteral("UNSUPPORTED"));

        // The first item would open, the second can't: neither does.
        const Intents::Outcome partial = Intents::perform(
            d.model, show({fileItem(QStringLiteral("ok.cpp")), unknownType}, QStringLiteral("right")), environment()
        );
        QVERIFY(!partial.ok);

        QCOMPARE(Intents::perform(d.model, show({fileItem(QStringLiteral("a.cpp"))}, QStringLiteral("the kitchen sink")), environment()).code, QStringLiteral("NOT_FOUND"));
        QCOMPARE(Intents::perform(d.model, {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), QStringLiteral("chat")}, {QStringLiteral("change"), QStringLiteral("sideways")}}, environment()).code, QStringLiteral("INVALID_ARGUMENT"));

        QCOMPARE(bytes(d.model), before);
    }

    void plainDescriptionsFindAreas()
    {
        Desk d = desk();
        const QString docs = d.model.createAnchor(d.chat, Edge::Bottom).createdId;
        d.model.openView(viewOf(fileItem(QStringLiteral("docs/guide.md"))), docs);
        d.model.renameAnchor(docs, QStringLiteral("Reference"));
        const Intents::Environment env = environment();

        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("the code"), env).anchorId, d.editors);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("our conversation"), env).anchorId, d.chat);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("the documentation"), env).anchorId, docs);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("reference"), env).anchorId, docs);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("the left panel"), env).anchorId, d.editors);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("main.cpp"), env).viewId, d.mainView);
        d.model.focusAnchor(d.chat);
        QCOMPARE(Intents::resolveArea(d.model, QStringLiteral("this panel"), env).anchorId, d.chat);
        QVERIFY(!Intents::resolveArea(d.model, QStringLiteral("the garden"), env).isValid());
    }

    void documentationGoesToTheSide()
    {
        WorkspaceModel model(counter());
        const QString editors = model.anchorIds().first();
        model.openView(viewOf(fileItem(QStringLiteral("src/a.cpp"))), editors);

        const Intents::Outcome outcome = Intents::perform(
            model, show({fileItem(QStringLiteral("docs/one.md")), fileItem(QStringLiteral("docs/two.md"))}, {}, QStringLiteral("documentation")), environment()
        );

        QVERIFY2(outcome.ok, qPrintable(outcome.message));
        QCOMPARE(outcome.createdAnchors.size(), 1);
        const QString side = outcome.createdAnchors.first();
        QCOMPARE(model.anchor(side)->views.size(), 2);
        QCOMPARE(model.neighborAnchor(editors, Edge::Right, QSizeF(1440, 900), {}), side);
        // A side panel, not half the window.
        QVERIFY(share(model, side) < 0.4);

        // More documentation joins it rather than opening another panel.
        const Intents::Outcome more = Intents::perform(model, show({fileItem(QStringLiteral("docs/three.md"))}, {}, QStringLiteral("reference")), environment());
        QVERIFY(more.ok);
        QVERIFY(more.createdAnchors.isEmpty());
        QCOMPARE(model.anchorOfView(more.opened.first()), side);
    }

    void labelsAnArea()
    {
        Desk d = desk();
        const Intents::Outcome outcome = Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("label")}, {QStringLiteral("target"), QStringLiteral("the chat")}, {QStringLiteral("name"), QStringLiteral("Reference documents")}},
            environment()
        );
        QVERIFY(outcome.ok);
        QCOMPARE(d.model.anchorDisplayName(d.chat), QStringLiteral("Reference documents"));
    }

    void describesTheWorkspaceInWords()
    {
        Desk d = desk();
        d.model.setAnchorLayoutLocked(d.chat, true);
        const QVariantMap description = Intents::describe(d.model, environment());
        const QVariantList areas = description.value(QStringLiteral("areas")).toList();

        QCOMPARE(areas.size(), 2);
        QCOMPARE(areas.at(0).toMap().value(QStringLiteral("area")).toString(), QStringLiteral("Editor"));
        QCOMPARE(areas.at(0).toMap().value(QStringLiteral("position")).toString(), QStringLiteral("left"));
        QCOMPARE(areas.at(1).toMap().value(QStringLiteral("position")).toString(), QStringLiteral("right"));
        QVERIFY(areas.at(1).toMap().value(QStringLiteral("locked")).toString().contains(QStringLiteral("layout locked")));
        QVERIFY(description.value(QStringLiteral("viewTypes")).toStringList().contains(ViewTypes::Terminal));
    }

    // 4 (model side). Putting an arrangement back.
    void restoresAnArrangement()
    {
        Desk d = desk();
        const WorkspaceModel::Data before = d.model.data();
        const QByteArray layoutBefore = QJsonDocument(Codec::captureLayout(d.model, {})).toJson(QJsonDocument::Compact);

        const Intents::Outcome opened = Intents::perform(
            d.model, show({fileItem(QStringLiteral("a.md")), fileItem(QStringLiteral("b.md"))}, QStringLiteral("right"), QStringLiteral("documentation")), environment()
        );
        QVERIFY(opened.ok);
        Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), d.editors}, {QStringLiteral("change"), QStringLiteral("larger")}},
            environment()
        );
        // The person opens something themselves meanwhile.
        const QString mine = d.model.openView(viewOf(fileItem(QStringLiteral("mine.cpp"))), d.editors).createdId;

        const QSet<QString> closable(opened.opened.cbegin(), opened.opened.cend());
        const Arrangements::Result restored = Arrangements::restore(d.model, before, closable);
        QVERIFY2(restored.ok, qPrintable(restored.message));

        QCOMPARE(QSet<QString>(restored.closedViews.cbegin(), restored.closedViews.cend()), closable);
        QCOMPARE(d.model.anchorOfView(mine), d.editors);
        QCOMPARE(d.model.anchorIds(), QStringList({d.editors, d.chat}));
        // Same panels, same sizes: only the person's new tab differs.
        WorkspaceModel withoutMine = d.model;
        withoutMine.closeView(mine);
        QCOMPARE(QJsonDocument(Codec::captureLayout(withoutMine, {})).toJson(QJsonDocument::Compact), layoutBefore);
        QVERIFY(d.model.validate().isEmpty());
    }

    void restoringNeverMovesALockedPanel()
    {
        Desk d = desk();
        const WorkspaceModel::Data before = d.model.data();
        Intents::perform(
            d.model,
            {{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("target"), d.chat}, {QStringLiteral("change"), QStringLiteral("larger")}},
            environment()
        );
        // The person locks the chat at its new size.
        d.model.setAnchorLayoutLocked(d.chat, true);
        const QByteArray now = bytes(d.model);

        const Arrangements::Result restored = Arrangements::restore(d.model, before, {});
        QVERIFY(!restored.ok);
        QCOMPARE(restored.code, QStringLiteral("LAYOUT_LOCKED"));
        QCOMPARE(bytes(d.model), now);
    }

    void filesOpenInTheRightView()
    {
        QCOMPARE(Resources::viewForFile(QStringLiteral("a.cpp"), {}).typeId, ViewTypes::Editor);
        QCOMPARE(Resources::viewForFile(QStringLiteral("README.md"), {}).typeId, ViewTypes::Preview);
        QVERIFY(Resources::viewForFile(QStringLiteral("README.md"), {}).textual);
        QCOMPARE(Resources::viewForFile(QStringLiteral("logo.png"), {}).typeId, ViewTypes::Preview);
        QVERIFY(!Resources::viewForFile(QStringLiteral("logo.png"), {}).textual);
        QCOMPARE(Resources::viewForFile(QStringLiteral("config"), {}).typeId, ViewTypes::Editor);
        const ResourceRef document = Resources::document(QStringLiteral("col"), QStringLiteral("lib"), fileRow(QStringLiteral("src/a.cpp")));
        QCOMPARE(document.id, QStringLiteral("col:lib:f-src-a.cpp"));
        QCOMPARE(document.details.value(QStringLiteral("path")).toString(), QStringLiteral("src/a.cpp"));
    }
    // ---- placement (phase B) ----------------------------------------------

    void filesNeverLandInAChatPanel()
    {
        Desk d = desk();
        d.model.focusAnchor(d.chat);
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("a.cpp")), d.editors);

        // A target the person picked wins over focus.
        const QString side = d.model.createAnchor(d.editors, Edge::Bottom).createdId;
        d.model.setTargetAnchor(side);
        d.model.focusAnchor(d.chat);
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("a.cpp")), side);
    }

    void filesFollowWhereThePersonWorked()
    {
        Desk d = desk();
        const QString second = d.model.createAnchor(d.editors, Edge::Bottom).createdId;
        d.model.openView(viewOf(fileItem(QStringLiteral("b.cpp"))), second);
        d.model.focusAnchor(d.chat);

        Placement::Hints hints;
        hints.recentAnchors = {d.chat, second, d.editors};
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("c.cpp"), hints), second);
        hints.recentAnchors = {d.chat, d.editors, second};
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("c.cpp"), hints), d.editors);
    }

    void filesRespectWorktreeBindings()
    {
        Desk d = desk();
        d.model.setAnchorLibrary(d.editors, QStringLiteral("lib-main"));
        const QString other = d.model.createAnchor(d.editors, Edge::Bottom).createdId;
        d.model.setAnchorLibrary(other, QStringLiteral("lib-feature"));
        d.model.focusAnchor(d.editors);

        // A file from the feature worktree goes to that worktree's panel,
        // even though the main panel has focus.
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("a.cpp", QStringLiteral("lib-feature"))), other);
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Editor, doc("a.cpp", QStringLiteral("lib-main"))), d.editors);
        // Neither binding changed.
        QCOMPARE(d.model.anchor(d.editors)->libraryId, QStringLiteral("lib-main"));
        QCOMPARE(d.model.anchor(other)->libraryId, QStringLiteral("lib-feature"));
    }

    void filesUsePanelsMeantForThem()
    {
        WorkspaceModel model(counter());
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("research")), model).ok);
        model.focusAnchor(QStringLiteral("research.chat"));
        model.setTargetAnchor({});

        QCOMPARE(Placement::chooseAnchor(model, ViewTypes::Preview, doc("paper.pdf")), QStringLiteral("research.sources"));
        QCOMPARE(Placement::chooseAnchor(model, ViewTypes::Editor, doc("notes.cpp")), QStringLiteral("research.notes"));
        QCOMPARE(Placement::chooseAnchor(model, ViewTypes::Terminal, Resources::terminal(QStringLiteral("col"), QStringLiteral("lib-main"), QStringLiteral("t"))), QStringLiteral("research.notes"));
    }

    void terminalsJoinTheirWorktreesTerminals()
    {
        Desk d = desk();
        const QString mainTerms = d.model.createAnchor(d.editors, Edge::Bottom).createdId;
        d.model.openView(terminalView(QStringLiteral("t1")), mainTerms);
        const QString featureTerms = d.model.createAnchor(mainTerms, Edge::Right).createdId;
        ViewRecord featureTerminal = terminalView(QStringLiteral("t2"));
        featureTerminal.resource = Resources::terminal(QStringLiteral("col"), QStringLiteral("lib-feature"), QStringLiteral("t2"));
        d.model.openView(featureTerminal, featureTerms);

        const ResourceRef next = Resources::terminal(QStringLiteral("col"), QStringLiteral("lib-feature"), QStringLiteral("t3"));
        QCOMPARE(Placement::chooseAnchor(d.model, ViewTypes::Terminal, next), featureTerms);
    }

};

QTEST_GUILESS_MAIN(CoreWorkspaceAgentTest)
#include "core_workspace_agent_test.moc"
