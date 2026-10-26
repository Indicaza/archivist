#include "App/Domains/Chat/chat_message_model.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

namespace
{
QVariantMap message(const QString &id, const QString &content)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("content"), content},
    };
}
}

class ChatMessageModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void appendsPrependsAndUpdatesWithoutReset()
    {
        ChatMessageModel model;
        QAbstractItemModelTester tester(
            &model,
            QAbstractItemModelTester::FailureReportingMode::QtTest
        );
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        model.sync({message("a", "1"), message("b", "2")});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(inserted.count(), 1);

        model.sync({message("a", "1"), message("b", "2"), message("c", "3")});
        QCOMPARE(inserted.count(), 2);
        QCOMPARE(inserted.last().at(1).toInt(), 2);

        model.sync({
            message("x", "0"),
            message("y", "0"),
            message("a", "1"),
            message("b", "2"),
            message("c", "3"),
        });
        QCOMPARE(inserted.count(), 3);
        QCOMPARE(inserted.last().at(1).toInt(), 0);
        QCOMPARE(inserted.last().at(2).toInt(), 1);

        model.sync({
            message("x", "0"),
            message("y", "0"),
            message("a", "1"),
            message("b", "2"),
            message("c", "3 more"),
        });
        QCOMPARE(changed.count(), 1);
        QCOMPARE(inserted.count(), 3);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(
            model.data(model.index(4), ChatMessageModel::MessageDataRole)
                .toMap()
                .value(QStringLiteral("content"))
                .toString(),
            QStringLiteral("3 more")
        );
        QCOMPARE(reset.count(), 0);
    }

    void replacesPlaceholdersInPlace()
    {
        ChatMessageModel model;
        QAbstractItemModelTester tester(
            &model,
            QAbstractItemModelTester::FailureReportingMode::QtTest
        );
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.sync({message("a", "1"), message("", "pending")});
        model.sync({message("a", "1"), message("b", "stored")});

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(
            model.data(model.index(1), ChatMessageModel::MessageIdRole).toString(),
            QStringLiteral("b")
        );
        QCOMPARE(reset.count(), 0);
    }

    void labelsStretchesOfConversation()
    {
        const auto at = [](const QString &id, const QString &createdAt) {
            return QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("content"), id},
                {QStringLiteral("createdAt"), createdAt},
            };
        };
        ChatMessageModel model;
        QAbstractItemModelTester tester(
            &model,
            QAbstractItemModelTester::FailureReportingMode::QtTest
        );
        const auto section = [&model](int row) {
            return model.data(model.index(row), ChatMessageModel::TimeSectionRole).toString();
        };
        const auto breakAfter = [&model](int row) {
            return model.data(model.index(row), ChatMessageModel::SectionBreakAfterRole).toString();
        };

        model.sync({
            at("a", "2026-03-02T10:00:00.000Z"),
            at("b", "2026-03-02T10:20:00.000Z"),
            at("c", "2026-03-02T11:05:00.000Z"),
        });
        QVERIFY(!section(0).isEmpty());
        // Twenty minutes later is the same stretch; 45 minutes is a new one.
        QCOMPARE(section(1), section(0));
        QVERIFY(section(2) != section(1));
        // The divider hangs below the row before the pause.
        QVERIFY(breakAfter(0).isEmpty());
        QCOMPARE(breakAfter(1), section(2));
        QVERIFY(breakAfter(2).isEmpty());

        // Older history joins the first stretch to its own.
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.sync({
            at("z", "2026-03-02T09:50:00.000Z"),
            at("a", "2026-03-02T10:00:00.000Z"),
            at("b", "2026-03-02T10:20:00.000Z"),
            at("c", "2026-03-02T11:05:00.000Z"),
        });
        QCOMPARE(section(1), section(0));
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.last().at(2).value<QList<int>>().contains(int(ChatMessageModel::TimeSectionRole)));
        // Rows already loaded keep their dividers, so their heights stay put.
        for (const QList<QVariant> &signal : changed) {
            QVERIFY(!signal.at(2).value<QList<int>>().contains(int(ChatMessageModel::SectionBreakAfterRole)));
        }
        QVERIFY(breakAfter(0).isEmpty());
        QCOMPARE(breakAfter(2), section(3));

        // History from an hour earlier ends with a divider on the new row.
        model.sync({
            at("y", "2026-03-02T08:40:00.000Z"),
            at("z", "2026-03-02T09:50:00.000Z"),
            at("a", "2026-03-02T10:00:00.000Z"),
            at("b", "2026-03-02T10:20:00.000Z"),
            at("c", "2026-03-02T11:05:00.000Z"),
        });
        QCOMPARE(breakAfter(0), section(1));

        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-03-02T12:00:00"), Qt::ISODate);
        QVERIFY(ChatMessageModel::sectionLabel(now.addSecs(-3600), now).startsWith(QStringLiteral("Today")));
        QVERIFY(ChatMessageModel::sectionLabel(now.addDays(-1), now).startsWith(QStringLiteral("Yesterday")));
    }

    void resetsWhenNothingIsShared()
    {
        ChatMessageModel model;
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.sync({message("a", "1")});
        model.sync({message("q", "9")});
        QCOMPARE(reset.count(), 1);

        model.sync({});
        QCOMPARE(model.rowCount(), 0);
    }
};

QTEST_GUILESS_MAIN(ChatMessageModelTest)
#include "chat_message_model_test.moc"
