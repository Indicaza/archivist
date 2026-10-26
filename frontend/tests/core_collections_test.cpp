#include "Core/Domains/AppState/app_state_repository.h"
#include "Core/Domains/Collections/collection_repository.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreCollectionsTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_libraryId;
    QString m_chatId;
    QString m_agentId;

    QString insertLibrary(const QString &name)
    {
        const QString id = newId();
        m_database.prepare(QStringLiteral("INSERT INTO libraries (id, name, root_path) VALUES (?, ?, ?)"))
            .bind(1, id)
            .bind(2, name)
            .bind(3, m_directory.filePath(name))
            .run();
        return id;
    }

    template<typename Work>
    QString errorFrom(Work &&work)
    {
        try {
            work();
        } catch (const CoreError &error) {
            return error.message();
        }

        return {};
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();

        m_libraryId = insertLibrary(QStringLiteral("code"));
        m_chatId = newId();
        m_database.prepare(QStringLiteral(
                         "INSERT INTO chats (id, library_id, title, type) VALUES (?, ?, 'Chat', 'standard')"
                     ))
            .bind(1, m_chatId)
            .bind(2, m_libraryId)
            .run();
        m_agentId = m_database.prepare(QStringLiteral("SELECT id FROM agents LIMIT 1")).scalar().toString();
        QVERIFY(!m_agentId.isEmpty());
    }

    void cleanup()
    {
        m_database.close();
    }

    void createsUpdatesAndScopesCollections()
    {
        QVariantMap input;
        input.insert(QStringLiteral("name"), QStringLiteral("  Archivist  "));
        input.insert(QStringLiteral("libraryIds"), QVariantList{m_libraryId, m_libraryId});
        input.insert(QStringLiteral("agentIds"), QVariantList{m_agentId});
        input.insert(QStringLiteral("defaultAgentId"), m_agentId);
        input.insert(QStringLiteral("parentCollectionId"), QString());

        const QVariantMap parent = CollectionRepository::createCollection(m_database, input);
        const QString parentId = parent.value(QStringLiteral("id")).toString();
        QVERIFY(isUuid(parentId));
        QCOMPARE(parent.value(QStringLiteral("name")).toString(), QStringLiteral("Archivist"));
        QCOMPARE(parent.value(QStringLiteral("libraryIds")).toList().size(), 1);
        QCOMPARE(parent.value(QStringLiteral("defaultAgentId")).toString(), m_agentId);
        QCOMPARE(parent.value(QStringLiteral("parentCollectionId")).typeId(), QMetaType::Nullptr);

        QVariantMap childInput;
        childInput.insert(QStringLiteral("name"), QStringLiteral("Docs"));
        childInput.insert(QStringLiteral("parentCollectionId"), parentId);
        childInput.insert(QStringLiteral("chatIds"), QVariantList{m_chatId});
        const QVariantMap child = CollectionRepository::createCollection(m_database, childInput);
        QCOMPARE(child.value(QStringLiteral("path")).toString(), QStringLiteral("Archivist / Docs"));

        const QVariantList active = CollectionRepository::activeCollections(m_database);
        QCOMPARE(active.size(), 2);
        QCOMPARE(active.first().toMap().value(QStringLiteral("path")).toString(), QStringLiteral("Archivist"));

        const QVariantMap scope = CollectionRepository::scope(m_database, parentId);
        QCOMPARE(scope.value(QStringLiteral("collectionIds")).toList().size(), 2);
        QCOMPARE(scope.value(QStringLiteral("libraryIds")).toList(), QVariantList{m_libraryId});
        QCOMPARE(scope.value(QStringLiteral("directChatIds")).toList(), QVariantList{m_chatId});
        QCOMPARE(scope.value(QStringLiteral("chatIds")).toList(), QVariantList{m_chatId});
        QCOMPARE(scope.value(QStringLiteral("defaultAgentId")).toString(), m_agentId);

        const QString cycle = errorFrom([&]() {
            QVariantMap move;
            move.insert(QStringLiteral("parentCollectionId"), child.value(QStringLiteral("id")));
            CollectionRepository::updateCollection(m_database, parentId, move);
        });
        QCOMPARE(cycle, QStringLiteral("A Collection cannot be moved beneath one of its descendants."));

        QVariantMap rosterOnly;
        rosterOnly.insert(QStringLiteral("agentIds"), QVariantList{});
        const QVariantMap updated = CollectionRepository::updateCollection(m_database, parentId, rosterOnly);
        QCOMPARE(updated.value(QStringLiteral("agentIds")).toList().size(), 0);
        QCOMPARE(updated.value(QStringLiteral("defaultAgentId")).typeId(), QMetaType::Nullptr);
        QCOMPARE(updated.value(QStringLiteral("name")).toString(), QStringLiteral("Archivist"));

        const QString missing = errorFrom([&]() {
            QVariantMap badLibrary;
            badLibrary.insert(QStringLiteral("libraryIds"), QVariantList{newId()});
            CollectionRepository::updateCollection(m_database, parentId, badLibrary);
        });
        QCOMPARE(missing, QStringLiteral("One or more Collection Library references are unavailable."));
    }

    void selectsArchivesAndRestores()
    {
        QVariantMap input;
        input.insert(QStringLiteral("name"), QStringLiteral("Work"));
        input.insert(QStringLiteral("libraryIds"), QVariantList{m_libraryId});
        const QString collectionId =
            CollectionRepository::createCollection(m_database, input).value(QStringLiteral("id")).toString();

        const QVariantMap state = AppStateRepository::setSelectedCollection(m_database, collectionId);
        QCOMPARE(state.value(QStringLiteral("selectedCollectionId")).toString(), collectionId);
        QCOMPARE(state.value(QStringLiteral("selectedLibraryId")).toString(), m_libraryId);
        QCOMPARE(state.value(QStringLiteral("selectedChatId")).toString(), m_chatId);

        const QVariantMap archived = CollectionRepository::archiveCollection(m_database, collectionId);
        QCOMPARE(archived.value(QStringLiteral("selectedCollectionId")).typeId(), QMetaType::Nullptr);
        QVERIFY(!isNullish(archived.value(QStringLiteral("collection")).toMap().value(QStringLiteral("archivedAt"))));
        QCOMPARE(CollectionRepository::activeCollections(m_database).size(), 0);
        QCOMPARE(CollectionRepository::archivedCollections(m_database).size(), 1);

        const QVariantMap restored = CollectionRepository::restoreCollection(m_database, collectionId);
        QCOMPARE(restored.value(QStringLiteral("archivedAt")).typeId(), QMetaType::Nullptr);

        const QString otherLibrary = insertLibrary(QStringLiteral("docs"));
        QCOMPARE(
            AppStateRepository::setSelectedLibrary(m_database, otherLibrary)
                .value(QStringLiteral("selectedLibraryId"))
                .toString(),
            otherLibrary
        );
        QCOMPARE(
            errorFrom([&]() { AppStateRepository::setSelectedLibrary(m_database, newId()); }),
            QStringLiteral("Library not found.")
        );
    }
};

QTEST_GUILESS_MAIN(CoreCollectionsTest)
#include "core_collections_test.moc"
