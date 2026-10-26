#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantMap>
#include <list>
#include <memory>

class QTextDocument;

// A flat shape drawn under the text: quote bars, inline-code chips, link
// underlines, diff line tints. Kept with the layout so painting stays cheap.
struct ChatTextDecoration
{
    QRectF rect;
    QColor color;
    // Spans the item's whole width (plus its bleed), not just the text.
    bool fullWidth = false;
};

// A laid-out, read-only text document shared between the cache and the chat
// text items that show it.
struct ChatTextDocument
{
    std::unique_ptr<QTextDocument> document;
    QSizeF size;
    qreal idealWidth = 0;
    int length = 0;
    int lineCount = 0;
    QList<ChatTextDecoration> decorations;

    ChatTextDocument();
    ~ChatTextDocument();
};

using ChatTextDocumentPtr = std::shared_ptr<const ChatTextDocument>;

// Laid-out chat text, kept so rows that scroll back into view (or are
// recycled for a message they showed before) skip markdown parsing and text
// layout. UI thread only: a laid-out document holds font engines that belong
// to the thread that laid it out, so documents must not be built on workers.
class ChatTextLayout final
{
public:
    struct Request
    {
        QString text;
        bool markdown = true;
        bool wrap = true;
        QFont font;
        qreal width = 0;
        // Plain text only: the fence language to syntax-highlight.
        QString language;
        // Colors and type for the reading styles (see styleKey for the keys
        // build() understands); empty means plain unstyled text.
        QVariantMap style;
        QString styleKey;
    };

    static ChatTextLayout &instance();

    // The cached document for the request, built now on a miss.
    ChatTextDocumentPtr acquire(const Request &request, bool cache = true);

    // Builds without touching the cache.
    static ChatTextDocumentPtr build(const Request &request);

    // The markdown Archivist shows: images removed and GitHub-style
    // callouts ("> [!NOTE]") turned into a bold label.
    static QString displayMarkdown(const QString &markdown);

    // A stable string for a style map, used in the cache key.
    //
    // Markdown keys: lineHeight (multiple of the font size), headingFamily,
    // headingColor, accent, codeFamily, codeColor, codeBackground,
    // linkColor, linkUnderline, linkArrows, quoteColor, quoteBar,
    // calloutBackground, warning, tableBorder, tableHeaderBackground.
    // Code keys: keyword, string, comment, number, type, function, constant,
    // meta, added, removed, addedBackground, removedBackground.
    static QString styleKey(const QVariantMap &style);

    [[nodiscard]] int cachedCount() const { return int(m_cache.size()); }
    [[nodiscard]] int buildCount() const { return m_builds; }
    void clear();

private:
    ChatTextLayout() = default;

    [[nodiscard]] static QString keyFor(const Request &request);

    QHash<QString, std::pair<ChatTextDocumentPtr, std::list<QString>::iterator>> m_cache;
    std::list<QString> m_order; // most recently used at the front
    int m_builds = 0;
};
