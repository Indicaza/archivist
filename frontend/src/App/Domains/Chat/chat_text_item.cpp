#include "chat_text_item.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <algorithm>

QPointer<ChatTextItem> ChatTextItem::s_selectionOwner;

ChatTextItem::ChatTextItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(false);
    setCursor(Qt::IBeamCursor);
}

ChatTextItem::~ChatTextItem()
{
    if (s_selectionOwner == this) {
        s_selectionOwner = nullptr;
    }
}

// --- properties ------------------------------------------------------------

void ChatTextItem::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }

    m_text = text;
    emit textChanged();
    scheduleLayout();
}

void ChatTextItem::setMarkdown(bool markdown)
{
    if (m_markdown == markdown) {
        return;
    }

    m_markdown = markdown;
    emit markdownChanged();
    scheduleLayout();
}

void ChatTextItem::setWrap(bool wrap)
{
    if (m_wrap == wrap) {
        return;
    }

    m_wrap = wrap;
    emit wrapChanged();
    scheduleLayout();
}

void ChatTextItem::setFont(const QFont &font)
{
    if (m_font == font && m_font.resolveMask() == font.resolveMask()) {
        return;
    }

    m_font = font;
    emit fontChanged();
    scheduleLayout();
}

#define CHAT_TEXT_COLOR_SETTER(setter, member, signal) \
    void ChatTextItem::setter(const QColor &color)     \
    {                                                  \
        if (member == color) {                         \
            return;                                    \
        }                                              \
        member = color;                                \
        emit signal();                                 \
        update();                                      \
    }

CHAT_TEXT_COLOR_SETTER(setColor, m_color, colorChanged)
CHAT_TEXT_COLOR_SETTER(setLinkColor, m_linkColor, linkColorChanged)
CHAT_TEXT_COLOR_SETTER(setSelectionColor, m_selectionColor, selectionColorChanged)
CHAT_TEXT_COLOR_SETTER(setSelectedTextColor, m_selectedTextColor, selectedTextColorChanged)

#undef CHAT_TEXT_COLOR_SETTER

void ChatTextItem::setSelectable(bool selectable)
{
    if (m_selectable == selectable) {
        return;
    }

    m_selectable = selectable;

    if (!selectable) {
        deselect();
    }

    setCursor(selectable ? Qt::IBeamCursor : Qt::ArrowCursor);
    emit selectableChanged();
}

void ChatTextItem::setLayoutSuspended(bool suspended)
{
    if (m_layoutSuspended == suspended) {
        return;
    }

    m_layoutSuspended = suspended;
    emit layoutSuspendedChanged();

    if (!suspended && m_layoutDirty) {
        relayout();
    }
}

void ChatTextItem::setStyling(const QVariantMap &styling)
{
    const QString key = ChatTextLayout::styleKey(styling);

    if (key == m_styleKey) {
        return;
    }

    m_styling = styling;
    m_styleKey = key;
    emit stylingChanged();
    scheduleLayout();
}

void ChatTextItem::setLanguage(const QString &language)
{
    if (m_language == language) {
        return;
    }

    m_language = language;
    emit languageChanged();

    if (!m_markdown) {
        scheduleLayout();
    }
}

void ChatTextItem::setDecorationBleed(qreal bleed)
{
    if (qFuzzyCompare(m_decorationBleed + 1, bleed + 1)) {
        return;
    }

    m_decorationBleed = bleed;
    emit decorationBleedChanged();
    update();
}

void ChatTextItem::setCacheable(bool cacheable)
{
    if (m_cacheable == cacheable) {
        return;
    }

    m_cacheable = cacheable;
    emit cacheableChanged();
}

qreal ChatTextItem::contentWidth() const
{
    return m_document ? (m_wrap ? m_document->size.width() : m_document->idealWidth) : 0;
}

qreal ChatTextItem::contentHeight() const
{
    return m_document ? m_document->size.height() : 0;
}

int ChatTextItem::length() const
{
    return m_document ? m_document->length : 0;
}

int ChatTextItem::lineCount() const
{
    return m_document ? m_document->lineCount : 0;
}

// --- layout ----------------------------------------------------------------

ChatTextLayout::Request ChatTextItem::layoutRequest() const
{
    return {m_text, m_markdown, m_wrap, m_font, m_wrap ? std::max<qreal>(0, width()) : -1,
            m_markdown ? QString() : m_language, m_styling, m_styleKey};
}

void ChatTextItem::scheduleLayout()
{
    // Before the component completes more properties are still arriving;
    // componentComplete lays out once with all of them.
    if (!isComponentComplete()) {
        return;
    }

    // While a recycled row is being rebound, its text, width and font
    // change one at a time; layoutSuspended collects them into one layout
    // when the rebind finishes (ChatMessage sets it from ListView.onPooled /
    // onReused). Otherwise layout is synchronous: a row must have its final
    // height before the ListView positions the rows after it. Deferring it,
    // even to polish, let the list lay out with the previous message's height
    // and then shift every row below when the real height arrived.
    if (m_layoutSuspended) {
        m_layoutDirty = true;
        return;
    }

    relayout();
}

void ChatTextItem::componentComplete()
{
    QQuickItem::componentComplete();
    // A new row needs its real height now, so this lays out synchronously
    // (a cache hit when the message was shown before).
    scheduleLayout();
}

void ChatTextItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (m_wrap && !qFuzzyCompare(newGeometry.width() + 1, oldGeometry.width() + 1)) {
        scheduleLayout();
    }
}

void ChatTextItem::relayout()
{
    m_layoutDirty = false;
    const ChatTextLayout::Request request = layoutRequest();

    if (m_text.isEmpty() || (m_wrap && request.width <= 0)) {
        applyDocument(nullptr);
        return;
    }

    applyDocument(ChatTextLayout::instance().acquire(request, m_cacheable));
}

void ChatTextItem::applyDocument(ChatTextDocumentPtr document)
{

    if (document == m_document) {
        return;
    }

    const int previousLength = length();
    m_document = std::move(document);
    m_rawTextValid = false;

    if (hasSelection() && (previousLength != length() || m_selectionEnd > length())) {
        setSelection(0, 0);
    }

    setImplicitSize(contentWidth(), contentHeight());

    emit layoutChanged();

    update();
}

// --- painting --------------------------------------------------------------

namespace
{
// Decorations under the text, then the text itself.
class ChatTextRootNode final : public QSGNode
{
public:
    QSGNode *decorations = nullptr;
    QSGTextNode *text = nullptr;
};
}

QSGNode *ChatTextItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_document || !window()) {
        delete oldNode;
        return nullptr;
    }

    auto *root = static_cast<ChatTextRootNode *>(oldNode);

    if (!root) {
        root = new ChatTextRootNode;
        root->decorations = new QSGNode;
        root->text = window()->createTextNode();
        root->appendChildNode(root->decorations);
        root->appendChildNode(root->text);
    }

    while (QSGNode *child = root->decorations->firstChild()) {
        root->decorations->removeChildNode(child);
        delete child;
    }

    for (const ChatTextDecoration &decoration : m_document->decorations) {
        QRectF rect = decoration.rect;

        if (decoration.fullWidth) {
            rect.setLeft(-m_decorationBleed);
            rect.setWidth(width() + 2 * m_decorationBleed);
        }

        QSGRectangleNode *shape = window()->createRectangleNode();
        shape->setRect(rect);
        shape->setColor(decoration.color);
        root->decorations->appendChildNode(shape);
    }

    QSGTextNode *node = root->text;
    node->clear();
    node->setRenderType(QSGTextNode::NativeRendering);
    node->setColor(m_color);
    node->setLinkColor(m_linkColor);
    node->setSelectionColor(m_selectionColor);
    node->setSelectionTextColor(m_selectedTextColor.isValid() ? m_selectedTextColor : m_color);
    node->setViewport(clipRect());

    const int selectionStart = std::min(m_selectionStart, m_selectionEnd);
    const int selectionCount = std::abs(m_selectionEnd - m_selectionStart);
    node->addTextDocument(QPointF(0, 0), m_document->document.get(),
                          selectionCount > 0 ? selectionStart : -1,
                          selectionCount > 0 ? selectionCount : -1);
    return root;
}

// --- queries ---------------------------------------------------------------

int ChatTextItem::hitTest(const QPointF &point) const
{
    if (!m_document) {
        return 0;
    }

    const int position = m_document->document->documentLayout()->hitTest(point, Qt::FuzzyHit);
    return std::clamp(position, 0, length());
}

QTextCursor ChatTextItem::cursor(int start, int end) const
{
    // Cursors only read here, but QTextCursor needs a mutable document.
    QTextCursor result(const_cast<QTextDocument *>(m_document->document.get()));
    result.setPosition(std::clamp(start, 0, length()));
    result.setPosition(std::clamp(end, 0, length()), QTextCursor::KeepAnchor);
    return result;
}

QString ChatTextItem::rawText() const
{
    if (!m_rawTextValid) {
        m_rawText = m_document ? m_document->document->toRawText() : QString();
        m_rawText.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        m_rawText.replace(QChar::LineSeparator, QLatin1Char('\n'));
        m_rawTextValid = true;
    }

    return m_rawText;
}

QString ChatTextItem::selectedText() const
{
    if (!m_document || !hasSelection()) {
        return {};
    }

    QString text = cursor(m_selectionStart, m_selectionEnd).selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    text.replace(QChar::Nbsp, QLatin1Char(' '));
    // The arrows after links are decoration, not text.
    text.remove(QStringLiteral("\u2009\u2197"));
    return text;
}

QRectF ChatTextItem::positionToRectangle(int position) const
{
    if (!m_document) {
        return {};
    }

    const QTextDocument *document = m_document->document.get();
    const QTextBlock block = document->findBlock(std::clamp(position, 0, length()));

    if (!block.isValid() || !block.layout()) {
        return {};
    }

    const QTextLayout *layout = block.layout();
    const int offset = position - block.position();
    const QTextLine line = layout->lineForTextPosition(std::clamp(offset, 0, std::max(0, block.length() - 1)));
    const QPointF origin = layout->position();

    if (!line.isValid()) {
        return QRectF(origin, QSizeF(1, document->documentLayout()->blockBoundingRect(block).height()));
    }

    return QRectF(origin.x() + line.cursorToX(offset), origin.y() + line.y(), 1, line.height());
}

QString ChatTextItem::getText(int start, int end) const
{
    const QString text = rawText();
    const int from = std::clamp(std::min(start, end), 0, int(text.size()));
    const int to = std::clamp(std::max(start, end), 0, int(text.size()));
    return text.mid(from, to - from);
}

QString ChatTextItem::linkAt(qreal x, qreal y) const
{
    return m_document ? m_document->document->documentLayout()->anchorAt(QPointF(x, y)) : QString();
}

// --- selection -------------------------------------------------------------

void ChatTextItem::setSelection(int start, int end)
{
    start = std::clamp(start, 0, length());
    end = std::clamp(end, 0, length());

    if (start == m_selectionStart && end == m_selectionEnd) {
        return;
    }

    m_selectionStart = start;
    m_selectionEnd = end;

    if (start != end) {
        // One selection in the transcript at a time, as with a text view.
        if (s_selectionOwner && s_selectionOwner != this) {
            s_selectionOwner->deselect();
        }

        s_selectionOwner = this;
    } else if (s_selectionOwner == this) {
        s_selectionOwner = nullptr;
    }

    emit selectionChanged();
    update();
}

void ChatTextItem::select(int start, int end)
{
    setSelection(start, end);
}

void ChatTextItem::selectAll()
{
    setSelection(0, length());
}

void ChatTextItem::deselect()
{
    setSelection(0, 0);
}

void ChatTextItem::copy() const
{
    if (!m_document || !hasSelection()) {
        return;
    }

    auto *mime = new QMimeData;
    mime->setText(selectedText());

    if (m_markdown) {
        mime->setHtml(cursor(m_selectionStart, m_selectionEnd).selection().toHtml());
    }

    QGuiApplication::clipboard()->setMimeData(mime);
}

void ChatTextItem::extendSelection(int position)
{
    if (!m_document) {
        return;
    }

    int start = std::min(position, m_anchorStart);
    int end = std::max(position, m_anchorEnd);

    if (m_unit != SelectionUnit::Character) {
        const auto moveStart = m_unit == SelectionUnit::Word ? QTextCursor::StartOfWord : QTextCursor::StartOfBlock;
        const auto moveEnd = m_unit == SelectionUnit::Word ? QTextCursor::EndOfWord : QTextCursor::EndOfBlock;

        QTextCursor startCursor = cursor(start, start);
        startCursor.movePosition(moveStart);
        QTextCursor endCursor = cursor(end, end);
        endCursor.movePosition(moveEnd);
        start = std::min(startCursor.position(), m_anchorStart);
        end = std::max(endCursor.position(), m_anchorEnd);
    }

    // Keep the selection's direction so shift-extension behaves.
    if (position < m_anchorStart) {
        setSelection(end, start);
    } else {
        setSelection(start, end);
    }
}

// --- input -----------------------------------------------------------------

bool ChatTextItem::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);

        if ((key->matches(QKeySequence::Copy) && hasSelection()) || key->matches(QKeySequence::SelectAll)) {
            event->accept();
            return true;
        }
    }

    return QQuickItem::event(event);
}

void ChatTextItem::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copy();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }

    QQuickItem::keyPressEvent(event);
}

void ChatTextItem::mousePressEvent(QMouseEvent *event)
{
    if (!m_document || (!m_selectable && linkAt(event->position().x(), event->position().y()).isEmpty())) {
        event->ignore();
        return;
    }

    const QPointF point = event->position();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto *hints = QGuiApplication::styleHints();
    const bool repeated = now - m_lastClickTime < hints->mouseDoubleClickInterval()
        && (point - m_lastClickPosition).manhattanLength() < hints->startDragDistance();
    m_clickCount = repeated ? std::min(m_clickCount + 1, 3) : 1;
    m_lastClickTime = now;
    m_lastClickPosition = point;

    m_pressPosition = point;
    m_pressed = true;
    m_dragging = false;
    m_pressedLink = m_clickCount == 1 ? linkAt(point.x(), point.y()) : QString();

    if (m_selectable) {
        forceActiveFocus(Qt::MouseFocusReason);
        const int position = hitTest(point);

        if (m_clickCount == 1 && (event->modifiers() & Qt::ShiftModifier) && hasSelection()) {
            m_unit = SelectionUnit::Character;
            m_anchorStart = m_anchorEnd = m_selectionStart;
            extendSelection(position);
        } else if (m_clickCount == 1) {
            m_unit = SelectionUnit::Character;
            m_anchorStart = m_anchorEnd = position;
            // Clearing waits for release, so a plain click on a link or a
            // flick start does not drop an existing selection.
        } else {
            m_unit = m_clickCount == 2 ? SelectionUnit::Word : SelectionUnit::Paragraph;
            m_anchorStart = m_anchorEnd = position;
            QTextCursor start = cursor(position, position);
            start.movePosition(m_unit == SelectionUnit::Word ? QTextCursor::StartOfWord : QTextCursor::StartOfBlock);
            QTextCursor end = cursor(position, position);
            end.movePosition(m_unit == SelectionUnit::Word ? QTextCursor::EndOfWord : QTextCursor::EndOfBlock);
            m_anchorStart = start.position();
            m_anchorEnd = end.position();
            setSelection(m_anchorStart, m_anchorEnd);
            setKeepMouseGrab(true);
        }
    }

    event->accept();
}

void ChatTextItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || !m_selectable) {
        event->ignore();
        return;
    }

    if (!m_dragging
        && (event->position() - m_pressPosition).manhattanLength() < QGuiApplication::styleHints()->startDragDistance()) {
        event->accept();
        return;
    }

    if (!m_dragging) {
        m_dragging = true;
        m_pressedLink.clear();
        // A drag that starts on text selects it instead of scrolling the
        // transcript, as TextEdit with selectByMouse does.
        setKeepMouseGrab(true);
    }

    extendSelection(hitTest(event->position()));
    event->accept();
}

void ChatTextItem::mouseReleaseEvent(QMouseEvent *event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    setKeepMouseGrab(false);

    if (!wasPressed) {
        event->ignore();
        return;
    }

    if (!m_dragging && m_clickCount == 1) {
        const QString link = linkAt(event->position().x(), event->position().y());

        if (!m_pressedLink.isEmpty() && link == m_pressedLink) {
            emit linkActivated(link);
        } else if (!(event->modifiers() & Qt::ShiftModifier)) {
            deselect();
        }
    }

    if (m_dragging) {
        // A drag ends a click sequence; the next press is a fresh click.
        m_lastClickTime = 0;
    }

    m_dragging = false;
    m_pressedLink.clear();
    event->accept();
}

void ChatTextItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Repeated clicks are counted in mousePressEvent (which also sees the
    // second press), so word and paragraph selection share one path.
    event->accept();
}

void ChatTextItem::mouseUngrabEvent()
{
    m_pressed = false;
    m_dragging = false;
    m_pressedLink.clear();
    setKeepMouseGrab(false);
}

void ChatTextItem::hoverMoveEvent(QHoverEvent *event)
{
    if (m_document) {
        const bool overLink = !linkAt(event->position().x(), event->position().y()).isEmpty();
        setCursor(overLink ? Qt::PointingHandCursor : (m_selectable ? Qt::IBeamCursor : Qt::ArrowCursor));
    }

    event->ignore();
}

void ChatTextItem::focusOutEvent(QFocusEvent *event)
{
    // Selection persists like TextEdit's persistentSelection; it only clears
    // when another chat text takes it or the user clicks elsewhere in text.
    QQuickItem::focusOutEvent(event);
}
