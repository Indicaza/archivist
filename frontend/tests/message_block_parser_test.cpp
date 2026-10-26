#include "App/Domains/Chat/message_block_parser.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

// Fixtures were produced by the JavaScript parseBlocks() that RichMessageContent.qml
// used before the parser moved to C++, so this pins the output to match it.
class MessageBlockParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void matchesTheQmlParser()
    {
        QFile file(QStringLiteral(ARCHIVIST_TEST_FIXTURES "/message_blocks.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
        QVERIFY(cases.size() > 100);

        for (const QJsonValue &value : cases) {
            const QJsonObject testCase = value.toObject();
            const QString input = testCase.value(QStringLiteral("input")).toString();
            const QByteArray expected = QJsonDocument(testCase.value(QStringLiteral("blocks")).toArray()).toJson(QJsonDocument::Compact);
            const QByteArray actual =
                QJsonDocument(QJsonArray::fromVariantList(MessageBlockParser::parseBlocks(input))).toJson(QJsonDocument::Compact);

            if (actual != expected) {
                qWarning().noquote() << "input:" << QJsonDocument(QJsonArray{input}).toJson(QJsonDocument::Compact);
                qWarning().noquote() << "expected:" << expected;
                qWarning().noquote() << "actual:  " << actual;
            }

            QCOMPARE(actual, expected);
        }
    }

    void readsFileNamesFromFences()
    {
        const auto first = [](const QString &input) {
            return MessageBlockParser::parseBlocks(input).value(0).toMap();
        };

        QCOMPARE(first(QStringLiteral("```ts src/app.ts\nlet a = 1\n```")).value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("src/app.ts"));
        const QVariantMap colon = first(QStringLiteral("```python:tools/run.py\nprint(1)\n```"));
        QCOMPARE(colon.value(QStringLiteral("language")).toString(), QStringLiteral("python"));
        QCOMPARE(colon.value(QStringLiteral("fileName")).toString(), QStringLiteral("tools/run.py"));
        QCOMPARE(first(QStringLiteral("```js title=\"app.js\"\nx()\n```")).value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("app.js"));
        // Plain fences carry no file name at all.
        QVERIFY(!first(QStringLiteral("```python extra\nx = 1\n```")).contains(QStringLiteral("fileName")));
    }

    void parsesALongStreamingReplyQuickly()
    {
        QString reply;

        for (int index = 0; index < 400; ++index) {
            reply += QStringLiteral("Paragraph %1 with some **markdown** and a | pipe.\n\n").arg(index);

            if (index % 40 == 0) {
                reply += QStringLiteral("```cpp\nint main() { return %1; }\n```\n\n").arg(index);
            }
        }

        QElapsedTimer timer;
        timer.start();
        const QVariantList blocks = MessageBlockParser::parseBlocks(reply);
        QVERIFY(blocks.size() > 10);
        QVERIFY2(timer.elapsed() < 50, "parsing a ~25 KB reply should take a few milliseconds");
    }
};

QTEST_GUILESS_MAIN(MessageBlockParserTest)
#include "message_block_parser_test.moc"
