#include "Core/Domains/Workspace/workspace_compose.h"

#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_codec.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <functional>

namespace Workspace::Compose
{
namespace
{
const QString Theater = QStringLiteral("archivist.agent-theater");

// Versioned recipes. A tree node is {"slot": name, "weight": w} or
// {"row" | "column": [nodes], "weight": w}; weights are shares of the
// parent. Templates are starting points: the caller fills their slots and
// may change weights or add slots.
const char *const kTemplates = R"json([
  {
    "id": "focus_references", "name": "Focus + References", "version": 1,
    "description": "One large panel for the work, reference material at the side.",
    "slots": [{"name": "Focus", "holds": ["code"]}, {"name": "References", "holds": ["docs"]}],
    "tree": {"row": [{"slot": "Focus", "weight": 0.68}, {"slot": "References", "weight": 0.32}]}
  },
  {
    "id": "chat_code", "name": "Chat + Code", "version": 1,
    "description": "The code beside a conversation.",
    "slots": [{"name": "Code", "holds": ["code"]}, {"name": "Chat", "holds": ["chat"]}],
    "tree": {"row": [{"slot": "Code", "weight": 0.62}, {"slot": "Chat", "weight": 0.38}]}
  },
  {
    "id": "code_diff", "name": "Code + Diff", "version": 1,
    "description": "A file next to the changes under review.",
    "slots": [{"name": "Code", "holds": ["code"]}, {"name": "Changes", "holds": ["diff"]}],
    "tree": {"row": [{"slot": "Code", "weight": 0.5}, {"slot": "Changes", "weight": 0.5}]}
  },
  {
    "id": "research_desk", "name": "Research Desk", "version": 1,
    "description": "Sources on the left, notes and references stacked on the right.",
    "slots": [{"name": "Sources", "holds": ["docs"]}, {"name": "Notes", "holds": ["code"]}, {"name": "Reference", "holds": ["preview"]}],
    "tree": {"row": [{"slot": "Sources", "weight": 0.52},
                     {"column": [{"slot": "Notes", "weight": 0.6}, {"slot": "Reference", "weight": 0.4}], "weight": 0.48}]}
  },
  {
    "id": "agent_overview", "name": "Agent Overview", "version": 1,
    "description": "Agent conversations across the top, their terminal and changes below.",
    "slots": [{"name": "Agents", "holds": ["agents"]}, {"name": "Terminal", "holds": ["terminal"]}, {"name": "Changes", "holds": ["diff", "code"]}],
    "tree": {"column": [{"slot": "Agents", "weight": 0.6},
                        {"row": [{"slot": "Terminal", "weight": 0.5}, {"slot": "Changes", "weight": 0.5}], "weight": 0.4}]}
  }
])json";

QStringList typesFor(const QString &word)
{
    using namespace ViewTypes;
    const QString w = word.trimmed().toLower();
    if (w == QStringLiteral("code") || w == QStringLiteral("editor") || w == QStringLiteral("files") || w == QStringLiteral("notes")) {
        return {Editor, Preview};
    }
    if (w == QStringLiteral("docs") || w == QStringLiteral("documentation") || w == QStringLiteral("reference") || w == QStringLiteral("references")) {
        return {Preview, Editor};
    }
    if (w == QStringLiteral("preview")) {
        return {Preview};
    }
    if (w == QStringLiteral("chat")) {
        return {Chat};
    }
    if (w == QStringLiteral("agents")) {
        return {Chat, Theater};
    }
    if (w == QStringLiteral("terminal")) {
        return {Terminal};
    }
    if (w == QStringLiteral("diff") || w == QStringLiteral("changes") || w == QStringLiteral("review")) {
        return {Diff};
    }
    return {};
}

QString slug(const QString &name)
{
    QString out;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber()) {
            out.append(c);
        } else if (!out.endsWith(QLatin1Char('-'))) {
            out.append(QLatin1Char('-'));
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    return out.isEmpty() ? QStringLiteral("panel") : out.left(24);
}

struct Node
{
    QString slot;            // leaf
    QString orientation;     // "row" | "column"
    QList<Node> children;
    // Room relative to siblings, around 1. 0 on a split: as much as its
    // largest child asks for.
    double weight = 1;
    QList<double> sizes;     // solved shares of the children
};

Node readNode(const QJsonObject &object)
{
    Node node;
    node.weight = object.value(QStringLiteral("weight")).toDouble(1);
    if (object.contains(QStringLiteral("slot"))) {
        node.slot = object.value(QStringLiteral("slot")).toString();
        return node;
    }
    node.orientation = object.contains(QStringLiteral("row")) ? QStringLiteral("row") : QStringLiteral("column");
    const QJsonArray children = object.value(node.orientation).toArray();
    for (const QJsonValue &child : children) {
        Node read = readNode(child.toObject());
        // Recipes give shares (0.68, 0.32); weights are around 1.
        read.weight *= children.size();
        node.children.append(read);
    }
    return node;
}

// ---- the geometry solver ---------------------------------------------------

struct Limits
{
    double minWidth = kMinimumPanelWidth;
    double minHeight = kMinimumPanelHeight;
    double splitter = 6;
};

double effectiveWeight(const Node &node, const QHash<QString, double> &asked)
{
    if (node.orientation.isEmpty()) {
        const double given = asked.value(node.slot);
        return given > 0 ? given : (node.weight > 0 ? node.weight : 1);
    }
    double largest = 0;
    for (const Node &child : node.children) {
        largest = std::max(largest, effectiveWeight(child, asked));
    }
    return node.weight > 0 ? std::max(node.weight, largest > 1 ? largest : 0) : largest;
}

QSizeF minimumSize(const Node &node, const Limits &limits)
{
    if (node.orientation.isEmpty()) {
        return {limits.minWidth, limits.minHeight};
    }
    const bool row = node.orientation == QStringLiteral("row");
    double width = 0;
    double height = 0;
    for (const Node &child : node.children) {
        const QSizeF size = minimumSize(child, limits);
        if (row) {
            width += size.width();
            height = std::max(height, size.height());
        } else {
            height += size.height();
            width = std::max(width, size.width());
        }
    }
    const double gaps = limits.splitter * double(std::max<qsizetype>(0, node.children.size() - 1));
    return row ? QSizeF(width + gaps, height) : QSizeF(width, height + gaps);
}

// Lengths along one split: proportional to the weights (capped), and never
// below each child's minimum. Empty when the minimums don't fit.
QList<double> solveSplit(QList<double> weights, const QList<double> &minimums, double available)
{
    double needed = 0;
    for (double minimum : minimums) {
        needed += minimum;
    }
    if (needed > available + 0.5) {
        return {};
    }
    const double smallest = *std::min_element(weights.cbegin(), weights.cend());
    for (double &weight : weights) {
        weight = std::min(weight, smallest * kMaximumWeightRatio);
    }

    QList<double> lengths(weights.size(), 0);
    QList<bool> pinned(weights.size(), false);
    for (int pass = 0; pass <= weights.size(); ++pass) {
        double free = available;
        double total = 0;
        for (qsizetype i = 0; i < weights.size(); ++i) {
            if (pinned.at(i)) {
                free -= lengths.at(i);
            } else {
                total += weights.at(i);
            }
        }
        bool changed = false;
        for (qsizetype i = 0; i < weights.size(); ++i) {
            if (pinned.at(i)) {
                continue;
            }
            lengths[i] = total > 0 ? free * weights.at(i) / total : 0;
            if (lengths.at(i) < minimums.at(i)) {
                lengths[i] = minimums.at(i);
                pinned[i] = true;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return lengths;
}

// Solves `node` for `size`; on failure points `failed` at the split that
// can't fit its children.
bool solve(Node &node, const QSizeF &size, const QHash<QString, double> &asked, const Limits &limits, Node **failed)
{
    if (node.orientation.isEmpty()) {
        if (size.width() + 0.5 < limits.minWidth || size.height() + 0.5 < limits.minHeight) {
            *failed = &node;
            return false;
        }
        return true;
    }
    const bool row = node.orientation == QStringLiteral("row");
    QList<double> weights;
    QList<double> minimums;
    for (const Node &child : std::as_const(node.children)) {
        weights.append(effectiveWeight(child, asked));
        const QSizeF minimum = minimumSize(child, limits);
        minimums.append(row ? minimum.width() : minimum.height());
    }
    const double gaps = limits.splitter * double(node.children.size() - 1);
    const double length = (row ? size.width() : size.height()) - gaps;
    const QList<double> lengths = solveSplit(weights, minimums, length);
    // Too little room across is found by the child split that needs it.
    if (lengths.isEmpty()) {
        *failed = &node;
        return false;
    }
    node.sizes.clear();
    for (qsizetype i = 0; i < node.children.size(); ++i) {
        node.sizes.append(lengths.at(i) / length);
        const QSizeF childSize = row ? QSizeF(lengths.at(i), size.height()) : QSizeF(size.width(), lengths.at(i));
        if (!solve(node.children[i], childSize, asked, limits, failed)) {
            return false;
        }
    }
    return true;
}

// A split with one child left becomes that child.
void collapse(Node &node)
{
    for (Node &child : node.children) {
        collapse(child);
    }
    if (!node.orientation.isEmpty() && node.children.size() == 1) {
        const double weight = node.weight;
        Node only = node.children.first();
        only.weight = weight > 0 ? weight : only.weight;
        node = only;
    }
}

Node leaf(const QString &slot, double weight = 1)
{
    Node node;
    node.slot = slot;
    node.weight = weight;
    return node;
}

Node split(const QString &orientation, const QList<Node> &children, double weight = 1)
{
    Node node;
    node.orientation = orientation;
    node.children = children;
    node.weight = weight;
    return node;
}

QString itemKey(const QVariantMap &item, const WorkspaceModel &model)
{
    const QString viewId = item.value(QStringLiteral("viewId")).toString();
    if (!viewId.isEmpty()) {
        const ViewRecord *view = model.view(viewId);
        return view ? view->resource.key() : QString();
    }
    const QVariantMap resource = item.value(QStringLiteral("resource")).toMap();
    ResourceRef ref;
    ref.kind = resource.value(QStringLiteral("kind")).toString();
    ref.id = resource.value(QStringLiteral("id")).toString();
    ref.collectionId = resource.value(QStringLiteral("collectionId")).toString();
    ref.libraryId = resource.value(QStringLiteral("libraryId")).toString();
    return ref.isEmpty() ? QString() : ref.key();
}

Plan refuse(const QString &message)
{
    Plan plan;
    plan.code = QStringLiteral("INVALID_ARGUMENT");
    plan.message = message;
    return plan;
}
} // namespace

QList<Template> templates()
{
    static const QList<Template> list = []() {
        QList<Template> out;
        for (const QJsonValue &value : QJsonDocument::fromJson(kTemplates).array()) {
            const QJsonObject object = value.toObject();
            out.append({
                object.value(QStringLiteral("id")).toString(),
                object.value(QStringLiteral("name")).toString(),
                object.value(QStringLiteral("version")).toInt(1),
                object.value(QStringLiteral("description")).toString(),
                object,
            });
        }
        return out;
    }();
    return list;
}

QStringList templateIds()
{
    QStringList ids;
    for (const Template &recipe : templates()) {
        ids.append(recipe.id);
    }
    return ids;
}

QStringList strategies()
{
    return {QStringLiteral("columns"), QStringLiteral("rows"), QStringLiteral("focus_with_sidebar")};
}

QStringList holdWords()
{
    return {
        QStringLiteral("code"), QStringLiteral("docs"), QStringLiteral("preview"), QStringLiteral("chat"),
        QStringLiteral("agents"), QStringLiteral("terminal"), QStringLiteral("diff"),
    };
}

Plan plan(const Spec &spec, const WorkspaceModel &model)
{
    // ---- check the specification ----
    const Template *recipe = nullptr;
    if (!spec.templateId.isEmpty()) {
        for (const Template &candidate : templates()) {
            if (candidate.id == spec.templateId) {
                recipe = &candidate;
            }
        }
        if (!recipe) {
            return refuse(QStringLiteral("There's no “%1” template. Templates: %2.").arg(spec.templateId, templateIds().join(QStringLiteral(", "))));
        }
    }
    if (!spec.strategy.isEmpty() && !strategies().contains(spec.strategy)) {
        return refuse(QStringLiteral("“%1” isn't a strategy. Strategies: %2.").arg(spec.strategy, strategies().join(QStringLiteral(", "))));
    }
    if (!recipe && spec.panels.isEmpty()) {
        return refuse(QStringLiteral("Describe at least one panel, or pick a template."));
    }

    int itemCount = 0;
    QSet<QString> names;
    for (const Slot &slot : spec.panels) {
        const QString name = slot.name.simplified();
        if (name.isEmpty() || name.size() > 40) {
            return refuse(QStringLiteral("Every panel needs a short name."));
        }
        if (names.contains(name.toLower())) {
            return refuse(QStringLiteral("Two panels are called “%1”.").arg(name));
        }
        names.insert(name.toLower());
        if (slot.weight < 0 || slot.weight > 10) {
            return refuse(QStringLiteral("“%1” has a weight outside 0 to 10.").arg(name));
        }
        if (slot.items.size() > kMaximumItemsPerSlot) {
            return refuse(QStringLiteral("“%1” has more than %2 items.").arg(name).arg(kMaximumItemsPerSlot));
        }
        itemCount += int(slot.items.size());
    }
    if (itemCount > kMaximumItems) {
        return refuse(QStringLiteral("That's more than %1 items for one arrangement.").arg(kMaximumItems));
    }

    // ---- fill the template's slots, or arrange the given ones ----
    QList<Slot> panelSlots;
    Node root;
    if (recipe) {
        for (const QJsonValue &value : recipe->recipe.value(QStringLiteral("slots")).toArray()) {
            Slot slot;
            slot.name = value.toObject().value(QStringLiteral("name")).toString();
            for (const QJsonValue &hold : value.toObject().value(QStringLiteral("holds")).toArray()) {
                slot.holds.append(hold.toString());
            }
            panelSlots.append(slot);
        }
        root = readNode(recipe->recipe.value(QStringLiteral("tree")).toObject());

        QList<bool> taken(panelSlots.size(), false);
        QList<Slot> extras;
        QList<const Slot *> unmatched;
        const auto fill = [&](qsizetype index, const Slot &given, bool rename) {
            taken[index] = true;
            Slot &target = panelSlots[index];
            if (rename) {
                target.name = given.name.simplified();
            }
            target.items = given.items;
            target.weight = given.weight;
            if (!given.holds.isEmpty()) {
                target.holds = given.holds;
            }
        };
        // By name first, then by what they hold.
        for (const Slot &given : spec.panels) {
            bool placed = false;
            for (qsizetype index = 0; index < panelSlots.size() && !placed; ++index) {
                if (!taken.at(index) && panelSlots.at(index).name.compare(given.name.simplified(), Qt::CaseInsensitive) == 0) {
                    fill(index, given, false);
                    placed = true;
                }
            }
            if (!placed) {
                unmatched.append(&given);
            }
        }
        for (const Slot *given : std::as_const(unmatched)) {
            bool placed = false;
            for (qsizetype index = 0; index < panelSlots.size() && !placed; ++index) {
                if (taken.at(index)) {
                    continue;
                }
                for (const QString &hold : given->holds) {
                    if (panelSlots.at(index).holds.contains(hold.toLower())) {
                        // Keep the template's tree working: it refers to the old name.
                        const QString oldName = panelSlots.at(index).name;
                        fill(index, *given, true);
                        std::function<void(Node &)> rename = [&](Node &node) {
                            if (node.slot == oldName) {
                                node.slot = panelSlots.at(index).name;
                            }
                            for (Node &child : node.children) {
                                rename(child);
                            }
                        };
                        rename(root);
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed) {
                extras.append(*given);
            }
        }
        if (!extras.isEmpty()) {
            QList<Node> side;
            for (Slot &extra : extras) {
                extra.name = extra.name.simplified();
                side.append(leaf(extra.name));
                panelSlots.append(extra);
            }
            root.weight = 1.65;
            root = split(QStringLiteral("row"), {root, side.size() == 1 ? leaf(extras.first().name, 1) : split(QStringLiteral("column"), side, 0)});
        }
    } else {
        for (const Slot &given : spec.panels) {
            Slot slot = given;
            slot.name = slot.name.simplified();
            panelSlots.append(slot);
        }
        QString strategy = spec.strategy;
        if (strategy.isEmpty()) {
            strategy = panelSlots.size() <= 3 ? QStringLiteral("columns") : QStringLiteral("focus_with_sidebar");
        }
        if (panelSlots.size() == 1) {
            root = leaf(panelSlots.first().name);
        } else if (strategy == QStringLiteral("focus_with_sidebar")) {
            QList<Node> side;
            for (qsizetype index = 1; index < panelSlots.size(); ++index) {
                side.append(leaf(panelSlots.at(index).name));
            }
            // About 62/38 unless the panels ask otherwise.
            root = split(QStringLiteral("row"), {leaf(panelSlots.first().name, 1.65), side.size() == 1 ? leaf(panelSlots.at(1).name, 1) : split(QStringLiteral("column"), side, 0)});
        } else {
            QList<Node> children;
            for (const Slot &slot : std::as_const(panelSlots)) {
                children.append(leaf(slot.name));
            }
            root = split(strategy == QStringLiteral("rows") ? QStringLiteral("column") : QStringLiteral("row"), children);
        }
    }

    if (panelSlots.size() > kMaximumSlots) {
        return refuse(QStringLiteral("That's more than %1 panels; combine some as tabs.").arg(kMaximumSlots));
    }

    // ---- solve the geometry for the real viewport ----
    // When panels can't all keep a usable size, the lightest neighbours in
    // the split that overflows share one panel as tabs.
    const QSizeF viewport = spec.viewport.isValid() && spec.viewport.width() > 0 && spec.viewport.height() > 0 ? spec.viewport : QSizeF(1440, 900);
    const Limits limits;
    QStringList adjustments;
    QString focusWanted = spec.focus.trimmed();
    for (;;) {
        QHash<QString, double> asked;
        for (const Slot &slot : std::as_const(panelSlots)) {
            asked.insert(slot.name, slot.weight);
        }
        Node *failed = nullptr;
        if (solve(root, viewport, asked, limits, &failed)) {
            break;
        }
        // The two lightest neighbouring panels in that split become one.
        // When it has no two neighbouring panels (say, chat beside a
        // column), the lightest group inside it gives up a panel instead.
        qsizetype best = -1;
        for (;;) {
            double bestWeight = 0;
            for (qsizetype i = 0; i + 1 < failed->children.size(); ++i) {
                const Node &a = failed->children.at(i);
                const Node &b = failed->children.at(i + 1);
                if (!a.orientation.isEmpty() || !b.orientation.isEmpty()) {
                    continue;
                }
                const double weight = effectiveWeight(a, asked) + effectiveWeight(b, asked);
                if (best < 0 || weight < bestWeight) {
                    best = i;
                    bestWeight = weight;
                }
            }
            if (best >= 0) {
                break;
            }
            Node *lightest = nullptr;
            for (Node &child : failed->children) {
                if (!child.orientation.isEmpty() && (!lightest || effectiveWeight(child, asked) < effectiveWeight(*lightest, asked))) {
                    lightest = &child;
                }
            }
            if (!lightest) {
                break;
            }
            failed = lightest;
        }
        if (best < 0) {
            return refuse(QStringLiteral("There isn't room for that arrangement (%1 x %2 pixels, panels need at least %3 x %4). Try fewer panels side by side.")
                              .arg(qRound(viewport.width())).arg(qRound(viewport.height()))
                              .arg(qRound(limits.minWidth)).arg(qRound(limits.minHeight)));
        }
        const QString keep = failed->children.at(best).slot;
        const QString fold = failed->children.at(best + 1).slot;
        Slot *into = nullptr;
        qsizetype foldIndex = -1;
        for (qsizetype i = 0; i < panelSlots.size(); ++i) {
            if (panelSlots.at(i).name == keep) {
                into = &panelSlots[i];
            } else if (panelSlots.at(i).name == fold) {
                foldIndex = i;
            }
        }
        if (!into || foldIndex < 0) {
            return refuse(QStringLiteral("The arrangement couldn't be fitted."));
        }
        into->items.append(panelSlots.at(foldIndex).items);
        for (const QString &hold : panelSlots.at(foldIndex).holds) {
            if (!into->holds.contains(hold)) {
                into->holds.append(hold);
            }
        }
        adjustments.append(QStringLiteral("“%1” is a tab in “%2” (not enough room for both)").arg(fold, keep));
        if (focusWanted.compare(fold, Qt::CaseInsensitive) == 0) {
            focusWanted = keep; // focus follows the content
        }
        panelSlots.removeAt(foldIndex);
        failed->children.removeAt(best + 1);
        collapse(root);
    }

    QHash<QString, qsizetype> slotIndex;
    for (qsizetype index = 0; index < panelSlots.size(); ++index) {
        slotIndex.insert(panelSlots.at(index).name, index);
    }
    QString focusSlot;
    if (!focusWanted.isEmpty()) {
        for (const Slot &slot : std::as_const(panelSlots)) {
            if (slot.name.compare(focusWanted, Qt::CaseInsensitive) == 0) {
                focusSlot = slot.name;
            }
        }
        if (focusSlot.isEmpty()) {
            return refuse(QStringLiteral("There's no panel called “%1” to focus.").arg(focusWanted));
        }
    }

    // ---- reuse panels that already show a slot's content ----
    const QString surfaceId = model.primarySurfaceId();
    QList<QStringList> slotTypes;
    for (const Slot &slot : std::as_const(panelSlots)) {
        QStringList types;
        for (const QString &hold : slot.holds) {
            for (const QString &type : typesFor(hold)) {
                if (!types.contains(type)) {
                    types.append(type);
                }
            }
        }
        for (const QVariant &value : slot.items) {
            const QString type = value.toMap().value(QStringLiteral("typeId")).toString();
            if (!type.isEmpty() && !types.contains(type)) {
                types.append(type);
            }
        }
        slotTypes.append(types);
    }

    struct Pair
    {
        int score;
        qsizetype slot;
        QString anchorId;
    };
    QList<Pair> pairs;
    for (const QString &anchorId : model.anchorIds(surfaceId)) {
        const AnchorRecord *anchor = model.anchor(anchorId);
        QSet<QString> keys;
        QSet<QString> types;
        for (const QString &viewId : anchor->views) {
            if (const ViewRecord *view = model.view(viewId)) {
                keys.insert(view->resource.key());
                types.insert(view->typeId);
            }
        }
        for (const QString &role : anchor->roles) {
            types.insert(role);
        }
        for (qsizetype index = 0; index < panelSlots.size(); ++index) {
            int score = 0;
            for (const QVariant &value : panelSlots.at(index).items) {
                const QVariantMap item = value.toMap();
                if (anchor->views.contains(item.value(QStringLiteral("viewId")).toString()) || keys.contains(itemKey(item, model))) {
                    score += 100;
                }
            }
            for (const QString &type : slotTypes.at(index)) {
                if (types.contains(type)) {
                    score += 30;
                    break;
                }
            }
            if (!anchor->name.isEmpty() && anchor->name.compare(panelSlots.at(index).name, Qt::CaseInsensitive) == 0) {
                score += 50;
            }
            if (score > 0) {
                pairs.append({score, index, anchorId});
            }
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.score > b.score; });

    QHash<qsizetype, QString> anchorOfSlot;
    QSet<QString> usedAnchors;
    for (const Pair &pair : std::as_const(pairs)) {
        if (!anchorOfSlot.contains(pair.slot) && !usedAnchors.contains(pair.anchorId)) {
            anchorOfSlot.insert(pair.slot, pair.anchorId);
            usedAnchors.insert(pair.anchorId);
        }
    }
    const QStringList existingAnchors = model.anchorIds();
    for (qsizetype index = 0; index < panelSlots.size(); ++index) {
        if (anchorOfSlot.contains(index)) {
            continue;
        }
        QString id = QStringLiteral("compose:") + slug(panelSlots.at(index).name);
        for (int n = 2; existingAnchors.contains(id) || usedAnchors.contains(id); ++n) {
            id = QStringLiteral("compose:%1-%2").arg(slug(panelSlots.at(index).name)).arg(n);
        }
        anchorOfSlot.insert(index, id);
        usedAnchors.insert(id);
    }

    // Leftover views land with the code, else in the first panel.
    qsizetype targetSlot = 0;
    for (qsizetype index = 0; index < panelSlots.size(); ++index) {
        if (slotTypes.at(index).contains(ViewTypes::Editor)) {
            targetSlot = index;
            break;
        }
    }

    // ---- the layout document ----
    QSet<QString> placedSlots;
    std::function<QJsonObject(const Node &)> encode = [&](const Node &node) -> QJsonObject {
        if (node.orientation.isEmpty()) {
            const qsizetype index = slotIndex.value(node.slot, -1);
            if (index < 0) {
                return {};
            }
            placedSlots.insert(node.slot);
            QStringList roles = slotTypes.at(index);
            if (index == targetSlot && !roles.contains(ViewTypes::Welcome)) {
                roles.append(ViewTypes::Welcome);
            }
            QJsonObject anchor{
                {QStringLiteral("id"), anchorOfSlot.value(index)},
                {QStringLiteral("pinned"), true},
                {QStringLiteral("roles"), QJsonArray::fromStringList(roles)},
                {QStringLiteral("name"), panelSlots.at(index).name},
            };
            // What the person set on a reused panel stays.
            if (const AnchorRecord *existing = model.anchor(anchorOfSlot.value(index))) {
                if (existing->switchable) {
                    anchor.insert(QStringLiteral("switchable"), true);
                }
                if (!existing->libraryId.isEmpty()) {
                    anchor.insert(QStringLiteral("libraryId"), existing->libraryId);
                }
            }
            return {{QStringLiteral("anchor"), anchor}};
        }

        QJsonArray children;
        QJsonArray sizes;
        for (qsizetype index = 0; index < node.children.size(); ++index) {
            children.append(encode(node.children.at(index)));
            sizes.append(node.sizes.value(index, 1.0 / double(node.children.size())));
        }
        return {
            {QStringLiteral("split"), node.orientation == QStringLiteral("row") ? QStringLiteral("horizontal") : QStringLiteral("vertical")},
            {QStringLiteral("sizes"), sizes},
            {QStringLiteral("children"), children},
        };
    };
    const QJsonObject tree = encode(root);
    if (placedSlots.size() != panelSlots.size()) {
        return refuse(QStringLiteral("The arrangement doesn't place every panel."));
    }

    const QJsonObject layout{
        {QStringLiteral("format"), Codec::LayoutFormat},
        {QStringLiteral("version"), Codec::kLayoutVersion},
        {QStringLiteral("name"), recipe ? recipe->name : QString()},
        {QStringLiteral("targetAnchor"), anchorOfSlot.value(targetSlot)},
        {QStringLiteral("surfaces"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), surfaceId.isEmpty() ? QStringLiteral("main") : surfaceId},
            {QStringLiteral("primary"), true},
            {QStringLiteral("root"), tree},
        }}},
    };

    QVariantList slotRequests;
    Plan result;
    for (qsizetype index = 0; index < panelSlots.size(); ++index) {
        slotRequests.append(QVariantMap{
            {QStringLiteral("anchorId"), anchorOfSlot.value(index)},
            {QStringLiteral("items"), panelSlots.at(index).items},
        });
        result.slotNames.append(panelSlots.at(index).name);
    }
    result.ok = true;
    result.adjustments = adjustments;
    result.request = {
        {QStringLiteral("action"), QStringLiteral("compose")},
        {QStringLiteral("layout"), layout.toVariantMap()},
        {QStringLiteral("slots"), slotRequests},
        {QStringLiteral("focusAnchor"), focusSlot.isEmpty() ? QString() : anchorOfSlot.value(slotIndex.value(focusSlot))},
    };
    return result;
}
} // namespace Workspace::Compose
