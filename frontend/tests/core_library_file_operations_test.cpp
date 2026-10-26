#include "Core/Domains/Libraries/library_file_operations.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreLibraryFileOperationsTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_root;
    QString m_libraryId;

    QString pathOf(const QString &relativePath) const
    {
        return QDir(m_root).filePath(relativePath);
    }

    void writeFile(const QString &relativePath, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(pathOf(relativePath)).absolutePath());
        QFile file(pathOf(relativePath));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QByteArray readFile(const QString &relativePath) const
    {
        QFile file(pathOf(relativePath));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QVariantMap cataloged(const QString &relativePath)
    {
        const auto file = LibraryFileRepository::fileByRelativePath(m_database, m_libraryId, relativePath);
        return file ? *file : QVariantMap();
    }

    QString idOf(const QString &relativePath)
    {
        return cataloged(relativePath).value(QStringLiteral("id")).toString();
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
        m_root = QFileInfo(m_directory.path()).canonicalFilePath() + QLatin1Char('/') + newId();
        QDir().mkpath(m_root);
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();

        writeFile(QStringLiteral("README.md"), "# Archivist\n");
        writeFile(QStringLiteral("notes/todo.txt"), "one\ntwo\n");
        writeFile(QStringLiteral("image.png"), QByteArray("\x89PNG\0\0", 6));
        QDir().mkpath(pathOf(QStringLiteral("archive")));

        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        LibraryFiles::scan(m_database, m_libraryId);
    }

    void cleanup()
    {
        m_database.close();
    }

    void writesTextWhenUnchanged()
    {
        const QVariantMap file = cataloged(QStringLiteral("README.md"));
        const QString modifiedAt = file.value(QStringLiteral("modifiedAt")).toString();
        QFile::setPermissions(pathOf(QStringLiteral("README.md")), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        const QVariantMap preview = LibraryFileOperations::writeText(
            m_database, m_libraryId, file.value(QStringLiteral("id")).toString(), QStringLiteral("a\r\nb\nc"), modifiedAt
        );

        QCOMPARE(readFile(QStringLiteral("README.md")), QByteArray("a\r\nb\nc"));
        QCOMPARE(preview.value(QStringLiteral("lineCount")).toLongLong(), 3);
        QCOMPARE(preview.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-8"));
        QCOMPARE(preview.value(QStringLiteral("file")).toMap().value(QStringLiteral("sizeBytes")).toLongLong(), 6);
        QCOMPARE(
            QFile::permissions(pathOf(QStringLiteral("README.md"))) & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
        );
        QCOMPARE(
            cataloged(QStringLiteral("README.md")).value(QStringLiteral("modifiedAt")).toString(),
            JsCompat::isoTimestamp(QFileInfo(pathOf(QStringLiteral("README.md"))).lastModified())
        );
    }

    void rejectsStaleAndUnsupportedWrites()
    {
        const QString id = idOf(QStringLiteral("README.md"));

        try {
            LibraryFileOperations::writeText(m_database, m_libraryId, id, QStringLiteral("x"), QStringLiteral("2001-01-01T00:00:00.000Z"));
            QFAIL("Expected a conflict.");
        } catch (const CoreError &error) {
            QCOMPARE(error.message(), QStringLiteral("This file changed on disk after it was opened."));
            QCOMPARE(error.details().value(QStringLiteral("expectedModifiedAt")).toString(), QStringLiteral("2001-01-01T00:00:00.000Z"));
        }

        QCOMPARE(readFile(QStringLiteral("README.md")), QByteArray("# Archivist\n"));

        const QVariantMap image = cataloged(QStringLiteral("image.png"));
        QCOMPARE(
            errorFrom([&]() {
                LibraryFileOperations::writeText(
                    m_database, m_libraryId, image.value(QStringLiteral("id")).toString(), QStringLiteral("x"),
                    image.value(QStringLiteral("modifiedAt")).toString()
                );
            }),
            QStringLiteral("This file type is not supported for text editing.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::writeText(m_database, m_libraryId, id, QStringLiteral("x"), QStringLiteral("nope")); }),
            QStringLiteral("Invalid file content.")
        );
    }

    void createsFilesAndFolders()
    {
        const QVariantMap file = LibraryFileOperations::createEntry(m_database, m_libraryId, QStringLiteral("notes"), QStringLiteral(" new.md "), false);
        QCOMPARE(file.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
        QCOMPARE(file.value(QStringLiteral("relativePath")).toString(), QStringLiteral("notes/new.md"));
        QVERIFY(QFileInfo(pathOf(QStringLiteral("notes/new.md"))).isFile());
        QCOMPARE(file.value(QStringLiteral("file")).toMap().value(QStringLiteral("extension")).toString(), QStringLiteral(".md"));
        QVERIFY(!idOf(QStringLiteral("notes/new.md")).isEmpty());

        const QVariantMap folder = LibraryFileOperations::createEntry(m_database, m_libraryId, QString(), QStringLiteral("drafts"), true);
        QCOMPARE(folder.value(QStringLiteral("kind")).toString(), QStringLiteral("directory"));
        QVERIFY(folder.value(QStringLiteral("file")).isNull());
        QVERIFY(QFileInfo(pathOf(QStringLiteral("drafts"))).isDir());

        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::createEntry(m_database, m_libraryId, QString(), QStringLiteral("README.md"), false); }),
            QStringLiteral("A file or folder with that name already exists.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::createEntry(m_database, m_libraryId, QStringLiteral("../x"), QStringLiteral("a"), false); }),
            QStringLiteral("Invalid Library path.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::createEntry(m_database, m_libraryId, QString(), QStringLiteral("a/b"), false); }),
            QStringLiteral("Invalid file or folder request.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::createEntry(m_database, m_libraryId, QStringLiteral("missing"), QStringLiteral("a"), true); }),
            QStringLiteral("The destination folder does not exist.")
        );
    }

    void renamesAndDuplicates()
    {
        const QString id = idOf(QStringLiteral("notes/todo.txt"));
        const QVariantMap renamed = LibraryFileOperations::renameFile(m_database, m_libraryId, id, QStringLiteral("done.md"));
        QCOMPARE(renamed.value(QStringLiteral("relativePath")).toString(), QStringLiteral("notes/done.md"));
        QCOMPARE(renamed.value(QStringLiteral("extension")).toString(), QStringLiteral(".md"));
        QVERIFY(!QFileInfo::exists(pathOf(QStringLiteral("notes/todo.txt"))));
        QCOMPARE(readFile(QStringLiteral("notes/done.md")), QByteArray("one\ntwo\n"));

        const QVariantMap copy = LibraryFileOperations::duplicateFile(m_database, m_libraryId, id, QStringLiteral("done copy.md"));
        QVERIFY(copy.value(QStringLiteral("id")).toString() != id);
        QCOMPARE(copy.value(QStringLiteral("relativePath")).toString(), QStringLiteral("notes/done copy.md"));
        QCOMPARE(readFile(QStringLiteral("notes/done copy.md")), QByteArray("one\ntwo\n"));

        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::renameFile(m_database, m_libraryId, id, QStringLiteral("done copy.md")); }),
            QStringLiteral("A file or folder with that name already exists.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::duplicateFile(m_database, m_libraryId, id, QStringLiteral("..")); }),
            QStringLiteral("Invalid duplicate file name.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::renameFile(m_database, m_libraryId, newId(), QStringLiteral("x.md")); }),
            QStringLiteral("Library file not found.")
        );
    }

    void movesFiles()
    {
        const QString id = idOf(QStringLiteral("README.md"));
        const QVariantMap moved = LibraryFileOperations::moveFile(m_database, m_libraryId, id, QStringLiteral("/archive/"));
        QCOMPARE(moved.value(QStringLiteral("relativePath")).toString(), QStringLiteral("archive/README.md"));
        QVERIFY(QFileInfo(pathOf(QStringLiteral("archive/README.md"))).isFile());

        const QVariantMap same = LibraryFileOperations::moveFile(m_database, m_libraryId, id, QStringLiteral("archive"));
        QCOMPARE(same.value(QStringLiteral("relativePath")).toString(), QStringLiteral("archive/README.md"));

        writeFile(QStringLiteral("README.md"), "other\n");
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::moveFile(m_database, m_libraryId, id, QString()); }),
            QStringLiteral("A file named README.md already exists there.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::moveFile(m_database, m_libraryId, id, QStringLiteral("../..")); }),
            QStringLiteral("The destination must stay inside the Library.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::moveFile(m_database, m_libraryId, id, QStringLiteral("nowhere")); }),
            QStringLiteral("The destination folder does not exist.")
        );
    }

    void refusesArchivedLibraries()
    {
        LibraryRepository::archiveLibrary(m_database, m_libraryId);
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::createEntry(m_database, m_libraryId, QString(), QStringLiteral("x.md"), false); }),
            QStringLiteral("Archived Libraries cannot modify files.")
        );
        QCOMPARE(
            errorFrom([&]() { LibraryFileOperations::revealEntry(m_database, m_libraryId, QStringLiteral("README.md")); }),
            QStringLiteral("Archived Libraries cannot modify files.")
        );
    }
};

QTEST_GUILESS_MAIN(CoreLibraryFileOperationsTest)
#include "core_library_file_operations_test.moc"
