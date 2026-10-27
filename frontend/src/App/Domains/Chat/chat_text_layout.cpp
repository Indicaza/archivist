#include "chat_text_layout.h"

#include "chat_syntax.h"

#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>
#include <QTextOption>
#include <QTextTable>
#include <QTextTableCell>
#include <algorithm>

namespace
{
// Enough for the rows around the viewport several times over, including
// both widths while the window is being resized; one entry is one message
// block, typically a few kilobytes.
constexpr int maximumCachedDocuments = 600;
}

ChatTextDocument::ChatTextDocument() = default;
ChatTextDocument::~ChatTextDocument() = default;

ChatTextLayout &ChatTextLayout::instance()
{
    static ChatTextLayout layout;
    return layout;
}

QString ChatTextLayout::displayMarkdown(const QString &markdown)
{
    static const QRegularExpression inlineImage(QStringLiteral(R"(!\[([^\]]*)\]\([^\n)]*\))"));
    static const QRegularExpression referenceImage(QStringLiteral(R"(!\[([^\]]*)\]\s*\[[^\]]*\])"));
    static const QRegularExpression htmlImage(QStringLiteral(R"(<\s*img\b[^>]*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression callout(
        QStringLiteral(R"(^(\s*>\s*)\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*$)"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);

    if (!markdown.contains(QLatin1Char('!')) && !markdown.contains(QLatin1Char('<'))) {
        return markdown;
    }

    const auto omitImages = [](QString text, const QRegularExpression &pattern) {
        QString result;
        qsizetype last = 0;
        auto matches = pattern.globalMatch(text);

        while (matches.hasNext()) {
            const auto match = matches.next();
            const QString alt = match.captured(1);
            result += text.mid(last, match.capturedStart() - last);
            result += alt.isEmpty() ? QStringLiteral("*Image omitted*") : QStringLiteral("*Image omitted: %1*").arg(alt);
            last = match.capturedEnd();
        }

        return last == 0 ? text : result + text.mid(last);
    };

    QString safe = omitImages(markdown, inlineImage);
    safe = omitImages(safe, referenceImage);
    safe.replace(htmlImage, QStringLiteral("&lt;image omitted&gt;"));

    // "> [!NOTE]" becomes "> **Note**", which the callout styling picks up.
    QString result;
    qsizetype last = 0;
    auto matches = callout.globalMatch(safe);

    while (matches.hasNext()) {
        const auto match = matches.next();
        QString label = match.captured(2).toLower();
        label[0] = label[0].toUpper();
        result += safe.mid(last, match.capturedStart() - last);
        result += match.captured(1) + QStringLiteral("**") + label + QStringLiteral("**");
        last = match.capturedEnd();
    }

    return last == 0 ? safe : result + safe.mid(last);
}

QString ChatTextLayout::styleKey(const QVariantMap &style)
{
    QString key;

    for (auto it = style.cbegin(); it != style.cend(); ++it) {
        const QVariant &value = it.value();
        key += it.key();
        key += QLatin1Char('=');
        key += value.metaType().id() == QMetaType::QColor ? value.value<QColor>().name(QColor::HexArgb) : value.toString();
        key += QLatin1Char(';');
    }

    return key;
}

QString ChatTextLayout::keyFor(const Request &request)
{
    // Width only matters for wrapped text; quarter-pixel buckets keep
    // floating-point noise from defeating the cache.
    const int width = request.wrap ? qRound(request.width * 4) : 0;
    QCryptographicHash hash(QCryptographicHash::Blake2b_160);
    hash.addData(request.text.toUtf8());
    return QString::fromLatin1(hash.result().toHex()) + QLatin1Char('|') + QString::number(request.text.size())
        + QLatin1Char('|') + QLatin1Char(request.markdown ? 'm' : 'p') + QLatin1Char(request.wrap ? 'w' : 'n')
        + QLatin1Char('|') + QString::number(width) + QLatin1Char('|') + request.font.toString()
        + QLatin1Char('|') + QString::number(int(request.font.hintingPreference()))
        + QLatin1Char(request.font.kerning() ? 'k' : '-') + QLatin1Char('|') + request.language
        + QLatin1Char('|') + request.styleKey;
}

namespace
{
QColor styleColor(const QVariantMap &style, const char *key)
{
    const QVariant value = style.value(QLatin1String(key));

    if (!value.isValid()) {
        return {};
    }

    if (value.metaType().id() == QMetaType::QColor) {
        return value.value<QColor>();
    }

    return QColor::fromString(value.toString());
}

qreal pixelSizeOf(const QFont &font)
{
    if (font.pixelSize() > 0) {
        return font.pixelSize();
    }

    return QFontMetricsF(font).height() / 1.2;
}

// The style's lineHeight (a multiple of the font size) as the percentage of
// the font's natural line spacing that QTextBlockFormat takes; 0 for none.
int proportionalLineHeight(const QVariantMap &style, const QFont &base)
{
    const qreal lineHeight = style.value(QStringLiteral("lineHeight")).toReal();
    const qreal naturalLine = QFontMetricsF(base).lineSpacing();

    if (lineHeight <= 0 || naturalLine <= 0) {
        return 0;
    }

    return std::max(100, qRound(lineHeight * pixelSizeOf(base) / naturalLine * 100));
}

bool isCodeFormat(const QTextCharFormat &format)
{
    if (format.fontFixedPitch()) {
        return true;
    }

    const QStringList families = format.fontFamilies().toStringList();
    return !families.isEmpty()
        && (families.first().contains(QLatin1String("mono"), Qt::CaseInsensitive)
            || families.first().contains(QLatin1String("courier"), Qt::CaseInsensitive));
}

// Calls `visit(line, origin, x1, x2)` for each line piece of [start, end).
template<typename Visitor>
void forEachLinePiece(const QTextDocument *document, int start, int end, Visitor visit)
{
    QTextBlock block = document->findBlock(start);

    while (block.isValid() && block.position() < end) {
        const QTextLayout *layout = block.layout();

        if (layout) {
            const QPointF origin = layout->position();
            const int blockStart = std::max(start, block.position()) - block.position();
            const int blockEnd = std::min(end, block.position() + block.length() - 1) - block.position();

            for (int index = 0; index < layout->lineCount(); ++index) {
                const QTextLine line = layout->lineAt(index);
                const int from = std::max(blockStart, line.textStart());
                const int to = std::min(blockEnd, line.textStart() + line.textLength());

                if (from < to) {
                    visit(line, origin, line.cursorToX(from), line.cursorToX(to));
                }
            }
        }

        block = block.next();
    }
}

struct MarkdownMarks
{
    QList<std::pair<int, int>> code;
    QList<std::pair<int, int>> links;
    QFont codeFont;
};

// The markdown importer starts a table cell with an empty paragraph when the
// table follows a quote, which pushes that cell's text down a line.
void removeEmptyCellLeads(QTextDocument *document)
{
    const auto frames = document->rootFrame()->childFrames();

    for (QTextFrame *frame : frames) {
        auto *table = qobject_cast<QTextTable *>(frame);

        if (!table) {
            continue;
        }

        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                const QTextBlock first = document->findBlock(cell.firstPosition());

                if (first.length() == 1 && cell.lastPosition() > first.position()) {
                    QTextCursor cursor(document);
                    cursor.setPosition(first.position());
                    cursor.deleteChar();
                }
            }
        }
    }
}

MarkdownMarks styleMarkdown(QTextDocument *document, const QVariantMap &style, const QFont &base)
{
    MarkdownMarks marks;
    removeEmptyCellLeads(document);

    if (style.isEmpty()) {
        return marks;
    }

    const qreal size = pixelSizeOf(base);
    const int bodyPercent = proportionalLineHeight(style, base);
    const QString headingFamily = style.value(QStringLiteral("headingFamily")).toString();
    const QString codeFamily = style.value(QStringLiteral("codeFamily")).toString();
    const QColor headingColor = styleColor(style, "headingColor");
    const QColor accent = styleColor(style, "accent");
    const QColor bulletColor = styleColor(style, "bulletColor").isValid()
        ? styleColor(style, "bulletColor")
        : accent;
    const QColor codeColor = styleColor(style, "codeColor");
    const QColor linkColor = styleColor(style, "linkColor");
    const QColor quoteColor = styleColor(style, "quoteColor");
    const QColor tableBorder = styleColor(style, "tableBorder");
    const bool linkArrows = style.value(QStringLiteral("linkArrows")).toBool();

    marks.codeFont = base;

    if (!codeFamily.isEmpty()) {
        marks.codeFont.setFamilies({codeFamily});
    }

    marks.codeFont.setPixelSize(std::max(9, qRound(size * 0.86)));
    marks.codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    document->setIndentWidth(std::round(size * 1.4));

    struct CharEdit
    {
        int start;
        int end;
        QTextCharFormat format;
        QTextCharFormat base;
    };

    QList<CharEdit> charEdits;
    QList<std::pair<int, QTextBlockFormat>> blockEdits;
    QList<std::pair<int, QTextCharFormat>> bulletEdits;
    QList<std::pair<int, QTextCharFormat>> arrows;

    // What the previous block was, for the space above the next one.
    enum class Previous { None, Heading, ListItem, Text };
    Previous previous = Previous::None;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat blockFormat = block.blockFormat();
        const int heading = blockFormat.headingLevel();
        const bool inTable = QTextCursor(block).currentTable() != nullptr;
        // The markdown importer can leave a quote level on the first table
        // cell after a quote; a table is never part of the quote.
        const int quote = inTable ? 0 : blockFormat.intProperty(QTextFormat::BlockQuoteLevel);
        const bool listItem = !inTable && heading == 0 && block.textList() != nullptr;
        QTextBlockFormat nextBlock;
        QTextCharFormat headingFormat;

        // Space between blocks, set as a top margin so it never doubles up:
        // a clear gap between paragraphs, a smaller one between list items,
        // more above a heading and little below it.
        if (!inTable) {
            qreal above = 0.85;

            if (previous == Previous::None) {
                above = 0;
            } else if (heading > 0) {
                above = heading <= 2 ? 1.5 : 1.2;
            } else if (previous == Previous::Heading) {
                above = 0.4;
            } else if (listItem && previous == Previous::ListItem) {
                above = 0.35;
            } else if (listItem) {
                above = 0.6;
            }

            nextBlock.setTopMargin(std::round(size * above));
            nextBlock.setBottomMargin(0);
            previous = heading > 0 ? Previous::Heading : listItem ? Previous::ListItem : Previous::Text;
        }

        if (heading > 0) {
            nextBlock.setLineHeight(118, QTextBlockFormat::ProportionalHeight);

            if (heading <= 2) {
                if (!headingFamily.isEmpty()) {
                    headingFormat.setFontFamilies(QStringList {headingFamily});
                }

                headingFormat.setProperty(QTextFormat::FontPixelSize, qRound(size * (heading == 1 ? 1.5 : 1.25)));
                headingFormat.setFontWeight(QFont::DemiBold);
                headingFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                headingFormat.setFontLetterSpacing(0);

                if (headingColor.isValid()) {
                    headingFormat.setForeground(headingColor);
                }
            } else if (heading == 3) {
                headingFormat.setProperty(QTextFormat::FontPixelSize, qRound(size * 1.1));
                headingFormat.setFontWeight(QFont::DemiBold);
                headingFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                headingFormat.setFontLetterSpacing(0);

                if (headingColor.isValid()) {
                    headingFormat.setForeground(headingColor);
                }
            } else {
                headingFormat.setProperty(QTextFormat::FontPixelSize, qRound(size));
                headingFormat.setFontWeight(QFont::DemiBold);

                if (headingColor.isValid()) {
                    headingFormat.setForeground(headingColor);
                }
            }
        } else if (bodyPercent > 100 && !inTable) {
            nextBlock.setLineHeight(bodyPercent, QTextBlockFormat::ProportionalHeight);
        }

        if (quote > 0) {
            nextBlock.setLeftMargin(std::round(size * 1.1) * quote);
            nextBlock.setIndent(0);
        } else if (inTable && blockFormat.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
            nextBlock.setProperty(QTextFormat::BlockQuoteLevel, 0);
            nextBlock.setLeftMargin(0);
        }

        blockEdits.append({block.position(), nextBlock});

        if (block.textList() && bulletColor.isValid()) {
            QTextCharFormat bullet;
            bullet.setForeground(bulletColor);
            bulletEdits.append({block.position(), bullet});
        }

        QString openHref;
        int openEnd = -1;
        QTextCharFormat openFormat;

        const auto closeLink = [&]() {
            if (openEnd >= 0 && linkArrows) {
                QTextCharFormat arrow = openFormat;
                arrow.setAnchor(false);
                arrow.clearProperty(QTextFormat::AnchorHref);
                arrow.clearProperty(QTextFormat::IsAnchor);
                arrow.setFontUnderline(false);
                arrow.setProperty(QTextFormat::FontPixelSize, std::max(9, qRound(size * 0.78)));

                if (linkColor.isValid()) {
                    QColor faded = linkColor;
                    faded.setAlphaF(0.7);
                    arrow.setForeground(faded);
                }

                arrows.append({openEnd, arrow});
            }

            openHref.clear();
            openEnd = -1;
        };

        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();

            if (!fragment.isValid()) {
                continue;
            }

            const QTextCharFormat format = fragment.charFormat();
            const int start = fragment.position();
            const int end = start + fragment.length();
            QTextCharFormat edit = headingFormat;
            bool changed = heading > 0;

            if (isCodeFormat(format) && heading == 0) {
                if (!codeFamily.isEmpty()) {
                    edit.setFontFamilies(QStringList {codeFamily});
                }

                edit.setFontFixedPitch(true);
                edit.setProperty(QTextFormat::FontPixelSize, marks.codeFont.pixelSize());
                edit.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                edit.setFontLetterSpacing(0);

                if (codeColor.isValid()) {
                    edit.setForeground(codeColor);
                }

                changed = true;

                if (!marks.code.isEmpty() && marks.code.last().second == start) {
                    marks.code.last().second = end;
                } else {
                    marks.code.append({start, end});
                }
            } else if (quote > 0 && heading == 0 && quoteColor.isValid() && !format.isAnchor()) {
                edit.setForeground(quoteColor);
                changed = true;
            }

            if (format.isAnchor() && !format.anchorHref().isEmpty()) {
                // Underlines are drawn as decorations so they can sit a little
                // lower and lighter than the font's own underline.
                edit.setFontUnderline(false);

                if (linkColor.isValid()) {
                    edit.setForeground(linkColor);
                }

                changed = true;

                if (format.anchorHref() == openHref && openEnd == start) {
                    openEnd = end;
                    marks.links.last().second = end;
                } else {
                    closeLink();
                    openHref = format.anchorHref();
                    openEnd = end;
                    openFormat = format;
                    marks.links.append({start, end});
                }
            } else {
                closeLink();
            }

            if (changed) {
                charEdits.append({start, end, edit, format});
            }
        }

        closeLink();
    }

    QTextCursor cursor(document);

    for (const auto &[position, format] : blockEdits) {
        cursor.setPosition(position);
        cursor.mergeBlockFormat(format);
    }

    for (const auto &[position, format] : bulletEdits) {
        cursor.setPosition(position);
        cursor.mergeBlockCharFormat(format);
    }

    for (const CharEdit &edit : charEdits) {
        cursor.setPosition(edit.start);
        cursor.setPosition(edit.end, QTextCursor::KeepAnchor);

        // A new pixel size has to replace the importer's point size, which
        // a merge would keep (and which wins when the font is resolved).
        if (edit.format.hasProperty(QTextFormat::FontPixelSize)) {
            QTextCharFormat replaced = edit.base;
            replaced.clearProperty(QTextFormat::FontPointSize);
            replaced.clearProperty(QTextFormat::FontSizeAdjustment);
            replaced.merge(edit.format);
            cursor.setCharFormat(replaced);
        } else {
            cursor.mergeCharFormat(edit.format);
        }
    }

    // Tables: hairline borders, roomy cells, a tinted header row.
    const auto frames = document->rootFrame()->childFrames();

    for (QTextFrame *frame : frames) {
        auto *table = qobject_cast<QTextTable *>(frame);

        if (!table) {
            continue;
        }

        QTextTableFormat format = table->format();
        format.setBorder(1);
        format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        format.setBorderCollapse(true);
        format.setCellSpacing(0);
        format.setCellPadding(std::round(size * 0.4));

        if (tableBorder.isValid()) {
            format.setBorderBrush(tableBorder);
        }

        table->setFormat(format);

        // Header cells read as headers whatever the importer left on them.
        QTextCharFormat header;
        header.setFontWeight(QFont::DemiBold);

        if (headingColor.isValid()) {
            header.setForeground(headingColor);
        }

        for (int column = 0; column < table->columns(); ++column) {
            const QTextTableCell cell = table->cellAt(0, column);
            cursor.setPosition(cell.firstPosition());
            cursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(header);
        }
    }

    // Arrows go in last, from the end backwards, so earlier positions hold.
    for (auto it = arrows.crbegin(); it != arrows.crend(); ++it) {
        cursor.setPosition(it->first);
        cursor.insertText(QStringLiteral(" ↗"), it->second);
    }

    // The arrows moved later text along; shift the marks after each one.
    if (!arrows.isEmpty()) {
        const auto shifted = [&arrows](int position) {
            int offset = 0;

            for (const auto &arrow : arrows) {
                if (arrow.first <= position) {
                    offset += 2;
                }
            }

            return position + offset;
        };

        for (auto &range : marks.code) {
            range = {shifted(range.first), shifted(range.second)};
        }

        for (auto &range : marks.links) {
            // A link ends right where its arrow starts.
            range = {shifted(range.first), shifted(range.second - 1) + 1};
        }
    }

    return marks;
}

void markdownDecorations(const QTextDocument *document, const MarkdownMarks &marks, const QVariantMap &style,
                         QList<ChatTextDecoration> &decorations)
{
    const QColor codeBackground = styleColor(style, "codeBackground");
    const QColor linkUnderline = styleColor(style, "linkUnderline");
    const QColor quoteBar = styleColor(style, "quoteBar");
    const QColor accent = styleColor(style, "accent");
    const QColor warning = styleColor(style, "warning");
    const QColor calloutBackground = styleColor(style, "calloutBackground");

    // Quotes: a bar down the left edge; a callout ("Note", "Warning") also
    // gets a faint wash and a colored bar.
    const auto *layout = document->documentLayout();
    QTextBlock block = document->begin();

    while (block.isValid()) {
        const int level = block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel);

        if (level <= 0 || !quoteBar.isValid()) {
            block = block.next();
            continue;
        }

        const QString lead = block.text().left(12).trimmed().toLower();
        const bool warningCallout = lead.startsWith(QLatin1String("warning")) || lead.startsWith(QLatin1String("caution"))
            || lead.startsWith(QLatin1String("important"));
        const bool callout = warningCallout || lead.startsWith(QLatin1String("note")) || lead.startsWith(QLatin1String("tip"));
        QRectF run = layout->blockBoundingRect(block);
        QTextBlock next = block.next();

        const auto startsCallout = [](const QTextBlock &candidate) {
            const QString text = candidate.text().left(12).trimmed().toLower();

            for (const char *label : {"note", "tip", "important", "warning", "caution"}) {
                if (text.startsWith(QLatin1String(label))) {
                    return true;
                }
            }

            return false;
        };

        while (next.isValid() && next.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0 && !startsCallout(next)) {
            run = run.united(layout->blockBoundingRect(next));
            next = next.next();
        }

        const qreal margin = block.blockFormat().leftMargin();
        const qreal barX = std::max<qreal>(0, margin - std::round(pixelSizeOf(document->defaultFont()) * 1.1));

        if (callout && calloutBackground.isValid()) {
            decorations.append({QRectF(barX, run.top() - 4, document->textWidth() > 0 ? document->textWidth() - barX : run.right() - barX, run.height() + 8),
                                calloutBackground, false});
        }

        QColor bar = callout ? (warningCallout && warning.isValid() ? warning : accent) : quoteBar;

        if (!bar.isValid()) {
            bar = quoteBar;
        }

        decorations.append({QRectF(barX, run.top() - (callout ? 4 : 0), 2.5, run.height() + (callout ? 8 : 0)), bar, false});
        block = next;
    }

    // Table header rows get a tint behind them.
    const QColor tableHeader = styleColor(style, "tableHeaderBackground");
    const auto frames = document->rootFrame()->childFrames();

    for (QTextFrame *frame : frames) {
        auto *table = qobject_cast<QTextTable *>(frame);

        if (!table || !tableHeader.isValid() || table->rows() < 2) {
            continue;
        }

        const qreal padding = table->format().cellPadding();
        // The table's bounding rect starts at its first cell's content.
        const QRectF bounds = layout->frameBoundingRect(table).translated(-padding, -padding);
        qreal headerBottom = bounds.top();
        qreal headerRight = bounds.left();

        for (int column = 0; column < table->columns(); ++column) {
            const QTextBlock cellBlock = table->cellAt(0, column).lastCursorPosition().block();
            const QRectF cellRect = layout->blockBoundingRect(cellBlock);
            headerBottom = std::max(headerBottom, cellRect.bottom() + padding);
            headerRight = std::max(headerRight, cellRect.right() + padding);
        }

        decorations.append({QRectF(bounds.left() + 1, bounds.top() + 1, headerRight - bounds.left() - 1, headerBottom - bounds.top() - 1),
                            tableHeader, false});
    }

    if (codeBackground.isValid() && !marks.code.isEmpty()) {
        const QFontMetricsF metrics(marks.codeFont);

        for (const auto &[start, end] : marks.code) {
            forEachLinePiece(document, start, end, [&](const QTextLine &line, QPointF origin, qreal x1, qreal x2) {
                const qreal baseline = origin.y() + line.y() + line.ascent();
                decorations.append({QRectF(origin.x() + x1 - 3, baseline - metrics.ascent() - 2.5, x2 - x1 + 6,
                                           metrics.ascent() + metrics.descent() + 5),
                                    codeBackground, false});
            });
        }
    }

    if (linkUnderline.isValid()) {
        for (const auto &[start, end] : marks.links) {
            forEachLinePiece(document, start, end, [&](const QTextLine &line, QPointF origin, qreal x1, qreal x2) {
                const qreal baseline = origin.y() + line.y() + line.ascent();
                decorations.append({QRectF(origin.x() + x1, baseline + 2.5, x2 - x1, 1), linkUnderline, false});
            });
        }
    }
}

void highlightCode(QTextDocument *document, const QString &text, const QString &language, const QVariantMap &style,
                   QList<int> &added, QList<int> &removed)
{
    const QList<ChatSyntax::Span> spans = ChatSyntax::tokenize(text, language);

    if (spans.isEmpty()) {
        return;
    }

    QTextCharFormat formats[10];
    bool valid[10] = {};
    const char *keys[10] = {"keyword", "string", "comment", "number", "type", "function", "constant", "meta", "added", "removed"};

    for (int index = 0; index < 10; ++index) {
        const QColor color = styleColor(style, keys[index]);

        if (color.isValid()) {
            formats[index].setForeground(color);
            valid[index] = true;
        }
    }

    // "<kind>FontStyle" ("italic bold", or "" for plain) comes from imported
    // themes; comments are italic unless the style says otherwise.
    for (int index = 0; index < 10; ++index) {
        const QString fontStyleKey = QLatin1String(keys[index]) + QStringLiteral("FontStyle");
        const bool comment = index == int(ChatSyntax::Kind::Comment);

        if (!style.contains(fontStyleKey)) {
            if (comment) {
                formats[index].setFontItalic(true);
            }

            continue;
        }

        const QString fontStyle = style.value(fontStyleKey).toString();

        if (fontStyle.contains(QLatin1String("italic"))) {
            formats[index].setFontItalic(true);
            valid[index] = true;
        }

        if (fontStyle.contains(QLatin1String("bold"))) {
            formats[index].setFontWeight(QFont::Bold);
            valid[index] = true;
        }

        if (fontStyle.contains(QLatin1String("underline"))) {
            formats[index].setFontUnderline(true);
            valid[index] = true;
        }
    }

    QTextCursor cursor(document);

    for (const ChatSyntax::Span &span : spans) {
        const int kind = int(span.kind);

        if (span.kind == ChatSyntax::Kind::Added || span.kind == ChatSyntax::Kind::Removed) {
            QList<int> &lines = span.kind == ChatSyntax::Kind::Added ? added : removed;
            lines.append(document->findBlock(span.start).blockNumber());
        }

        if (!valid[kind]) {
            continue;
        }

        cursor.setPosition(span.start);
        cursor.setPosition(span.start + span.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(formats[kind]);
    }
}
} // namespace

ChatTextDocumentPtr ChatTextLayout::build(const Request &request)
{
    auto result = std::make_shared<ChatTextDocument>();
    auto document = std::make_unique<QTextDocument>();
    document->setUndoRedoEnabled(false);
    document->setDocumentMargin(0);
    document->setDefaultFont(request.font);

    // Matches TextEdit.Wrap / TextEdit.NoWrap.
    QTextOption option = document->defaultTextOption();
    option.setWrapMode(request.wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    document->setDefaultTextOption(option);

    MarkdownMarks marks;
    QList<int> addedLines;
    QList<int> removedLines;

    if (request.markdown) {
        document->setMarkdown(displayMarkdown(request.text));
        marks = styleMarkdown(document.get(), request.style, request.font);
    } else {
        document->setPlainText(request.text);

        // Code and other plain text can ask for roomier lines too.
        if (const int percent = proportionalLineHeight(request.style, request.font); percent > 100) {
            QTextCursor cursor(document.get());
            QTextBlockFormat format;
            format.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
            cursor.select(QTextCursor::Document);
            cursor.mergeBlockFormat(format);
        }

        if (!request.language.isEmpty() && !request.style.isEmpty()) {
            highlightCode(document.get(), request.text, request.language, request.style, addedLines, removedLines);
        }
    }

    document->setTextWidth(request.wrap ? std::max<qreal>(1, request.width) : -1);
    result->size = document->size(); // lays the whole document out
    result->idealWidth = document->idealWidth();
    result->length = std::max(0, document->characterCount() - 1);
    result->lineCount = document->blockCount();

    if (request.markdown) {
        markdownDecorations(document.get(), marks, request.style, result->decorations);
    } else if (!addedLines.isEmpty() || !removedLines.isEmpty()) {
        const QColor addedBackground = styleColor(request.style, "addedBackground");
        const QColor removedBackground = styleColor(request.style, "removedBackground");
        const auto *layout = document->documentLayout();

        for (const int line : addedLines) {
            if (addedBackground.isValid()) {
                const QRectF rect = layout->blockBoundingRect(document->findBlockByNumber(line));
                result->decorations.append({QRectF(0, rect.top(), 0, rect.height()), addedBackground, true});
            }
        }

        for (const int line : removedLines) {
            if (removedBackground.isValid()) {
                const QRectF rect = layout->blockBoundingRect(document->findBlockByNumber(line));
                result->decorations.append({QRectF(0, rect.top(), 0, rect.height()), removedBackground, true});
            }
        }
    }

    result->document = std::move(document);
    return result;
}

ChatTextDocumentPtr ChatTextLayout::acquire(const Request &request, bool cache)
{
    if (!cache) {
        ++m_builds;
        return build(request);
    }

    const QString key = keyFor(request);
    const auto found = m_cache.find(key);

    if (found != m_cache.end()) {
        m_order.splice(m_order.begin(), m_order, found->second);
        return found->first;
    }

    ++m_builds;
    ChatTextDocumentPtr document = build(request);
    m_order.push_front(key);
    m_cache.insert(key, {document, m_order.begin()});

    while (m_cache.size() > maximumCachedDocuments) {
        m_cache.remove(m_order.back());
        m_order.pop_back();
    }

    return document;
}

void ChatTextLayout::clear()
{
    m_cache.clear();
    m_order.clear();
}
