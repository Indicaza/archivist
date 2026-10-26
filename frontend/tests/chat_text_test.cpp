#include "App/Domains/Chat/chat_syntax.h"
#include "App/Domains/Chat/chat_text_item.h"
#include "App/Domains/Chat/chat_text_layout.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextDocument>
#include <QTest>
#include <memory>

namespace
{
// Same font settings as Theme.chatBodyFont, written as grouped properties so
// TextEdit and ChatText get identical fonts.
const char *const fontProperties = R"(
    font.family: "Helvetica"
    font.pixelSize: 16
    font.letterSpacing: 0.08
    font.kerning: true
    font.hintingPreference: Font.PreferNoHinting
    font.preferTypoLineMetrics: true
)";

const QString sampleMarkdown = QStringLiteral(
    "# A heading\n\nSome **bold** text with a [link](https://example.com) and `inline code`, long enough to "
    "wrap across a few lines at the width used in this test.\n\n- one\n- two\n\n> quoted\n\nLast paragraph.");
}

class ChatTextTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void matchesTextEditHeights_data();
    void matchesTextEditHeights();
    void sharesLayoutsBetweenItems();
    void reusedRowLaysOutOnceAfterAllChanges();
    void changesApplyImmediately();
    void dragSelectsAndCopies();
    void doubleClickSelectsAWord();
    void clickActivatesLinks();
    void reportsPositionsForTheRevealAnimation();
    void stylesMarkdownForReading();
    void highlightsCode();
    void tintsDiffLines();
    void tokenizesCommonLanguages();
    void linkArrowsStayOutOfCopiedText();

private:
    QQuickItem *create(const QString &body);

    QQmlEngine m_engine;
    std::unique_ptr<QQuickWindow> m_window;
};

void ChatTextTest::initTestCase()
{
    qmlRegisterType<ChatTextItem>("Archivist.Services", 1, 0, "ChatText");
}

void ChatTextTest::init()
{
    ChatTextLayout::instance().clear();
    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(800, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
}

QQuickItem *ChatTextTest::create(const QString &body)
{
    QQmlComponent component(&m_engine);
    component.setData(("import QtQuick\nimport Archivist.Services\n" + body).toUtf8(), QUrl());
    auto *item = qobject_cast<QQuickItem *>(component.create());

    if (!item) {
        qWarning() << component.errors();
        return nullptr;
    }

    item->setParentItem(m_window->contentItem());
    item->setParent(m_window.get());
    return item;
}

void ChatTextTest::matchesTextEditHeights_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("markdown");
    QTest::addColumn<bool>("wrap");

    QTest::newRow("markdown") << sampleMarkdown << true << true;
    QTest::newRow("plain wrapped") << sampleMarkdown << false << true;
    QTest::newRow("code") << QStringLiteral("int main()\n{\n    return 0; // a long line that must not wrap at all\n}") << false << false;
}

void ChatTextTest::matchesTextEditHeights()
{
    QFETCH(QString, text);
    QFETCH(bool, markdown);
    QFETCH(bool, wrap);

    const QString format = markdown ? QStringLiteral("TextEdit.MarkdownText") : QStringLiteral("TextEdit.PlainText");
    const QString wrapMode = wrap ? QStringLiteral("TextEdit.Wrap") : QStringLiteral("TextEdit.NoWrap");
    const QString font = QString::fromLatin1(fontProperties);
    QQuickItem *row = create(QStringLiteral(R"(
Item {
    property string editText
    property string chatText
    TextEdit {
        objectName: "edit"
        width: 420
        textMargin: 0
        text: parent.editText
        textFormat: %1
        wrapMode: %2
        %3
    }
    ChatText {
        objectName: "chat"
        width: 420
        text: parent.chatText
        markdown: %4
        wrap: %5
        %3
    }
})").arg(format, wrapMode, font, markdown ? QStringLiteral("true") : QStringLiteral("false"),
         wrap ? QStringLiteral("true") : QStringLiteral("false")));
    QVERIFY(row);
    // ChatText applies the display rules (images, heading sizes) itself.
    row->setProperty("editText", markdown ? ChatTextLayout::displayMarkdown(text) : text);
    row->setProperty("chatText", text);

    auto *edit = row->findChild<QQuickItem *>("edit");
    auto *chat = row->findChild<QQuickItem *>("chat");
    QTRY_VERIFY(chat->property("contentHeight").toReal() > 0);
    QCOMPARE(chat->property("contentHeight").toReal(), edit->property("contentHeight").toReal());
    QCOMPARE(chat->property("length").toInt(), edit->property("length").toInt());
}

void ChatTextTest::sharesLayoutsBetweenItems()
{
    const int builds = ChatTextLayout::instance().buildCount();
    QQuickItem *row = create(QStringLiteral(R"(
Column {
    ChatText { width: 400; text: "Same **text**" }
    ChatText { width: 400; text: "Same **text**" }
    ChatText { width: 300; text: "Same **text**" }
})"));
    QVERIFY(row);
    QCOMPARE(ChatTextLayout::instance().buildCount(), builds + 2);
    QCOMPARE(ChatTextLayout::instance().cachedCount(), 2);
}

void ChatTextTest::reusedRowLaysOutOnceAfterAllChanges()
{
    QQuickItem *item = create(QStringLiteral("ChatText { width: 400; text: \"First message\" }"));
    QVERIFY(item);
    const int builds = ChatTextLayout::instance().buildCount();

    // What a recycled transcript row does: while it is rebound, several
    // properties change one at a time. Only the final combination is laid
    // out, and it is laid out the moment the rebind ends.
    item->setProperty("layoutSuspended", true);
    item->setProperty("text", QStringLiteral("Second message, which is different"));
    item->setWidth(320);
    item->setProperty("text", QStringLiteral("Third message"));
    QCOMPARE(ChatTextLayout::instance().buildCount(), builds);

    item->setProperty("layoutSuspended", false);
    QCOMPARE(ChatTextLayout::instance().buildCount(), builds + 1);
    QCOMPARE(item->property("length").toInt(), QStringLiteral("Third message").size());
    QVERIFY(item->implicitHeight() > 0);
}

void ChatTextTest::changesApplyImmediately()
{
    QQuickItem *item = create(QStringLiteral("ChatText { width: 400; text: \"Short\" }"));
    QVERIFY(item);
    const qreal oneLine = item->implicitHeight();

    // No deferral: the new height is there before anything else runs, so a
    // ListView never positions rows with a stale height.
    item->setProperty("text", QStringLiteral("Line one\n\nLine two\n\nLine three"));
    QVERIFY(item->implicitHeight() > oneLine * 2);
}

void ChatTextTest::dragSelectsAndCopies()
{
    QQuickItem *item = create(QStringLiteral("ChatText { x: 10; y: 10; width: 600; text: \"Hello selectable world\" }"));
    QVERIFY(item);
    const qreal y = item->implicitHeight() / 2;
    const QPointF start = item->mapToScene(QPointF(1, y));
    const QPointF end = item->mapToScene(QPointF(item->property("contentWidth").toReal() - 1, y));

    QTest::mousePress(m_window.get(), Qt::LeftButton, {}, start.toPoint());
    QTest::mouseMove(m_window.get(), (start + QPointF(20, 0)).toPoint());
    QTest::mouseMove(m_window.get(), end.toPoint());
    QTest::mouseRelease(m_window.get(), Qt::LeftButton, {}, end.toPoint());

    QCOMPARE(item->property("selectedText").toString(), QStringLiteral("Hello selectable world"));
    QVERIFY(QMetaObject::invokeMethod(item, "copy"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Hello selectable world"));

    // A plain click clears it again.
    QTest::mouseClick(m_window.get(), Qt::LeftButton, {}, start.toPoint());
    QVERIFY(!item->property("hasSelection").toBool());
}

void ChatTextTest::doubleClickSelectsAWord()
{
    QQuickItem *item = create(QStringLiteral("ChatText { width: 600; text: \"alpha beta gamma\" }"));
    QVERIFY(item);
    QRectF rect;
    QVERIFY(QMetaObject::invokeMethod(item, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, 8)));
    const QPoint point = item->mapToScene(rect.center()).toPoint();

    QTest::mouseDClick(m_window.get(), Qt::LeftButton, {}, point);
    QCOMPARE(item->property("selectedText").toString(), QStringLiteral("beta"));
}

void ChatTextTest::clickActivatesLinks()
{
    QQuickItem *item = create(QStringLiteral("ChatText { width: 600; text: \"[open](https://example.com) after\" }"));
    QVERIFY(item);
    QSignalSpy activated(item, SIGNAL(linkActivated(QString)));
    QRectF rect;
    QVERIFY(QMetaObject::invokeMethod(item, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, 1)));

    QTest::mouseClick(m_window.get(), Qt::LeftButton, {}, item->mapToScene(rect.center()).toPoint());
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.first().first().toString(), QStringLiteral("https://example.com"));
}

void ChatTextTest::reportsPositionsForTheRevealAnimation()
{
    QQuickItem *item = create(QStringLiteral("ChatText { width: 600; text: \"Line one\\n\\nLine two\" }"));
    QVERIFY(item);
    QString text;
    QVERIFY(QMetaObject::invokeMethod(item, "getText", Q_RETURN_ARG(QString, text), Q_ARG(int, 0), Q_ARG(int, 8)));
    QCOMPARE(text, QStringLiteral("Line one"));

    QRectF first;
    QRectF second;
    QVERIFY(QMetaObject::invokeMethod(item, "positionToRectangle", Q_RETURN_ARG(QRectF, first), Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(item, "positionToRectangle", Q_RETURN_ARG(QRectF, second), Q_ARG(int, 9)));
    QVERIFY(first.height() > 0);
    QVERIFY(second.y() > first.y());
}

namespace
{
QVariantMap readingStyle()
{
    return {
        {QStringLiteral("lineHeight"), 1.55},
        {QStringLiteral("headingFamily"), QStringLiteral("Georgia")},
        {QStringLiteral("accent"), QColor(QStringLiteral("#b7aad2"))},
        {QStringLiteral("codeBackground"), QColor(QStringLiteral("#2a2723"))},
        {QStringLiteral("linkColor"), QColor(QStringLiteral("#b7aad2"))},
        {QStringLiteral("linkUnderline"), QColor(QStringLiteral("#889280bc"))},
        {QStringLiteral("linkArrows"), true},
        {QStringLiteral("quoteBar"), QColor(QStringLiteral("#4a453d"))},
    };
}

ChatTextLayout::Request request(const QString &text, bool markdown, const QVariantMap &style, const QString &language = {})
{
    QFont font(QStringLiteral("Helvetica"));
    font.setPixelSize(16);
    ChatTextLayout::Request result;
    result.text = text;
    result.markdown = markdown;
    result.wrap = markdown;
    result.font = font;
    result.width = 420;
    result.language = language;
    result.style = style;
    result.styleKey = ChatTextLayout::styleKey(style);
    return result;
}

QTextCharFormat formatAt(const QTextDocument *document, int position)
{
    const QTextBlock block = document->findBlock(position);

    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();

        if (fragment.position() <= position && position < fragment.position() + fragment.length()) {
            return fragment.charFormat();
        }
    }

    return {};
}
}

void ChatTextTest::stylesMarkdownForReading()
{
    const auto plain = ChatTextLayout::build(request(sampleMarkdown, true, {}));
    const auto styled = ChatTextLayout::build(request(sampleMarkdown, true, readingStyle()));
    const QTextDocument *document = styled->document.get();

    // Headings: serif at a fixed size.
    const QTextCharFormat heading = formatAt(document, 0);
    QCOMPARE(heading.intProperty(QTextFormat::FontPixelSize), 22);
    QCOMPARE(heading.fontFamilies().toStringList().value(0), QStringLiteral("Georgia"));

    // Body paragraphs get the reading line height, so the text is taller.
    QCOMPARE(document->findBlockByNumber(1).blockFormat().lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
    QVERIFY(styled->size.height() > plain->size.height());

    // Inline code chips, the link underline and the quote bar are drawn as
    // decorations under the text.
    QVERIFY(styled->decorations.size() >= 3);
    QVERIFY(plain->decorations.isEmpty());

    // The style is part of the cache key.
    QVERIFY(ChatTextLayout::styleKey(readingStyle()) != ChatTextLayout::styleKey({}));
}

void ChatTextTest::highlightsCode()
{
    const QVariantMap palette{
        {QStringLiteral("keyword"), QColor(Qt::red)},
        {QStringLiteral("comment"), QColor(Qt::green)},
        {QStringLiteral("number"), QColor(Qt::blue)},
    };
    const QString code = QStringLiteral("def answer():\n    return 42  # the answer");
    const auto document = ChatTextLayout::build(request(code, false, palette, QStringLiteral("py")));

    QCOMPARE(document->lineCount, 2);
    QCOMPARE(formatAt(document->document.get(), 0).foreground().color(), QColor(Qt::red));
    QCOMPARE(formatAt(document->document.get(), int(code.indexOf(QStringLiteral("42")))).foreground().color(), QColor(Qt::blue));
    const QTextCharFormat comment = formatAt(document->document.get(), int(code.indexOf(QLatin1Char('#'))));
    QCOMPARE(comment.foreground().color(), QColor(Qt::green));
    QVERIFY(comment.fontItalic());

    // An unknown language stays plain.
    const auto plain = ChatTextLayout::build(request(code, false, palette, QStringLiteral("brainfudge")));
    QVERIFY(!formatAt(plain->document.get(), 0).foreground().color().isValid()
            || formatAt(plain->document.get(), 0).foreground().color() != QColor(Qt::red));
}

void ChatTextTest::tintsDiffLines()
{
    const QVariantMap palette{
        {QStringLiteral("addedBackground"), QColor(0, 255, 0, 40)},
        {QStringLiteral("removedBackground"), QColor(255, 0, 0, 40)},
    };
    const auto document = ChatTextLayout::build(
        request(QStringLiteral("--- a/x\n+++ b/x\n@@ -1 +1 @@\n-old\n+new\n same"), false, palette, QStringLiteral("diff")));

    QCOMPARE(document->decorations.size(), 2);
    QVERIFY(document->decorations.at(0).fullWidth);
}

void ChatTextTest::tokenizesCommonLanguages()
{
    const auto kindAt = [](const QList<ChatSyntax::Span> &spans, int position) {
        for (const ChatSyntax::Span &span : spans) {
            if (span.start <= position && position < span.start + span.length) {
                return int(span.kind);
            }
        }

        return -1;
    };

    const QString ts = QStringLiteral("const name: string = `hi ${who}` // greet");
    const auto tsSpans = ChatSyntax::tokenize(ts, QStringLiteral("tsx"));
    QCOMPARE(kindAt(tsSpans, 0), int(ChatSyntax::Kind::Keyword));
    QCOMPARE(kindAt(tsSpans, int(ts.indexOf(QLatin1Char('`')))), int(ChatSyntax::Kind::String));
    QCOMPARE(kindAt(tsSpans, int(ts.indexOf(QStringLiteral("//")))), int(ChatSyntax::Kind::Comment));

    const QString json = QStringLiteral("{\"key\": \"value\", \"n\": 3, \"ok\": true}");
    const auto jsonSpans = ChatSyntax::tokenize(json, QStringLiteral("json"));
    QCOMPARE(kindAt(jsonSpans, 1), int(ChatSyntax::Kind::Type));
    QCOMPARE(kindAt(jsonSpans, int(json.indexOf(QStringLiteral("\"value")))), int(ChatSyntax::Kind::String));
    QCOMPARE(kindAt(jsonSpans, int(json.indexOf(QStringLiteral("true")))), int(ChatSyntax::Kind::Constant));

    const QString shell = QStringLiteral("echo \"$HOME\" # where");
    const auto shellSpans = ChatSyntax::tokenize(shell, QStringLiteral("bash"));
    QCOMPARE(kindAt(shellSpans, int(shell.indexOf(QLatin1Char('#')))), int(ChatSyntax::Kind::Comment));

    const QString rust = QStringLiteral("fn main<'a>() { println!(\"x\"); }");
    const auto rustSpans = ChatSyntax::tokenize(rust, QStringLiteral("rs"));
    QCOMPARE(kindAt(rustSpans, 0), int(ChatSyntax::Kind::Keyword));
    QCOMPARE(kindAt(rustSpans, int(rust.indexOf(QStringLiteral("'a")))), int(ChatSyntax::Kind::Meta));
    QCOMPARE(kindAt(rustSpans, int(rust.indexOf(QStringLiteral("println")))), int(ChatSyntax::Kind::Function));

    QVERIFY(ChatSyntax::tokenize(QStringLiteral("anything"), QString()).isEmpty());
    // Unfinished code (mid-stream) must not trip the scanner.
    QVERIFY(!ChatSyntax::tokenize(QStringLiteral("/* open comment \"and string"), QStringLiteral("c")).isEmpty());
}

void ChatTextTest::linkArrowsStayOutOfCopiedText()
{
    QQuickItem *item = create(QStringLiteral(
        "ChatText { width: 600; text: \"[docs](https://example.com) here\"; "
        "styling: ({ linkArrows: true, linkColor: \"#b7aad2\" }) }"));
    QVERIFY(item);
    QVERIFY(QMetaObject::invokeMethod(item, "selectAll"));
    QCOMPARE(item->property("selectedText").toString(), QStringLiteral("docs here"));
}

QTEST_MAIN(ChatTextTest)
#include "chat_text_test.moc"
