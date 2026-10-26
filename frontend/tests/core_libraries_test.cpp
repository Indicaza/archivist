#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreLibrariesTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;

    template<typename Work>
    CoreError errorFrom(Work &&work)
    {
        try {
            work();
        } catch (const CoreError &error) {
            return error;
        }

        return CoreError(QString());
    }

    QString makeFolder(const QString &name)
    {
        const QString path = m_directory.filePath(name);
        QDir().mkpath(path);
        return path;
    }

    QVariant selectedLibraryId()
    {
        return nullable(m_database.prepare(QStringLiteral("SELECT selected_library_id FROM app_settings WHERE id = 1")).scalar());
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();
        m_database.exec(QStringLiteral("UPDATE app_settings SET selected_library_id = NULL"));
    }

    void cleanup()
    {
        m_database.close();
    }

    void createsFromAFolderAndSelectsTheFirstLibrary()
    {
        const QString folder = makeFolder(QStringLiteral("notes-") + newId());
        const QVariantMap result = LibraryRepository::createLibrary(
            m_database,
            {{QStringLiteral("rootPath"), folder + QStringLiteral("/./")}}
        );

        const QVariantMap library = result.value(QStringLiteral("library")).toMap();
        QVERIFY(isUuid(library.value(QStringLiteral("id")).toString()));
        QCOMPARE(library.value(QStringLiteral("rootPath")).toString(), QFileInfo(folder).canonicalFilePath());
        QVERIFY(library.value(QStringLiteral("name")).toString().startsWith(QStringLiteral("notes-")));
        QCOMPARE(library.value(QStringLiteral("description")).typeId(), QMetaType::Nullptr);
        QCOMPARE(result.value(QStringLiteral("selectedLibraryId")).toString(), library.value(QStringLiteral("id")).toString());

        // A second Library does not steal the selection.
        const QVariantMap second = LibraryRepository::createLibrary(
            m_database,
            {
                {QStringLiteral("rootPath"), makeFolder(newId())},
                {QStringLiteral("name"), QStringLiteral("  Code  ")},
                {QStringLiteral("description"), QStringLiteral(" Work ")},
            }
        );
        QCOMPARE(second.value(QStringLiteral("library")).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Code"));
        QCOMPARE(second.value(QStringLiteral("library")).toMap().value(QStringLiteral("description")).toString(), QStringLiteral("Work"));
        QCOMPARE(second.value(QStringLiteral("selectedLibraryId")).toString(), library.value(QStringLiteral("id")).toString());
        QCOMPARE(LibraryRepository::activeLibraries(m_database).size(), 2);
    }

    void rejectsBadFoldersAndDuplicates()
    {
        QCOMPARE(
            errorFrom([&] { LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_directory.filePath(newId())}}); }).message(),
            QStringLiteral("The selected folder does not exist.")
        );

        const QString file = m_directory.filePath(newId() + QStringLiteral(".txt"));
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.close();
        QCOMPARE(
            errorFrom([&] { LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), file}}); }).message(),
            QStringLiteral("The selected path is not a folder.")
        );
        QCOMPARE(
            errorFrom([&] { LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), QStringLiteral("  ")}}); }).message(),
            QStringLiteral("Invalid Library data.")
        );

        const QString folder = makeFolder(newId());
        const QString libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), folder}})
                                      .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();

        const CoreError exists = errorFrom([&] { LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), folder}}); });
        QCOMPARE(exists.message(), QStringLiteral("This folder is already registered as a Library."));
        QCOMPARE(exists.details().value(QStringLiteral("code")).toString(), QStringLiteral("LIBRARY_EXISTS"));
        QCOMPARE(exists.details().value(QStringLiteral("libraryId")).toString(), libraryId);

        LibraryRepository::archiveLibrary(m_database, libraryId);
        const CoreError archived = errorFrom([&] { LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), folder}}); });
        QCOMPARE(archived.details().value(QStringLiteral("code")).toString(), QStringLiteral("LIBRARY_ARCHIVED"));
    }

    void updatesArchivesAndRestores()
    {
        const QString firstId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), makeFolder(newId())}})
                                    .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        const QString secondId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), makeFolder(newId())}})
                                     .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();

        QCOMPARE(
            errorFrom([&] { LibraryRepository::updateLibrary(m_database, firstId, {}); }).message(),
            QStringLiteral("Invalid Library data.")
        );

        const QVariantMap renamed = LibraryRepository::updateLibrary(
            m_database,
            firstId,
            {{QStringLiteral("name"), QStringLiteral(" Renamed ")}, {QStringLiteral("description"), QStringLiteral("About")}}
        );
        QCOMPARE(renamed.value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));
        QCOMPARE(renamed.value(QStringLiteral("description")).toString(), QStringLiteral("About"));

        const QVariantMap cleared = LibraryRepository::updateLibrary(
            m_database,
            firstId,
            {{QStringLiteral("description"), QVariant::fromValue(nullptr)}}
        );
        QCOMPARE(cleared.value(QStringLiteral("description")).typeId(), QMetaType::Nullptr);
        QCOMPARE(cleared.value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));

        // Archiving the selected Library moves the selection to another one.
        QCOMPARE(selectedLibraryId().toString(), firstId);
        const QVariantMap archive = LibraryRepository::archiveLibrary(m_database, firstId);
        QCOMPARE(archive.value(QStringLiteral("selectedLibraryId")).toString(), secondId);
        QCOMPARE(selectedLibraryId().toString(), secondId);
        QCOMPARE(LibraryRepository::archivedLibraries(m_database).size(), 1);
        QCOMPARE(
            errorFrom([&] { LibraryRepository::archiveLibrary(m_database, firstId); }).message(),
            QStringLiteral("Library is already archived.")
        );

        const QVariantMap lastArchive = LibraryRepository::archiveLibrary(m_database, secondId);
        QCOMPARE(lastArchive.value(QStringLiteral("selectedLibraryId")).typeId(), QMetaType::Nullptr);

        const QVariantMap restored = LibraryRepository::restoreLibrary(m_database, firstId);
        QCOMPARE(restored.value(QStringLiteral("archivedAt")).typeId(), QMetaType::Nullptr);
        QCOMPARE(
            errorFrom([&] { LibraryRepository::restoreLibrary(m_database, firstId); }).message(),
            QStringLiteral("Library is already active.")
        );
        QCOMPARE(
            errorFrom([&] { LibraryRepository::restoreLibrary(m_database, QStringLiteral("x")); }).message(),
            QStringLiteral("Invalid Library ID.")
        );
    }
};

QTEST_GUILESS_MAIN(CoreLibrariesTest)
#include "core_libraries_test.moc"
