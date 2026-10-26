#pragma once

#include "chat_text_layout.h"

#include <QColor>
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QVariantMap>

class QTextCursor;

// Read-only chat text drawn straight from a laid-out QTextDocument.
//
// Replaces TextEdit in the transcript. A TextEdit carries a full editing
// stack (text control, cursor, undo, input method), and building one per
// message block is what made fast scrolling stutter. ChatText only lays out
// a QTextDocument (shared through ChatTextLayout's cache, so a row that comes
// back into view reuses it) and draws it, so its height is known the moment
// the row exists. It keeps the parts of TextEdit the chat uses: mouse
// selection, copy, links and the position queries the reveal animation needs.
class ChatTextItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(bool markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY wrapChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor NOTIFY linkColorChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionColorChanged)
    Q_PROPERTY(QColor selectedTextColor READ selectedTextColor WRITE setSelectedTextColor NOTIFY selectedTextColorChanged)
    Q_PROPERTY(bool selectable READ selectable WRITE setSelectable NOTIFY selectableChanged)
    Q_PROPERTY(bool cacheable READ cacheable WRITE setCacheable NOTIFY cacheableChanged)
    Q_PROPERTY(bool layoutSuspended READ layoutSuspended WRITE setLayoutSuspended NOTIFY layoutSuspendedChanged)
    // Reading styles (markdown) or syntax colors (code); see
    // ChatTextLayout::styleKey for the keys.
    Q_PROPERTY(QVariantMap styling READ styling WRITE setStyling NOTIFY stylingChanged)
    // Plain text only: the fence language to highlight with `styling`.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // How far full-width decorations (diff line tints) reach past each side.
    Q_PROPERTY(qreal decorationBleed READ decorationBleed WRITE setDecorationBleed NOTIFY decorationBleedChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY layoutChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(int length READ length NOTIFY layoutChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY layoutChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)

public:
    explicit ChatTextItem(QQuickItem *parent = nullptr);
    ~ChatTextItem() override;

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text);
    [[nodiscard]] bool markdown() const { return m_markdown; }
    void setMarkdown(bool markdown);
    [[nodiscard]] bool wrap() const { return m_wrap; }
    void setWrap(bool wrap);
    [[nodiscard]] QFont font() const { return m_font; }
    void setFont(const QFont &font);
    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &color);
    [[nodiscard]] QColor linkColor() const { return m_linkColor; }
    void setLinkColor(const QColor &color);
    [[nodiscard]] QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor &color);
    [[nodiscard]] QColor selectedTextColor() const { return m_selectedTextColor; }
    void setSelectedTextColor(const QColor &color);
    [[nodiscard]] bool selectable() const { return m_selectable; }
    void setSelectable(bool selectable);
    [[nodiscard]] bool cacheable() const { return m_cacheable; }
    void setCacheable(bool cacheable);
    [[nodiscard]] bool layoutSuspended() const { return m_layoutSuspended; }
    void setLayoutSuspended(bool suspended);
    [[nodiscard]] QVariantMap styling() const { return m_styling; }
    void setStyling(const QVariantMap &styling);
    [[nodiscard]] QString language() const { return m_language; }
    void setLanguage(const QString &language);
    [[nodiscard]] qreal decorationBleed() const { return m_decorationBleed; }
    void setDecorationBleed(qreal bleed);

    [[nodiscard]] qreal contentWidth() const;
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] int length() const;
    [[nodiscard]] int lineCount() const;
    [[nodiscard]] bool hasSelection() const { return m_selectionStart != m_selectionEnd; }
    [[nodiscard]] QString selectedText() const;

    Q_INVOKABLE QRectF positionToRectangle(int position) const;
    Q_INVOKABLE QString getText(int start, int end) const;
    Q_INVOKABLE QString linkAt(qreal x, qreal y) const;
    Q_INVOKABLE void select(int start, int end);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselect();
    Q_INVOKABLE void copy() const;

signals:
    void textChanged();
    void markdownChanged();
    void wrapChanged();
    void fontChanged();
    void colorChanged();
    void linkColorChanged();
    void selectionColorChanged();
    void selectedTextColorChanged();
    void selectableChanged();
    void cacheableChanged();
    void layoutSuspendedChanged();
    void stylingChanged();
    void languageChanged();
    void decorationBleedChanged();
    void layoutChanged();
    void selectionChanged();
    void linkActivated(const QString &link);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    enum class SelectionUnit { Character, Word, Paragraph };

    [[nodiscard]] ChatTextLayout::Request layoutRequest() const;
    void scheduleLayout();
    void relayout();
    void applyDocument(ChatTextDocumentPtr document);
    [[nodiscard]] int hitTest(const QPointF &point) const;
    [[nodiscard]] QTextCursor cursor(int start, int end) const;
    void setSelection(int start, int end);
    void extendSelection(int position);
    [[nodiscard]] QString rawText() const;

    QString m_text;
    bool m_markdown = true;
    bool m_wrap = true;
    QFont m_font;
    QColor m_color = Qt::black;
    QColor m_linkColor = QColor(0x2f, 0x6f, 0xd6);
    QColor m_selectionColor = QColor(0x33, 0x99, 0xff, 0x66);
    QColor m_selectedTextColor;
    bool m_selectable = true;
    bool m_cacheable = true;
    bool m_layoutSuspended = false;
    bool m_layoutDirty = false;
    QVariantMap m_styling;
    QString m_styleKey;
    QString m_language;
    qreal m_decorationBleed = 0;

    ChatTextDocumentPtr m_document;
    mutable QString m_rawText;
    mutable bool m_rawTextValid = false;

    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    int m_anchorStart = 0;
    int m_anchorEnd = 0;
    SelectionUnit m_unit = SelectionUnit::Character;
    QPointF m_pressPosition;
    QString m_pressedLink;
    bool m_dragging = false;
    bool m_pressed = false;
    qint64 m_lastClickTime = 0;
    QPointF m_lastClickPosition;
    int m_clickCount = 0;

    static QPointer<ChatTextItem> s_selectionOwner;
};
