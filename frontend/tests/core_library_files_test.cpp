#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Libraries/library_text_chunker.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreLibraryFilesTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_root;
    QString m_libraryId;

    void writeFile(const QString &relativePath, const QByteArray &content)
    {
        const QString path = QDir(m_root).filePath(relativePath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QVariantMap fileNamed(const QVariantList &files, const QString &relativePath)
    {
        for (const QVariant &value : files) {
            if (value.toMap().value(QStringLiteral("relativePath")).toString() == relativePath) {
                return value.toMap();
            }
        }

        return {};
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
        m_root = m_directory.filePath(newId());
        QDir().mkpath(m_root);
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();

        writeFile(QStringLiteral("README.md"), "# Archivist\n\nLocal first.\n");
        writeFile(QStringLiteral("src/main.cpp"), "int main() {\r\n  return 0;\r\n}\r\n");
        writeFile(QStringLiteral("src/empty.txt"), "   \n");
        writeFile(QStringLiteral("notes/.hidden.md"), "hidden but cataloged\n");
        writeFile(QStringLiteral("node_modules/pkg/index.js"), "ignored\n");
        writeFile(QStringLiteral("build/out.txt"), "ignored\n");
        writeFile(QStringLiteral(".DS_Store"), "x");
        writeFile(QStringLiteral("setup.exe"), "MZ");
        writeFile(QStringLiteral("image.png"), QByteArray("\x89PNG\0\0", 6));
        writeFile(QStringLiteral("binary.txt"), QByteArray("a\0b", 3));
        writeFile(QStringLiteral("Tool.app/Contents/Info.plist"), "<plist/>");
        writeFile(QStringLiteral("Photos.photoslibrary/originals/a.jpg"), "jpg");
        writeFile(QStringLiteral("Photos.photoslibrary/database/db.sqlite"), "db");
        QVERIFY(QFile::link(QDir(m_root).filePath(QStringLiteral("README.md")), QDir(m_root).filePath(QStringLiteral("link.md"))));

        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
    }

    void cleanup()
    {
        m_database.close();
    }

    void chunksMatchTheNodeIndexer()
    {
        QFile file(QStringLiteral(ARCHIVIST_TEST_FIXTURES "/library_chunks.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
        QVERIFY(cases.size() > 50);
        int caseIndex = 0;

        for (const QJsonValue &value : cases) {
            const QJsonObject testCase = value.toObject();
            const QJsonArray expected = testCase.value(QStringLiteral("chunks")).toArray();
            const QString fileId = expected.isEmpty()
                ? QStringLiteral("unused")
                : expected.first().toObject().value(QStringLiteral("libraryFileId")).toString();
            const auto chunks = LibraryTextChunker::chunk(
                QStringLiteral("lib-1"),
                fileId,
                testCase.value(QStringLiteral("extension")).toString(),
                testCase.value(QStringLiteral("content")).toString()
            );

            QVERIFY2(chunks.size() == expected.size(), qPrintable(QStringLiteral("case %1").arg(caseIndex)));

            for (qsizetype index = 0; index < chunks.size(); ++index) {
                const QJsonObject want = expected.at(index).toObject();
                const auto &got = chunks.at(index);
                QCOMPARE(got.startLine, want.value(QStringLiteral("startLine")).toInt());
                QCOMPARE(got.endLine, want.value(QStringLiteral("endLine")).toInt());
                QCOMPARE(got.ordinal, want.value(QStringLiteral("ordinal")).toInt());
                QCOMPARE(got.estimatedTokens, want.value(QStringLiteral("estimatedTokens")).toInt());
                QCOMPARE(got.contentHash, want.value(QStringLiteral("contentHash")).toString());
                QCOMPARE(got.id, want.value(QStringLiteral("id")).toString());
            }

            ++caseIndex;
        }
    }

    void scansCatalogsAndIndexes()
    {
        const QVariantMap result = LibraryFiles::scan(m_database, m_libraryId);
        const QVariantList files = result.value(QStringLiteral("files")).toList();
        QStringList paths;

        for (const QVariant &value : files) {
            paths.append(value.toMap().value(QStringLiteral("relativePath")).toString());
        }

        QCOMPARE(
            paths,
            (QStringList{
                QStringLiteral("binary.txt"),
                QStringLiteral("image.png"),
                QStringLiteral("notes/.hidden.md"),
                QStringLiteral("Photos.photoslibrary/originals/a.jpg"),
                QStringLiteral("README.md"),
                QStringLiteral("src/empty.txt"),
                QStringLiteral("src/main.cpp"),
            })
        );

        const QVariantMap scan = result.value(QStringLiteral("scan")).toMap();
        QCOMPARE(scan.value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
        QCOMPARE(scan.value(QStringLiteral("catalogedFileCount")).toInt(), 7);
        QCOMPARE(scan.value(QStringLiteral("discoveredFileCount")).toInt(), 7);
        // link.md, .DS_Store, setup.exe, node_modules, build, Tool.app
        QCOMPARE(scan.value(QStringLiteral("ignoredEntryCount")).toInt(), 6);

        const QVariantMap readme = fileNamed(files, QStringLiteral("README.md"));
        QCOMPARE(readme.value(QStringLiteral("extension")).toString(), QStringLiteral(".md"));
        QCOMPARE(readme.value(QStringLiteral("status")).toString(), QStringLiteral("available"));
        QVERIFY(readme.value(QStringLiteral("modifiedAt")).toString().endsWith(QLatin1Char('Z')));

        const QVariantMap index = result.value(QStringLiteral("index")).toMap();
        QCOMPARE(index.value(QStringLiteral("processedFileCount")).toInt(), 5);
        QCOMPARE(index.value(QStringLiteral("indexedFileCount")).toInt(), 3);
        QCOMPARE(index.value(QStringLiteral("emptyFileCount")).toInt(), 1);
        QCOMPARE(index.value(QStringLiteral("failedFileCount")).toInt(), 1);
        QCOMPARE(
            index.value(QStringLiteral("issues")).toList().first().toMap().value(QStringLiteral("message")).toString(),
            QStringLiteral("This file appears to be binary.")
        );

        // Full-text search sees the indexed chunks through the FTS triggers.
        QCOMPARE(
            m_database.prepare(QStringLiteral("SELECT COUNT(*) FROM library_chunk_search WHERE library_chunk_search MATCH 'Archivist'"))
                .scalar()
                .toInt(),
            1
        );

        // A second scan reuses unchanged documents.
        const QVariantMap again = LibraryFiles::scan(m_database, m_libraryId).value(QStringLiteral("index")).toMap();
        QCOMPARE(again.value(QStringLiteral("unchangedFileCount")).toInt(), 4);
        QCOMPARE(again.value(QStringLiteral("indexedFileCount")).toInt(), 0);

        // Deleted files are marked missing.
        QVERIFY(QFile::remove(QDir(m_root).filePath(QStringLiteral("src/main.cpp"))));
        const QVariantList afterDelete = LibraryFiles::scan(m_database, m_libraryId).value(QStringLiteral("files")).toList();
        QCOMPARE(fileNamed(afterDelete, QStringLiteral("src/main.cpp")).value(QStringLiteral("status")).toString(), QStringLiteral("missing"));
    }

    void readsTextAndListsDirectories()
    {
        const QVariantList files = LibraryFiles::scan(m_database, m_libraryId).value(QStringLiteral("files")).toList();
        const QString mainId = fileNamed(files, QStringLiteral("src/main.cpp")).value(QStringLiteral("id")).toString();
        const QVariantMap preview = LibraryFiles::readText(m_database, m_libraryId, mainId);
        QCOMPARE(preview.value(QStringLiteral("content")).toString(), QStringLiteral("int main() {\r\n  return 0;\r\n}\r\n"));
        QCOMPARE(preview.value(QStringLiteral("lineCount")).toInt(), 4);
        QCOMPARE(preview.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-8"));

        const QString imageId = fileNamed(files, QStringLiteral("image.png")).value(QStringLiteral("id")).toString();
        QCOMPARE(
            errorFrom([&] { (void)LibraryFiles::readText(m_database, m_libraryId, imageId); }),
            QStringLiteral("This file type is not supported for text preview.")
        );

        QCOMPARE(
            LibraryFiles::directories(m_database, m_libraryId),
            (QStringList{QStringLiteral("notes"), QStringLiteral("src")})
        );

        const QVariantMap catalog = LibraryFiles::catalogWithDirectories(m_database, m_libraryId);
        QCOMPARE(catalog.value(QStringLiteral("files")).toList().size(), 7);
        QCOMPARE(catalog.value(QStringLiteral("directories")).toList().size(), 2);
        QVERIFY(!isNullish(catalog.value(QStringLiteral("latestScan"))));
    }

    void reportsGitStatus()
    {
        const QVariantMap notRepository = LibraryFiles::gitStatus(m_database, m_libraryId);
        QCOMPARE(notRepository.value(QStringLiteral("repository")).toBool(), false);

        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty()) {
            QSKIP("git is not installed");
        }

        auto git = [&](const QStringList &arguments) {
            QProcess process;
            process.setWorkingDirectory(m_root);
            process.start(QStringLiteral("git"), arguments);
            QVERIFY(process.waitForFinished(10000));
        };

        git({QStringLiteral("init"), QStringLiteral("-q")});
        git({QStringLiteral("-c"), QStringLiteral("user.email=t@t"), QStringLiteral("-c"), QStringLiteral("user.name=t"),
             QStringLiteral("add"), QStringLiteral("README.md")});
        git({QStringLiteral("-c"), QStringLiteral("user.email=t@t"), QStringLiteral("-c"), QStringLiteral("user.name=t"),
             QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("init")});
        writeFile(QStringLiteral("README.md"), "# Changed\n");

        const QVariantMap status = LibraryFiles::gitStatus(m_database, m_libraryId);
        QVERIFY(status.value(QStringLiteral("repository")).toBool());
        QVERIFY(status.value(QStringLiteral("dirty")).toBool());
        QCOMPARE(status.value(QStringLiteral("counts")).toMap().value(QStringLiteral("modified")).toInt(), 1);
        QVERIFY(status.value(QStringLiteral("counts")).toMap().value(QStringLiteral("untracked")).toInt() > 0);

        bool sawReadme = false;

        for (const QVariant &value : status.value(QStringLiteral("entries")).toList()) {
            const QVariantMap entry = value.toMap();

            if (entry.value(QStringLiteral("path")).toString() == QStringLiteral("README.md")) {
                sawReadme = true;
                QCOMPARE(entry.value(QStringLiteral("status")).toString(), QStringLiteral("modified"));
                QCOMPARE(entry.value(QStringLiteral("worktreeStatus")).toString(), QStringLiteral("M"));
            }
        }

        QVERIFY(sawReadme);
    }
};

QTEST_GUILESS_MAIN(CoreLibraryFilesTest)
#include "core_library_files_test.moc"
