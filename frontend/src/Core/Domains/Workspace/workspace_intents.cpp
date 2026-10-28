#include "Core/Domains/Workspace/workspace_intents.h"

#include "Core/Domains/Workspace/workspace_placement.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Workspace::Intents
{
namespace
{
const QSizeF kTypicalSurface(1440, 900);

// ---- vocabulary ------------------------------------------------------------

bool mentionsAny(const QString &text, const QStringList &words)
{
    for (const QString &word : words) {
        const QRegularExpression pattern(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)));
        if (pattern.match(text).hasMatch()) {
            return true;
        }
    }
    return false;
}

const QStringList &chatWords()
{
    static const QStringList words{
        QStringLiteral("chat"), QStringLiteral("chats"), QStringLiteral("conversation"), QStringLiteral("conversations"),
        QStringLiteral("discussion"), QStringLiteral("assistant"),
    };
    return words;
}

const QStringList &codeWords()
{
    static const QStringList words{
        QStringLiteral("code"), QStringLiteral("editor"), QStringLiteral("editors"), QStringLiteral("source"),
        QStringLiteral("file"), QStringLiteral("files"), QStringLiteral("main editor"),
    };
    return words;
}

const QStringList &terminalWords()
{
    static const QStringList words{
        QStringLiteral("terminal"), QStringLiteral("terminals"), QStringLiteral("shell"), QStringLiteral("console"),
        QStringLiteral("command line"),
    };
    return words;
}

const QStringList &docWords()
{
    static const QStringList words{
        QStringLiteral("doc"), QStringLiteral("docs"), QStringLiteral("documentation"), QStringLiteral("reference"),
        QStringLiteral("references"), QStringLiteral("readme"), QStringLiteral("markdown"), QStringLiteral("notes"),
        QStringLiteral("preview"), QStringLiteral("spec"), QStringLiteral("guide"),
    };
    return words;
}

const QStringList &diffWords()
{
    static const QStringList words{QStringLiteral("diff"), QStringLiteral("diffs"), QStringLiteral("changes"), QStringLiteral("review")};
    return words;
}

const QStringList &hereWords()
{
    static const QStringList words{
        QStringLiteral("this"), QStringLiteral("here"), QStringLiteral("current"), QStringLiteral("focused"),
        QStringLiteral("active"), QStringLiteral("this panel"), QStringLiteral("this one"),
    };
    return words;
}

bool isDocumentFamily(const QString &typeId)
{
    return typeId == ViewTypes::Editor || typeId == ViewTypes::Preview || typeId == ViewTypes::Diff
        || typeId == ViewTypes::Welcome;
}

bool isDocPurpose(const QString &purpose)
{
    return mentionsAny(purpose.toLower(), docWords());
}

bool parseSide(const QString &text, Edge *edge)
{
    static const QList<QPair<QString, Edge>> sides{
        {QStringLiteral("right"), Edge::Right},
        {QStringLiteral("left"), Edge::Left},
        {QStringLiteral("bottom"), Edge::Bottom},
        {QStringLiteral("below"), Edge::Bottom},
        {QStringLiteral("top"), Edge::Top},
        {QStringLiteral("above"), Edge::Top},
    };
    for (const auto &side : sides) {
        if (mentionsAny(text, {side.first})) {
            *edge = side.second;
            return true;
        }
    }
    return false;
}

// "beside the chat" -> "the chat"
bool splitBeside(const QString &text, QString *other)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:.*\\b)?(?:beside|next to|alongside|near|by|with|together with|left of|right of|below|above|under)\\s+(.+)$")
    );
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch() || match.captured(1).trimmed().isEmpty()) {
        return false;
    }
    if (other) {
        *other = match.captured(1).trimmed();
    }
    return true;
}

QString stripFiller(QString text)
{
    text = text.toLower().trimmed();
    static const QRegularExpression filler(
        QStringLiteral("\\b(?:the|a|an|my|our|your|panel|pane|area|section|window|side|one|tab|please)\\b")
    );
    text.replace(filler, QStringLiteral(" "));
    return text.simplified();
}

// ---- reading the model -----------------------------------------------------

QSizeF surfaceSize(const Environment &environment, const QString &surfaceId)
{
    const QSizeF size = environment.surfaceSizes.value(surfaceId);
    return size.width() > 1 && size.height() > 1 ? size : kTypicalSurface;
}

LayoutGeometry geometryOf(const WorkspaceModel &model, const QString &surfaceId, const Environment &environment)
{
    LayoutMetrics metrics = environment.metrics;
    return model.computeGeometry(surfaceId, surfaceSize(environment, surfaceId), metrics);
}

QRectF anchorRect(const WorkspaceModel &model, const QString &anchorId, const Environment &environment)
{
    const QString surfaceId = model.surfaceOfAnchor(anchorId);
    return geometryOf(model, surfaceId, environment).anchors.value(anchorId);
}

double shareOf(const WorkspaceModel &model, const QString &anchorId, const Environment &environment)
{
    const QString surfaceId = model.surfaceOfAnchor(anchorId);
    const QSizeF size = surfaceSize(environment, surfaceId);
    const QRectF rect = anchorRect(model, anchorId, environment);
    const double total = size.width() * size.height();
    return total > 0 ? (rect.width() * rect.height()) / total : 0.0;
}

QStringList typesIn(const WorkspaceModel &model, const QString &anchorId)
{
    QStringList types;
    const AnchorRecord *anchor = model.anchor(anchorId);
    if (!anchor) {
        return types;
    }
    for (const QString &viewId : anchor->views) {
        if (const ViewRecord *view = model.view(viewId)) {
            if (!types.contains(view->typeId)) {
                types.append(view->typeId);
            }
        }
    }
    if (types.isEmpty()) {
        types = anchor->roles;
    }
    return types;
}

QString kindWord(const QString &typeId)
{
    if (typeId == ViewTypes::Editor) {
        return QStringLiteral("code");
    }
    if (typeId == ViewTypes::Preview) {
        return QStringLiteral("document");
    }
    if (typeId == ViewTypes::Chat) {
        return QStringLiteral("chat");
    }
    if (typeId == ViewTypes::Terminal) {
        return QStringLiteral("terminal");
    }
    if (typeId == ViewTypes::Diff) {
        return QStringLiteral("diff");
    }
    if (typeId == ViewTypes::Welcome) {
        return QStringLiteral("welcome");
    }
    return typeId;
}

QString viewLabel(const ViewRecord &view)
{
    if (!view.title.isEmpty()) {
        return view.title;
    }
    const QString name = view.resource.details.value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
        return name;
    }
    const QString path = view.resource.details.value(QStringLiteral("path")).toString();
    return path.isEmpty() ? kindWord(view.typeId) : path;
}

// Where an anchor sits in its window, in words.
QString positionOf(const WorkspaceModel &model, const QString &anchorId, const Environment &environment)
{
    const QString surfaceId = model.surfaceOfAnchor(anchorId);
    const QSizeF size = surfaceSize(environment, surfaceId);
    const QRectF rect = anchorRect(model, anchorId, environment);

    if (!rect.isValid()) {
        return QStringLiteral("hidden");
    }

    const bool fullWidth = rect.width() >= size.width() * 0.9;
    const bool fullHeight = rect.height() >= size.height() * 0.9;
    if (fullWidth && fullHeight) {
        return QStringLiteral("whole window");
    }

    const double cx = rect.center().x() / size.width();
    const double cy = rect.center().y() / size.height();
    const QString horizontal = fullWidth ? QString() : cx < 0.4 ? QStringLiteral("left") : cx > 0.6 ? QStringLiteral("right") : QStringLiteral("center");
    const QString vertical = fullHeight ? QString() : cy < 0.4 ? QStringLiteral("top") : cy > 0.6 ? QStringLiteral("bottom") : QString();

    if (horizontal.isEmpty()) {
        return vertical.isEmpty() ? QStringLiteral("center") : vertical;
    }
    return vertical.isEmpty() ? horizontal : vertical + QLatin1Char(' ') + horizontal;
}

bool touchesEdge(const QRectF &rect, const QSizeF &size, Edge edge)
{
    constexpr double tolerance = 4.0;
    switch (edge) {
    case Edge::Left: return rect.left() <= tolerance;
    case Edge::Right: return rect.right() >= size.width() - tolerance;
    case Edge::Top: return rect.top() <= tolerance;
    case Edge::Bottom: return rect.bottom() >= size.height() - tolerance;
    case Edge::Center: return false;
    }
    return false;
}

QString largestAnchor(const WorkspaceModel &model, const QString &surfaceId, const Environment &environment)
{
    QString best;
    double bestShare = -1;
    for (const QString &anchorId : model.anchorIds(surfaceId)) {
        const double share = shareOf(model, anchorId, environment);
        if (share > bestShare) {
            bestShare = share;
            best = anchorId;
        }
    }
    return best;
}

// The anchor that holds the most code: where "the editor" is.
QString mainEditorAnchor(const WorkspaceModel &model, const Environment &environment)
{
    QString best;
    double bestScore = -1;
    for (const QString &anchorId : model.anchorIds(model.primarySurfaceId())) {
        const QStringList types = typesIn(model, anchorId);
        double score = shareOf(model, anchorId, environment);
        if (types.contains(ViewTypes::Editor)) {
            score += 2;
        } else if (types.contains(ViewTypes::Preview) || types.contains(ViewTypes::Welcome) || types.isEmpty()) {
            score += 1;
        }
        if (score > bestScore) {
            bestScore = score;
            best = anchorId;
        }
    }
    return best;
}

// Could a view of `typeId` sit in this anchor without crowding something
// unrelated? Empty anchors and anchors already showing that family can.
bool suits(const WorkspaceModel &model, const QString &anchorId, const QString &typeId)
{
    const AnchorRecord *anchor = model.anchor(anchorId);
    if (!anchor) {
        return false;
    }
    if (anchor->views.isEmpty()) {
        return anchor->roles.isEmpty() || anchor->roles.contains(typeId)
            || (isDocumentFamily(typeId) && std::any_of(anchor->roles.cbegin(), anchor->roles.cend(), isDocumentFamily));
    }
    for (const QString &held : typesIn(model, anchorId)) {
        if (held == typeId || (isDocumentFamily(held) && isDocumentFamily(typeId))) {
            return true;
        }
    }
    return false;
}

bool subtreeLocked(const WorkspaceModel &model, const QString &nodeId)
{
    const SplitNode *node = model.node(nodeId);
    if (!node) {
        return false;
    }
    if (node->leaf) {
        const AnchorRecord *anchor = model.anchor(node->anchorId);
        return anchor && anchor->layoutLocked;
    }
    for (const QString &child : node->children) {
        if (subtreeLocked(model, child)) {
            return true;
        }
    }
    return false;
}

// ---- resolving descriptions ----------------------------------------------

AreaMatch matchArea(const WorkspaceModel &model, const QString &description, const Environment &environment)
{
    AreaMatch best;
    const QString raw = description.trimmed();
    if (raw.isEmpty()) {
        return best;
    }

    if (model.anchor(raw)) {
        return {raw, {}, 1000};
    }
    if (model.view(raw)) {
        return {model.anchorOfView(raw), raw, 1000};
    }

    const QString text = raw.toLower();
    const QString core = stripFiller(text);
    Edge side = Edge::Center;
    const bool hasSide = parseSide(text, &side);
    const bool wantsMain = mentionsAny(text, {QStringLiteral("main"), QStringLiteral("big"), QStringLiteral("biggest"), QStringLiteral("large"), QStringLiteral("largest"), QStringLiteral("primary"), QStringLiteral("center"), QStringLiteral("middle")});
    const bool wantsSide = mentionsAny(text, {QStringLiteral("side"), QStringLiteral("sidebar")});
    const bool wantsHere = mentionsAny(text, hereWords());

    for (const QString &surfaceId : model.surfaceIds()) {
        const QSizeF size = surfaceSize(environment, surfaceId);
        const LayoutGeometry geometry = geometryOf(model, surfaceId, environment);
        const QString largest = largestAnchor(model, surfaceId, environment);

        for (const QString &anchorId : model.anchorIds(surfaceId)) {
            const AnchorRecord *anchor = model.anchor(anchorId);
            const QRectF rect = geometry.anchors.value(anchorId);
            const QStringList types = typesIn(model, anchorId);
            const double share = size.width() * size.height() > 0 ? rect.width() * rect.height() / (size.width() * size.height()) : 0;
            int score = 0;
            QString viewId;

            const QString name = anchor->name.toLower();
            const QString display = model.anchorDisplayName(anchorId).toLower();
            if (!name.isEmpty() && (name == text || name == core)) {
                score = std::max(score, 800);
            } else if (display == text || display == core) {
                score = std::max(score, 700);
            } else if (!name.isEmpty() && core.size() >= 3 && (name.contains(core) || core.contains(name))) {
                score = std::max(score, 500);
            }

            for (const QString &id : anchor->views) {
                const ViewRecord *view = model.view(id);
                if (!view) {
                    continue;
                }
                const QString label = viewLabel(*view).toLower();
                const QString path = view->resource.details.value(QStringLiteral("path")).toString().toLower();
                int viewScore = 0;
                if (label == text || label == core || (!path.isEmpty() && (path == text || path == core))) {
                    viewScore = 600;
                } else if (core.size() >= 4 && (label.contains(core) || (!path.isEmpty() && path.contains(core)))) {
                    viewScore = 420;
                }
                if (viewScore > 0 && viewScore + (view->id == anchor->activeViewId ? 5 : 0) > score) {
                    score = viewScore + (view->id == anchor->activeViewId ? 5 : 0);
                    viewId = id;
                }
            }

            int content = 0;
            if (mentionsAny(text, chatWords()) && types.contains(ViewTypes::Chat)) {
                content = 300;
            }
            if (mentionsAny(text, terminalWords()) && types.contains(ViewTypes::Terminal)) {
                content = std::max(content, 300);
            }
            if (mentionsAny(text, diffWords()) && types.contains(ViewTypes::Diff)) {
                content = std::max(content, 300);
            }
            if (mentionsAny(text, docWords())) {
                if (mentionsAny(name, docWords())) {
                    content = std::max(content, 380);
                } else if (types.contains(ViewTypes::Preview) && !types.contains(ViewTypes::Editor)) {
                    content = std::max(content, 260);
                } else if (types.contains(ViewTypes::Preview)) {
                    content = std::max(content, 200);
                }
            }
            if (mentionsAny(text, codeWords()) && types.contains(ViewTypes::Editor)) {
                content = std::max(content, 300);
            }

            int position = 0;
            if (hasSide && touchesEdge(rect, size, side)) {
                position = 200;
                // The anchor most to that side wins among several.
                const double lean = side == Edge::Right ? rect.center().x() / size.width()
                    : side == Edge::Left ? 1 - rect.center().x() / size.width()
                    : side == Edge::Bottom ? rect.center().y() / size.height()
                    : 1 - rect.center().y() / size.height();
                position += static_cast<int>(lean * 40);
            }
            if (wantsSide && anchorId != largest && (touchesEdge(rect, size, Edge::Left) || touchesEdge(rect, size, Edge::Right))) {
                position = std::max(position, 150);
            }
            if (wantsMain && anchorId == largest) {
                position = std::max(position, 250);
            }
            if (wantsHere && model.focusedAnchorId() == anchorId) {
                score = std::max(score, 900);
            }

            // Describing both what and where ("the docs on the right")
            // counts both; on its own either one works.
            if (content > 0 && position > 0) {
                score = std::max(score, content + position);
            } else {
                score = std::max(score, std::max(content, position));
            }

            if (score > 0) {
                // Among equals, the bigger and the more recently used.
                score += static_cast<int>(share * 30);
                const qsizetype recent = environment.recentAnchors.indexOf(anchorId);
                if (recent >= 0) {
                    score += std::max<int>(0, 10 - static_cast<int>(recent) * 2);
                }
            }

            if (score > best.score) {
                best = {anchorId, viewId, score};
            }
        }
    }

    // Weak matches (only "bigger and recent") don't count.
    return best.score >= 150 ? best : AreaMatch{};
}

QString notFoundMessage(const WorkspaceModel &model, const QString &description)
{
    QStringList names;
    for (const QString &anchorId : model.anchorIds()) {
        names.append(QStringLiteral("“%1”").arg(model.anchorDisplayName(anchorId)));
    }
    return QStringLiteral("There's no area matching “%1”. The workspace has %2.")
        .arg(description, names.isEmpty() ? QStringLiteral("no panels") : names.join(QStringLiteral(", ")));
}

// ---- performing ------------------------------------------------------------

class Run
{
public:
    Run(WorkspaceModel &work, const Environment &environment, Outcome &outcome)
        : m_work(work)
        , m_env(environment)
        , m_out(outcome)
    {
    }

    bool show(const QVariantMap &request);
    bool focus(const QVariantMap &request);
    bool resize(const QVariantMap &request);
    bool move(const QVariantMap &request);
    bool keep(const QVariantMap &request);
    bool label(const QVariantMap &request);

    QString focusTarget;

private:
    bool fail(const QString &code, const QString &message)
    {
        m_out.ok = false;
        m_out.code = code;
        m_out.message = message;
        return false;
    }

    bool failWith(const OpResult &result)
    {
        return fail(result.code, result.message);
    }

    bool resolve(const QString &description, AreaMatch *match)
    {
        *match = matchArea(m_work, description, m_env);
        if (!match->isValid()) {
            return fail(QStringLiteral("NOT_FOUND"), notFoundMessage(m_work, description));
        }
        return true;
    }

    QString destinationFor(const QString &typeId, const ResourceRef &resource, const QString &where, const QString &purpose);
    QString newAnchorBeside(const QString &anchorId, Edge edge, double share);
    QString newAnchorAtEdge(const QString &surfaceId, Edge edge, double share);
    void giveShare(const QString &anchorId, double share);
    bool conversationIsDocked() const;

    WorkspaceModel &m_work;
    const Environment &m_env;
    Outcome &m_out;
    QString m_destination; // shared by the items of one show
};

bool Run::conversationIsDocked() const
{
    for (const QString &anchorId : m_work.anchorIds()) {
        if (typesIn(m_work, anchorId).contains(ViewTypes::Chat) && !m_work.anchor(anchorId)->views.isEmpty()) {
            return true;
        }
    }
    return false;
}

void Run::giveShare(const QString &anchorId, double share)
{
    const QString leaf = m_work.leafOfAnchor(anchorId);
    const SplitNode *leafNode = m_work.node(leaf);
    if (!leafNode || leafNode->parentId.isEmpty()) {
        return;
    }
    const SplitNode *parent = m_work.node(leafNode->parentId);
    const qsizetype index = parent->children.indexOf(leaf);
    if (index < 0 || parent->children.size() < 2) {
        return;
    }
    QList<double> sizes = parent->sizes;
    const double others = 1.0 - sizes.value(index);
    if (others <= 0) {
        return;
    }
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        sizes[i] = i == index ? share : sizes.at(i) / others * (1.0 - share);
    }
    // A locked neighbour keeps its size; the panel then keeps the split's
    // default share, which is fine.
    m_work.setSplitSizes(parent->id, sizes);
}

QString Run::newAnchorBeside(const QString &anchorId, Edge edge, double share)
{
    const OpResult created = m_work.createAnchor(anchorId, edge);
    if (!created.ok) {
        failWith(created);
        return {};
    }
    m_out.createdAnchors.append(created.createdId);
    if (share > 0) {
        giveShare(created.createdId, share);
    }
    return created.createdId;
}

QString Run::newAnchorAtEdge(const QString &surfaceId, Edge edge, double share)
{
    const OpResult created = m_work.createAnchorAtSurfaceEdge(surfaceId, edge);
    if (!created.ok) {
        failWith(created);
        return {};
    }
    m_out.createdAnchors.append(created.createdId);
    if (share > 0) {
        giveShare(created.createdId, share);
    }
    return created.createdId;
}

QString Run::destinationFor(const QString &typeId, const ResourceRef &resource, const QString &where, const QString &purpose)
{
    const QString text = where.trimmed().toLower();
    const Origin origin = m_env.origin;
    Placement::Hints hints;
    hints.origin = origin;
    hints.recentAnchors = m_env.recentAnchors;

    auto receivable = [&](const QString &anchorId) {
        return Placement::canReceive(m_work, anchorId, origin);
    };

    // A panel the person set aside for this purpose comes first.
    if (text.isEmpty() && !purpose.trimmed().isEmpty()) {
        const QString wanted = purpose.trimmed().toLower();
        for (auto it = m_env.purposeAreas.cbegin(); it != m_env.purposeAreas.cend(); ++it) {
            const bool matches = wanted.contains(it.key()) || it.key().contains(wanted) || (isDocPurpose(wanted) && isDocPurpose(it.key()));
            if (matches && m_work.anchor(it.value()) && receivable(it.value())) {
                return it.value();
            }
        }
    }

    // Reference material goes to an area meant for it, else to the side.
    if (text.isEmpty() && isDocPurpose(purpose)) {
        const AreaMatch docs = matchArea(m_work, QStringLiteral("documentation"), m_env);
        if (docs.isValid() && docs.score >= 260 && receivable(docs.anchorId)) {
            return docs.anchorId;
        }
        if (!m_env.mayAddAreas) {
            return destinationFor(typeId, resource, {}, {});
        }
        return destinationFor(typeId, resource, QStringLiteral("right"), {});
    }

    if (text.isEmpty()) {
        const QString chosen = Placement::chooseAnchor(m_work, typeId, resource, hints);
        if (!receivable(chosen)) {
            fail(QStringLiteral("CONTENT_LOCKED"), QStringLiteral("Every panel that could show this has locked content."));
            return {};
        }
        return chosen;
    }

    if (mentionsAny(text, hereWords()) && !splitBeside(text, nullptr)) {
        const QString focused = m_work.focusedAnchorId();
        if (!receivable(focused)) {
            fail(QStringLiteral("CONTENT_LOCKED"), m_work.anchorDisplayName(focused) + QStringLiteral(" has locked content."));
            return {};
        }
        return focused;
    }

    if (mentionsAny(text, {QStringLiteral("new"), QStringLiteral("separate"), QStringLiteral("own"), QStringLiteral("split")})) {
        return newAnchorBeside(mainEditorAnchor(m_work, m_env), Edge::Right, 0.5);
    }

    QString other;
    if (splitBeside(text, &other)) {
        // The conversation lives in the bottom workbench unless a panel
        // shows it: then "beside the chat" is just "in the editor, with the
        // chat kept open".
        if (mentionsAny(other, chatWords()) && !conversationIsDocked()) {
            if (!m_env.commandSurfaceName.isEmpty()) {
                m_out.commandSurface = QStringLiteral("chat");
            }
            return destinationFor(typeId, resource, {}, purpose);
        }

        AreaMatch near;
        if (!resolve(other, &near)) {
            return {};
        }

        if (suits(m_work, near.anchorId, typeId) && receivable(near.anchorId)
            && !typesIn(m_work, near.anchorId).contains(ViewTypes::Chat)
            && !typesIn(m_work, near.anchorId).contains(ViewTypes::Terminal)) {
            // "Beside the editor" for more code: as tabs in the editor, not
            // a needless split.
            return near.anchorId;
        }

        const QString surfaceId = m_work.surfaceOfAnchor(near.anchorId);
        const QSizeF size = surfaceSize(m_env, surfaceId);
        for (Edge direction : {Edge::Right, Edge::Left, Edge::Bottom, Edge::Top}) {
            const QString neighbour = m_work.neighborAnchor(near.anchorId, direction, size, m_env.metrics);
            if (!neighbour.isEmpty() && receivable(neighbour) && suits(m_work, neighbour, typeId)) {
                return neighbour;
            }
        }

        Edge edge = Edge::Right;
        if (text.contains(QStringLiteral("left of"))) {
            edge = Edge::Left;
        } else if (text.contains(QStringLiteral("below")) || text.contains(QStringLiteral("under"))) {
            edge = Edge::Bottom;
        } else if (text.contains(QStringLiteral("above"))) {
            edge = Edge::Top;
        }
        return newAnchorBeside(near.anchorId, edge, 0.5);
    }

    Edge side = Edge::Center;
    if (parseSide(text, &side) || mentionsAny(text, {QStringLiteral("side"), QStringLiteral("sidebar")})) {
        if (side == Edge::Center) {
            side = Edge::Right;
        }

        // An area named for it ("the docs on the right") comes first.
        const AreaMatch named = matchArea(m_work, text, m_env);
        if (named.isValid() && named.score >= 500 && receivable(named.anchorId)) {
            return named.anchorId;
        }

        const QString surfaceId = m_work.surfaceOfAnchor(m_work.focusedAnchorId()).isEmpty()
            ? m_work.primarySurfaceId()
            : m_work.surfaceOfAnchor(m_work.focusedAnchorId());
        const QSizeF size = surfaceSize(m_env, surfaceId);
        const LayoutGeometry geometry = geometryOf(m_work, surfaceId, m_env);
        const QStringList anchors = m_work.anchorIds(surfaceId);
        // The main editor is never "the side", even when it is the only panel.
        const QString main = mainEditorAnchor(m_work, m_env);
        QString best;
        double bestLean = -1;

        for (const QString &anchorId : anchors) {
            const QRectF rect = geometry.anchors.value(anchorId);
            if (anchorId == main || !touchesEdge(rect, size, side) || !receivable(anchorId) || !suits(m_work, anchorId, typeId)) {
                continue;
            }
            const double lean = side == Edge::Right ? rect.center().x() : side == Edge::Left ? -rect.center().x()
                : side == Edge::Bottom ? rect.center().y() : -rect.center().y();
            if (best.isEmpty() || lean > bestLean) {
                best = anchorId;
                bestLean = lean;
            }
        }

        if (!best.isEmpty()) {
            return best;
        }
        return newAnchorAtEdge(surfaceId, side, m_env.sidePanelShare);
    }

    AreaMatch named;
    if (!resolve(where, &named)) {
        return {};
    }
    if (!receivable(named.anchorId)) {
        fail(
            QStringLiteral("CONTENT_LOCKED"),
            QStringLiteral("“%1” has locked content, so an agent can't add to it.").arg(m_work.anchorDisplayName(named.anchorId))
        );
        return {};
    }
    return named.anchorId;
}

bool Run::show(const QVariantMap &request)
{
    const QVariantList items = request.value(QStringLiteral("items")).toList();
    const QString where = request.value(QStringLiteral("where")).toString();
    const QString purpose = request.value(QStringLiteral("purpose")).toString();
    const bool explicitWhere = !where.trimmed().isEmpty() || isDocPurpose(purpose);

    if (items.isEmpty()) {
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("Nothing to show."));
    }

    QStringList openedTitles;
    QStringList revealedTitles;

    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();

        // The bottom Chat/Terminal workbench.
        const QString surface = item.value(QStringLiteral("surface")).toString();
        if (!surface.isEmpty()) {
            if (surface != QStringLiteral("chat") && surface != QStringLiteral("terminal")) {
                return fail(QStringLiteral("UNSUPPORTED"), QStringLiteral("The bottom panel shows the chat or the terminal."));
            }
            if (m_env.commandSurfaceName.isEmpty()) {
                return fail(QStringLiteral("UNSUPPORTED"), QStringLiteral("There's no bottom panel here."));
            }
            m_out.commandSurface = surface;
            m_out.notes.append(QStringLiteral("Kept the %1 open in %2").arg(surface == QStringLiteral("chat") ? QStringLiteral("conversation") : QStringLiteral("terminal"), m_env.commandSurfaceName));
            continue;
        }

        QString existing = item.value(QStringLiteral("viewId")).toString();
        ViewRecord record;

        if (!existing.isEmpty()) {
            if (!m_work.view(existing)) {
                return fail(QStringLiteral("NOT_FOUND"), QStringLiteral("That view is no longer open."));
            }
            record = *m_work.view(existing);
        } else {
            record.typeId = item.value(QStringLiteral("typeId")).toString();
            record.resource.kind = item.value(QStringLiteral("resource")).toMap().value(QStringLiteral("kind")).toString();
            const QVariantMap resource = item.value(QStringLiteral("resource")).toMap();
            record.resource.id = resource.value(QStringLiteral("id")).toString();
            record.resource.collectionId = resource.value(QStringLiteral("collectionId")).toString();
            record.resource.libraryId = resource.value(QStringLiteral("libraryId")).toString();
            record.resource.details = resource.value(QStringLiteral("details")).toMap();
            record.title = item.value(QStringLiteral("title")).toString();
            record.state = item.value(QStringLiteral("state")).toMap();

            const ViewTypeDescriptor *type = m_env.registry ? m_env.registry->find(record.typeId) : nullptr;
            if (m_env.registry && !type) {
                return fail(QStringLiteral("UNSUPPORTED"), QStringLiteral("Archivist can't show “%1” views.").arg(record.typeId));
            }
            if (record.typeId.isEmpty()) {
                return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("A view type is needed."));
            }
            // Files and documents are one family: the app opens library
            // documents in both editors and previews.
            const auto family = [](const QString &kind) {
                return kind == QLatin1String("file") ? QStringLiteral("document") : kind;
            };
            if (type && !type->resourceKind.isEmpty() && family(type->resourceKind) != family(record.resource.kind)) {
                return fail(
                    QStringLiteral("INVALID_ARGUMENT"),
                    QStringLiteral("A %1 view shows a %2, not a %3.").arg(type->displayName, type->resourceKind, record.resource.kind.isEmpty() ? QStringLiteral("nothing") : record.resource.kind)
                );
            }
            if (type) {
                record.stateVersion = type->stateVersion;
            }
            existing = Placement::existingView(m_work, record.typeId, record.resource, m_destination);
        }

        if (!existing.isEmpty()) {
            const QString owner = m_work.anchorOfView(existing);
            if (explicitWhere) {
                if (m_destination.isEmpty()) {
                    m_destination = destinationFor(record.typeId, record.resource, where, purpose);
                    if (m_destination.isEmpty()) {
                        return false;
                    }
                }
                if (owner != m_destination) {
                    const OpResult moved = m_work.moveView(existing, m_destination, -1, m_env.origin);
                    if (!moved.ok) {
                        // Can't take it out of a locked panel: show it where it is.
                        m_out.notes.append(QStringLiteral("Left %1 where it is (%2)").arg(viewLabel(record), moved.message));
                    }
                }
            }
            m_work.activateView(existing);
            m_out.revealed.append(existing);
            revealedTitles.append(viewLabel(record));
            if (focusTarget.isEmpty()) {
                focusTarget = m_work.anchorOfView(existing);
            }
            continue;
        }

        if (m_destination.isEmpty() || !suits(m_work, m_destination, record.typeId)) {
            const QString destination = destinationFor(record.typeId, record.resource, where, purpose);
            if (destination.isEmpty()) {
                return false;
            }
            m_destination = destination;
        }

        const OpResult opened = m_work.openView(record, m_destination, -1, true, m_env.origin);
        if (!opened.ok) {
            return failWith(opened);
        }
        m_out.opened.append(opened.createdId);
        openedTitles.append(viewLabel(record));
        if (focusTarget.isEmpty()) {
            focusTarget = m_destination;
        }
    }

    // A panel made for this request goes away again when what it shows is
    // closed (pinned panels stay).
    for (const QString &anchorId : std::as_const(m_out.createdAnchors)) {
        if (m_work.anchor(anchorId) && !m_work.anchor(anchorId)->views.isEmpty()) {
            m_work.setAnchorPinned(anchorId, false);
        }
    }

    // Show the first item, not the last, in each panel that received several.
    QSet<QString> shownAnchors;
    for (const QString &viewId : m_out.opened + m_out.revealed) {
        const QString anchorId = m_work.anchorOfView(viewId);
        if (!shownAnchors.contains(anchorId)) {
            shownAnchors.insert(anchorId);
            m_work.activateView(viewId);
        }
    }

    if (!openedTitles.isEmpty()) {
        m_out.notes.append(QStringLiteral("Opened %1 in “%2”").arg(openedTitles.join(QStringLiteral(", ")), m_work.anchorDisplayName(m_destination)));
    }
    if (!revealedTitles.isEmpty()) {
        m_out.notes.append(QStringLiteral("Brought forward %1 (already open)").arg(revealedTitles.join(QStringLiteral(", "))));
    }
    return true;
}

bool Run::focus(const QVariantMap &request)
{
    AreaMatch match;
    if (!resolve(request.value(QStringLiteral("target")).toString(), &match)) {
        return false;
    }
    if (!match.viewId.isEmpty()) {
        m_work.activateView(match.viewId);
    }
    focusTarget = match.anchorId;
    m_out.notes.append(QStringLiteral("Brought “%1” forward").arg(m_work.anchorDisplayName(match.anchorId)));
    return true;
}

bool Run::resize(const QVariantMap &request)
{
    AreaMatch match;
    if (!resolve(request.value(QStringLiteral("target")).toString(), &match)) {
        return false;
    }

    const QString anchorId = match.anchorId;
    const QString name = m_work.anchorDisplayName(anchorId);
    const QString change = request.value(QStringLiteral("change")).toString().trimmed().toLower();
    const QString surfaceId = m_work.surfaceOfAnchor(anchorId);

    if (change == QStringLiteral("maximize") || change == QStringLiteral("maximise") || change == QStringLiteral("full")) {
        const OpResult result = m_work.maximizeAnchor(anchorId);
        if (!result.ok) {
            return failWith(result);
        }
        m_out.notes.append(QStringLiteral("Filled the window with “%1” (the rest is one click away)").arg(name));
        return true;
    }

    if (change == QStringLiteral("restore") || change == QStringLiteral("unmaximize") || change == QStringLiteral("normal")) {
        const OpResult result = m_work.restoreAnchor(surfaceId);
        if (!result.ok) {
            return failWith(result);
        }
        m_out.notes.append(QStringLiteral("Showed every panel again"));
        return true;
    }

    const AnchorCapabilities capabilities = m_work.anchorCapabilities(anchorId, m_env.origin);
    if (!capabilities.canResize) {
        return fail(QStringLiteral("LAYOUT_LOCKED"), capabilities.reasons.value(QStringLiteral("resize"), name + QStringLiteral(" can't be resized.")));
    }

    // A maximized panel already has the whole window.
    if (const SurfaceRecord *surface = m_work.surface(surfaceId); surface && !surface->maximizedAnchorId.isEmpty()) {
        m_work.restoreAnchor(surfaceId);
    }

    if (change == QStringLiteral("equal") || change == QStringLiteral("even") || change == QStringLiteral("balance")) {
        const SplitNode *leaf = m_work.node(m_work.leafOfAnchor(anchorId));
        if (!leaf || leaf->parentId.isEmpty()) {
            return fail(QStringLiteral("INVALID_ARGUMENT"), name + QStringLiteral(" has no neighbours to share space with."));
        }
        const OpResult result = m_work.equalizeSplit(leaf->parentId);
        if (!result.ok) {
            return failWith(result);
        }
        m_out.notes.append(QStringLiteral("Evened out “%1” and its neighbours").arg(name));
        return true;
    }

    bool fraction = false;
    const double requested = change.toDouble(&fraction);
    const bool larger = change == QStringLiteral("larger") || change == QStringLiteral("bigger") || change == QStringLiteral("grow")
        || change == QStringLiteral("wider") || change == QStringLiteral("taller");
    const bool smaller = change == QStringLiteral("smaller") || change == QStringLiteral("shrink") || change == QStringLiteral("narrower")
        || change == QStringLiteral("shorter");

    if (!larger && !smaller && !(fraction && requested > 0.05 && requested < 0.95)) {
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("Say larger, smaller, maximize, restore, equal, or a share like 0.6."));
    }

    const double before = shareOf(m_work, anchorId, m_env);
    const bool keepsRoomForMain = m_env.largestSideShare < 1.0 && anchorId != mainEditorAnchor(m_work, m_env);
    QString node = m_work.leafOfAnchor(anchorId);
    int levels = 0;

    while (levels < 2) {
        const SplitNode *child = m_work.node(node);
        if (!child || child->parentId.isEmpty()) {
            break;
        }
        const SplitNode parent = *m_work.node(child->parentId);
        const qsizetype index = parent.children.indexOf(node);
        QList<double> sizes = parent.sizes;
        const double current = sizes.value(index);
        double target = fraction ? requested
            : larger ? std::min(m_env.maximumShare, std::max(current + 0.12, current * 1.3))
            : std::max(m_env.minimumShare, std::min(current - 0.1, current * 0.7));
        // "Keep my main editor large": others grow only so far, unless a
        // size was asked for.
        if (larger && !fraction && keepsRoomForMain) {
            target = std::max(current, std::min(target, m_env.largestSideShare));
        }

        // Only unlocked siblings give or take space.
        QList<qsizetype> free;
        double freeTotal = 0;
        for (qsizetype i = 0; i < parent.children.size(); ++i) {
            if (i == index) {
                continue;
            }
            if (!subtreeLocked(m_work, parent.children.at(i))) {
                free.append(i);
                freeTotal += sizes.at(i);
            }
        }

        if (free.isEmpty() || freeTotal <= 0) {
            node = parent.id;
            ++levels;
            continue;
        }

        const double floor = m_env.minimumShare;
        const double room = freeTotal - floor * free.size();
        target = std::min(target, current + std::max(0.0, room));
        target = std::max(target, floor);
        const double delta = target - current;

        if (std::abs(delta) < 0.01) {
            node = parent.id;
            ++levels;
            continue;
        }

        for (qsizetype i : std::as_const(free)) {
            sizes[i] = sizes.at(i) - delta * (sizes.at(i) / freeTotal);
        }
        sizes[index] = target;

        const OpResult result = m_work.setSplitSizes(parent.id, sizes);
        if (!result.ok) {
            return failWith(result);
        }

        if (fraction) {
            break;
        }
        node = parent.id;
        ++levels;
    }

    const double after = shareOf(m_work, anchorId, m_env);
    if (std::abs(after - before) < 0.005) {
        return fail(
            QStringLiteral("NO_ROOM"),
            larger && keepsRoomForMain ? QStringLiteral("“%1” is as large as it gets while the main editor stays large (the person's preference). Give a size to go further.").arg(name)
            : larger ? QStringLiteral("“%1” can't get any larger here.").arg(name)
                     : QStringLiteral("“%1” can't change size here.").arg(name)
        );
    }

    m_out.notes.append(QStringLiteral("Made “%1” %2 (%3% → %4% of the window)")
                           .arg(name, after > before ? QStringLiteral("larger") : QStringLiteral("smaller"))
                           .arg(qRound(before * 100))
                           .arg(qRound(after * 100)));
    return true;
}

bool Run::move(const QVariantMap &request)
{
    AreaMatch match;
    if (!resolve(request.value(QStringLiteral("target")).toString(), &match)) {
        return false;
    }

    const QString to = request.value(QStringLiteral("to")).toString().trimmed().toLower();
    const QString anchorId = match.anchorId;
    const QString name = m_work.anchorDisplayName(anchorId);
    const AnchorRecord *anchor = m_work.anchor(anchorId);
    const bool singleView = !match.viewId.isEmpty() && anchor->views.size() > 1;

    if (to.isEmpty()) {
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("Say where it should go."));
    }

    QString other;
    if (splitBeside(to, &other)) {
        AreaMatch near;
        if (!resolve(other, &near)) {
            return false;
        }
        if (near.anchorId == anchorId) {
            return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("That's the same panel."));
        }
        Edge edge = Edge::Right;
        if (to.contains(QStringLiteral("left of"))) {
            edge = Edge::Left;
        } else if (to.contains(QStringLiteral("below")) || to.contains(QStringLiteral("under"))) {
            edge = Edge::Bottom;
        } else if (to.contains(QStringLiteral("above"))) {
            edge = Edge::Top;
        }
        const OpResult result = singleView
            ? m_work.splitView(match.viewId, near.anchorId, edge, m_env.origin)
            : m_work.moveAnchor(anchorId, near.anchorId, edge, m_env.origin);
        if (!result.ok) {
            return failWith(result);
        }
        m_out.notes.append(QStringLiteral("Moved “%1” beside “%2”").arg(name, m_work.anchorDisplayName(near.anchorId)));
        return true;
    }

    Edge side = Edge::Center;
    if (!parseSide(to, &side)) {
        // An area name: merge the view (or the panel's tabs) into it.
        AreaMatch into;
        if (!resolve(to, &into)) {
            return false;
        }
        if (into.anchorId == anchorId) {
            return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("That's the same panel."));
        }
        const OpResult result = singleView
            ? m_work.moveView(match.viewId, into.anchorId, -1, m_env.origin)
            : m_work.mergeAnchor(anchorId, into.anchorId, m_env.origin);
        if (!result.ok) {
            return failWith(result);
        }
        m_out.notes.append(QStringLiteral("Moved “%1” into “%2”").arg(name, m_work.anchorDisplayName(into.anchorId)));
        return true;
    }

    const QString surfaceId = m_work.surfaceOfAnchor(anchorId);
    const QSizeF size = surfaceSize(m_env, surfaceId);
    if (!singleView && touchesEdge(anchorRect(m_work, anchorId, m_env), size, side) && m_work.anchorIds(surfaceId).size() > 1) {
        m_out.notes.append(QStringLiteral("“%1” is already on the %2").arg(name, edgeName(side)));
        return true;
    }

    const AnchorCapabilities capabilities = m_work.anchorCapabilities(anchorId, m_env.origin);
    if (!singleView && !capabilities.canMove) {
        return fail(QStringLiteral("LAYOUT_LOCKED"), capabilities.reasons.value(QStringLiteral("move"), name + QStringLiteral(" can't move.")));
    }

    const OpResult result = singleView
        ? m_work.dockViewAtSurfaceEdge(match.viewId, surfaceId, side, m_env.origin)
        : m_work.dockAnchorAtSurfaceEdge(anchorId, surfaceId, side);
    if (!result.ok) {
        return failWith(result);
    }
    const QString moved = singleView ? m_work.anchorOfView(match.viewId) : anchorId;
    giveShare(moved, m_env.sidePanelShare);
    if (singleView && m_work.anchor(moved)) {
        m_work.setAnchorPinned(moved, false);
        m_out.createdAnchors.append(moved);
    }
    m_out.notes.append(QStringLiteral("Moved “%1” to the %2").arg(singleView ? viewLabel(*m_work.view(match.viewId)) : name, edgeName(side)));
    return true;
}

bool Run::keep(const QVariantMap &request)
{
    AreaMatch match;
    if (!resolve(request.value(QStringLiteral("target")).toString(), &match)) {
        return false;
    }
    const OpResult result = m_work.setAnchorLayoutLocked(match.anchorId, true, m_env.origin);
    if (!result.ok) {
        return failWith(result);
    }
    m_out.notes.append(QStringLiteral("Locked “%1” in place (only you can unlock it)").arg(m_work.anchorDisplayName(match.anchorId)));
    return true;
}

bool Run::label(const QVariantMap &request)
{
    AreaMatch match;
    if (!resolve(request.value(QStringLiteral("target")).toString(), &match)) {
        return false;
    }
    const QString name = request.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("A name is needed."));
    }
    const QString before = m_work.anchorDisplayName(match.anchorId);
    const OpResult result = m_work.renameAnchor(match.anchorId, name, m_env.origin);
    if (!result.ok) {
        return failWith(result);
    }
    m_out.notes.append(QStringLiteral("Named “%1” “%2”").arg(before, m_work.anchorDisplayName(match.anchorId)));
    return true;
}
} // namespace

QStringList actions()
{
    return {
        QStringLiteral("describe"), QStringLiteral("show"), QStringLiteral("focus"), QStringLiteral("resize"),
        QStringLiteral("move"), QStringLiteral("keep"), QStringLiteral("label"),
    };
}

AreaMatch resolveArea(const WorkspaceModel &model, const QString &description, const Environment &environment)
{
    return matchArea(model, description, environment);
}

QString areaLabel(const WorkspaceModel &model, const QString &anchorId)
{
    return model.anchorDisplayName(anchorId);
}

QVariantMap describe(const WorkspaceModel &model, const Environment &environment)
{
    QVariantList areas;
    bool chatDocked = false;

    for (const QString &surfaceId : model.surfaceIds()) {
        const SurfaceRecord *surface = model.surface(surfaceId);
        const bool primary = surface && surface->primary;

        for (const QString &anchorId : model.anchorIds(surfaceId)) {
            const AnchorRecord *anchor = model.anchor(anchorId);
            QVariantList shows;
            QStringList kinds;

            for (const QString &viewId : anchor->views) {
                const ViewRecord *view = model.view(viewId);
                if (!view) {
                    continue;
                }
                QVariantMap entry{
                    {QStringLiteral("viewId"), view->id},
                    {QStringLiteral("title"), viewLabel(*view)},
                    {QStringLiteral("kind"), kindWord(view->typeId)},
                };
                const QString path = view->resource.details.value(QStringLiteral("path")).toString();
                if (!path.isEmpty()) {
                    entry.insert(QStringLiteral("path"), path);
                }
                if (!view->resource.libraryId.isEmpty()) {
                    entry.insert(QStringLiteral("libraryId"), view->resource.libraryId);
                }
                if (view->id == anchor->activeViewId) {
                    entry.insert(QStringLiteral("visible"), true);
                }
                shows.append(entry);
                if (!kinds.contains(kindWord(view->typeId))) {
                    kinds.append(kindWord(view->typeId));
                }
                chatDocked = chatDocked || view->typeId == ViewTypes::Chat;
            }

            QVariantMap area{
                {QStringLiteral("area"), model.anchorDisplayName(anchorId)},
                {QStringLiteral("id"), anchorId},
                {QStringLiteral("window"), primary ? QStringLiteral("main") : QStringLiteral("floating")},
                {QStringLiteral("position"), positionOf(model, anchorId, environment)},
                {QStringLiteral("sizePercent"), qRound(shareOf(model, anchorId, environment) * 100)},
                {QStringLiteral("shows"), shows},
                {QStringLiteral("kinds"), kinds.isEmpty() ? QVariant(QStringLiteral("empty")) : QVariant(kinds)},
            };

            if (model.focusedAnchorId() == anchorId) {
                area.insert(QStringLiteral("focused"), true);
            }
            if (surface && surface->maximizedAnchorId == anchorId) {
                area.insert(QStringLiteral("maximized"), true);
            }
            QStringList locks;
            if (anchor->layoutLocked) {
                locks.append(QStringLiteral("layout locked: can't be moved or resized"));
            }
            if (anchor->contentLocked) {
                locks.append(QStringLiteral("content locked: you can't add to or change what it shows"));
            }
            if (!locks.isEmpty()) {
                area.insert(QStringLiteral("locked"), locks.join(QStringLiteral("; ")));
            }
            if (anchor->switchable) {
                area.insert(QStringLiteral("shelf"), true);
            }
            if (!anchor->libraryId.isEmpty()) {
                area.insert(QStringLiteral("worktree"), anchor->libraryId);
            }
            areas.append(area);
        }
    }

    QStringList types;
    if (environment.registry) {
        for (const ViewTypeDescriptor &type : environment.registry->types()) {
            types.append(type.typeId);
        }
    }

    QVariantMap result{
        {QStringLiteral("areas"), areas},
        {QStringLiteral("viewTypes"), types},
    };
    if (!chatDocked && !environment.commandSurfaceName.isEmpty()) {
        result.insert(QStringLiteral("conversation"), QStringLiteral("in %1, with the terminal").arg(environment.commandSurfaceName));
    }
    return result;
}

Outcome perform(WorkspaceModel &model, const QVariantMap &request, const Environment &environment)
{
    const QString action = request.value(QStringLiteral("action")).toString();

    if (action == QStringLiteral("describe")) {
        Outcome outcome;
        outcome.data = describe(model, environment);
        return outcome;
    }

    if (!actions().contains(action)) {
        return Outcome::failure(QStringLiteral("UNSUPPORTED"), QStringLiteral("Archivist can't do “%1” with the workspace.").arg(action));
    }

    WorkspaceModel work = model;
    const QString focusedBefore = model.focusedAnchorId();
    const int revisionBefore = model.revision();
    Outcome outcome;
    Run run(work, environment, outcome);

    bool ok = false;
    if (action == QStringLiteral("show")) {
        ok = run.show(request);
    } else if (action == QStringLiteral("focus")) {
        ok = run.focus(request);
    } else if (action == QStringLiteral("resize")) {
        ok = run.resize(request);
    } else if (action == QStringLiteral("move")) {
        ok = run.move(request);
    } else if (action == QStringLiteral("keep")) {
        ok = run.keep(request);
    } else if (action == QStringLiteral("label")) {
        ok = run.label(request);
    }

    if (!ok) {
        Outcome failed = Outcome::failure(outcome.code, outcome.message);
        return failed;
    }

    // Keyboard focus stays with the person unless they asked to go there.
    const bool focusRequested = action == QStringLiteral("focus") || request.value(QStringLiteral("focus")).toBool();
    if (focusRequested && environment.mayMoveFocus && work.anchor(run.focusTarget)) {
        work.focusAnchor(run.focusTarget);
        outcome.focusAnchorId = run.focusTarget;
    } else if (work.anchor(focusedBefore)) {
        work.focusAnchor(focusedBefore);
    }

    const QStringList problems = work.validate();
    if (!problems.isEmpty()) {
        return Outcome::failure(QStringLiteral("INVARIANT"), problems.join(QStringLiteral("; ")));
    }

    outcome.changed = work.revision() != revisionBefore;
    if (outcome.changed) {
        model = work;
    }
    return outcome;
}
} // namespace Workspace::Intents
