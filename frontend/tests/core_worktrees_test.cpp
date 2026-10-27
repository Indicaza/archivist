#include "Core/Domains/AI/ai_run_repository.h"
#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Collections/collection_repository.h"
#include "Core/Domains/Libraries/library_file_operations.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Worktrees/git_worktrees.h"
#include "Core/Domains/Worktrees/workspace_change_journal.h"
#include "Core/Domains/Worktrees/worktree_repository.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreWorktreesTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_base;
    QString m_repo;
    QString m_libraryId;

    void git(const QString &directory, const QStringList &arguments)
    {
        QStringList full{
            QStringLiteral("-c"), QStringLiteral("user.name=Archivist Test"),
            QStringLiteral("-c"), QStringLiteral("user.email=test@archivist.local"),
            QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false"),
        };
        full.append(arguments);
        const GitWorktrees::CommandResult result = GitWorktrees::run(directory, full);
        QVERIFY2(result.ok(), qPrintable(arguments.join(QLatin1Char(' ')) + QStringLiteral(": ") + result.errors));
    }

    static void writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    template<typename Work>
    static QString errorCode(Work &&work)
    {
        try {
            work();
        } catch (const CoreError &error) {
            return error.details().value(QStringLiteral("code")).toString();
        }

        return {};
    }

    QVariantMap entryFor(const QVariantMap &overview, const QString &path)
    {
        for (const QVariant &value : overview.value(QStringLiteral("worktrees")).toList()) {
            const QVariantMap entry = value.toMap();

            if (GitWorktrees::samePath(entry.value(QStringLiteral("path")).toString(), path)
                || entry.value(QStringLiteral("path")).toString() == path) {
                return entry;
            }
        }

        return {};
    }

    QVariantMap createWorktree(const QString &branch, const QVariantMap &extra = {})
    {
        QVariantMap input = extra;
        input.insert(QStringLiteral("branch"), branch);
        return WorktreeRepository::createWorktree(m_database, m_libraryId, input);
    }

    int libraryCount()
    {
        return m_database.prepare(QStringLiteral("SELECT COUNT(*) FROM libraries")).scalar().toInt();
    }

    QStringList blockerCodes(const QVariantMap &preview)
    {
        QStringList codes;

        for (const QVariant &value : preview.value(QStringLiteral("blockers")).toList()) {
            codes.append(value.toMap().value(QStringLiteral("code")).toString());
        }

        return codes;
    }

    QStringList journalKinds(const QString &libraryId)
    {
        QStringList kinds;

        for (const QVariant &value : WorkspaceChangeJournal::eventsAfter(m_database, libraryId, 0)) {
            kinds.append(value.toMap().value(QStringLiteral("kind")).toString());
        }

        return kinds;
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(GitWorktrees::run(QDir::tempPath(), {QStringLiteral("--version")}).ok(), "git is required for these tests");
    }

    void init()
    {
        QVERIFY(m_directory.isValid());
        m_base = GitWorktrees::canonicalPath(m_directory.path()) + QLatin1Char('/') + newId();
        m_repo = m_base + QStringLiteral("/project");
        QDir().mkpath(m_repo);
        git(m_repo, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
        writeFile(m_repo + QStringLiteral("/shared.txt"), "main version\n");
        writeFile(m_repo + QStringLiteral("/docs/readme.md"), "# Docs\n");
        git(m_repo, {QStringLiteral("add"), QStringLiteral(".")});
        git(m_repo, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("initial")});

        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();
        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_repo}})
                          .value(QStringLiteral("library"))
                          .toMap()
                          .value(QStringLiteral("id"))
                          .toString();
        QVERIFY(isUuid(m_libraryId));
    }

    void cleanup()
    {
        m_database.close();
    }

    void parsesPorcelainOutput()
    {
        const QString text = QStringLiteral(
            "worktree /repo\nHEAD 1111111111111111111111111111111111111111\nbranch refs/heads/main\n\n"
            "worktree /repo.worktrees/feature\nHEAD 2222222222222222222222222222222222222222\nbranch refs/heads/feature/x\nlocked agent busy\n\n"
            "worktree /tmp/detached\nHEAD 3333333333333333333333333333333333333333\ndetached\nprunable gitdir file points to non-existent location\n\n"
        );
        const QList<GitWorktrees::Worktree> parsed = GitWorktrees::parseList(text, false);
        QCOMPARE(parsed.size(), 3);
        QVERIFY(parsed.at(0).main);
        QCOMPARE(parsed.at(0).branch, QStringLiteral("main"));
        QCOMPARE(parsed.at(1).branch, QStringLiteral("feature/x"));
        QVERIFY(parsed.at(1).locked);
        QCOMPARE(parsed.at(1).lockReason, QStringLiteral("agent busy"));
        QVERIFY(!parsed.at(1).main);
        QVERIFY(parsed.at(2).detached);
        QVERIFY(parsed.at(2).prunable);
        QVERIFY(parsed.at(2).branch.isEmpty());

        QString nul = text;
        nul.replace(QLatin1Char('\n'), QLatin1Char('\0'));
        const QList<GitWorktrees::Worktree> nulParsed = GitWorktrees::parseList(nul, true);
        QCOMPARE(nulParsed.size(), 3);
        QCOMPARE(nulParsed.at(1).lockReason, QStringLiteral("agent busy"));
    }

    void inspectsRepositoriesAndPrefixes()
    {
        const GitWorktrees::Repository top = GitWorktrees::inspect(m_repo);
        QVERIFY(top.repository);
        QCOMPARE(top.topLevel, m_repo);
        QVERIFY(top.prefix.isEmpty());
        QVERIFY(!top.linkedWorktree);
        QVERIFY(top.key.endsWith(QStringLiteral("/.git")));

        const GitWorktrees::Repository docs = GitWorktrees::inspect(m_repo + QStringLiteral("/docs"));
        QCOMPARE(docs.prefix, QStringLiteral("docs"));
        QCOMPARE(docs.key, top.key);

        const QString plain = m_base + QStringLiteral("/plain");
        QDir().mkpath(plain);
        QVERIFY(!GitWorktrees::inspect(plain).repository);
        QVERIFY(!GitWorktrees::inspect(m_base + QStringLiteral("/missing")).repository);
    }

    void discoversWorktreesCreatedOutsideArchivist()
    {
        const QString external = m_base + QStringLiteral("/external");
        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("outside"), external});

        const QVariantMap overview = WorktreeRepository::overview(m_database, m_libraryId);
        QVERIFY(overview.value(QStringLiteral("error")).isNull());
        QCOMPARE(overview.value(QStringLiteral("anchorLibraryId")).toString(), m_libraryId);
        QCOMPARE(overview.value(QStringLiteral("repository")).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("project"));
        QCOMPARE(overview.value(QStringLiteral("worktrees")).toList().size(), 2);

        const QVariantMap main = entryFor(overview, m_repo);
        QVERIFY(main.value(QStringLiteral("anchor")).toBool());
        QVERIFY(main.value(QStringLiteral("main")).toBool());
        QCOMPARE(main.value(QStringLiteral("libraryId")).toString(), m_libraryId);

        const QVariantMap outside = entryFor(overview, external);
        QCOMPARE(outside.value(QStringLiteral("branch")).toString(), QStringLiteral("outside"));
        QVERIFY(outside.value(QStringLiteral("libraryId")).isNull());
        QVERIFY(!outside.value(QStringLiteral("linked")).toBool());

        // Opening it makes it a Library linked to the anchor; opening again reuses it.
        const QVariantMap opened = WorktreeRepository::openWorktree(m_database, m_libraryId, external);
        QVERIFY(opened.value(QStringLiteral("created")).toBool());
        const QVariantMap library = opened.value(QStringLiteral("library")).toMap();
        QCOMPARE(library.value(QStringLiteral("rootPath")).toString(), GitWorktrees::canonicalPath(external));
        QCOMPARE(library.value(QStringLiteral("worktreeOfLibraryId")).toString(), m_libraryId);
        QCOMPARE(library.value(QStringLiteral("name")).toString(), QStringLiteral("project · outside"));

        const QVariantMap reopened = WorktreeRepository::openWorktree(m_database, m_libraryId, external);
        QVERIFY(!reopened.value(QStringLiteral("created")).toBool());
        QCOMPARE(reopened.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")), library.value(QStringLiteral("id")));

        const QString worktreeLibraryId = library.value(QStringLiteral("id")).toString();
        QCOMPARE(WorktreeRepository::anchorLibraryId(m_database, worktreeLibraryId), m_libraryId);
        QCOMPARE(journalKinds(worktreeLibraryId), QStringList{QStringLiteral("worktree.opened")});

        // Any Library of the repository gives the same overview.
        const QVariantMap fromWorktree = WorktreeRepository::overview(m_database, worktreeLibraryId);
        QCOMPARE(fromWorktree.value(QStringLiteral("anchorLibraryId")).toString(), m_libraryId);
        QVERIFY(entryFor(fromWorktree, external).value(QStringLiteral("linked")).toBool());

        // The main checkout opens as the anchor itself.
        QCOMPARE(
            WorktreeRepository::openWorktree(m_database, worktreeLibraryId, m_repo).value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString(),
            m_libraryId
        );

        // A folder that is not one of this repository's worktrees is refused.
        const QString unrelated = m_base + QStringLiteral("/unrelated");
        QDir().mkpath(unrelated);
        QCOMPARE(errorCode([&]() { WorktreeRepository::openWorktree(m_database, m_libraryId, unrelated); }), QStringLiteral("NOT_A_WORKTREE"));
    }

    void createsWorktreesOnNewAndExistingBranches()
    {
        const QVariantMap created = createWorktree(QStringLiteral("feature/login"));
        const QVariantMap library = created.value(QStringLiteral("library")).toMap();
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(path, m_base + QStringLiteral("/project.worktrees/feature-login"));
        QCOMPARE(readFile(path + QStringLiteral("/shared.txt")), QByteArray("main version\n"));
        QCOMPARE(library.value(QStringLiteral("worktreeOfLibraryId")).toString(), m_libraryId);
        QVERIFY(GitWorktrees::branchExists(m_repo, QStringLiteral("feature/login")));

        const auto link = WorktreeRepository::linkFor(m_database, library.value(QStringLiteral("id")).toString());
        QVERIFY(link.has_value());
        QCOMPARE(link->value(QStringLiteral("origin")).toString(), QStringLiteral("created"));
        QCOMPARE(link->value(QStringLiteral("createdBranch")).toString(), QStringLiteral("feature/login"));
        QCOMPARE(
            journalKinds(library.value(QStringLiteral("id")).toString()),
            (QStringList{QStringLiteral("worktree.opened"), QStringLiteral("worktree.created")})
        );

        git(m_repo, {QStringLiteral("branch"), QStringLiteral("topic")});
        const QVariantMap existing = createWorktree(QStringLiteral("topic"), {{QStringLiteral("newBranch"), false}});
        QCOMPARE(existing.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("branch")).toString(), QStringLiteral("topic"));

        // A second checkout of the same branch is refused before git runs.
        QCOMPARE(
            errorCode([&]() { createWorktree(QStringLiteral("topic"), {{QStringLiteral("newBranch"), false}}); }),
            QStringLiteral("BRANCH_IN_USE")
        );

        const QVariantMap overview = WorktreeRepository::overview(m_database, m_libraryId);
        QCOMPARE(overview.value(QStringLiteral("worktrees")).toList().size(), 3);
    }

    void detachedHeadWorktreesAreLabelledByCommit()
    {
        const QString detached = m_base + QStringLiteral("/detached");
        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-q"), QStringLiteral("--detach"), detached});
        const QVariantMap entry = entryFor(WorktreeRepository::overview(m_database, m_libraryId), detached);
        QVERIFY(entry.value(QStringLiteral("detached")).toBool());
        QVERIFY(entry.value(QStringLiteral("branch")).isNull());
        QCOMPARE(entry.value(QStringLiteral("label")).toString().size(), 8);

        const GitWorktrees::Status status = GitWorktrees::status(detached);
        QVERIFY(status.available);
        QVERIFY(status.detached);
        QVERIFY(!status.head.isEmpty());
    }

    void failedCreationLeavesNothingBehind()
    {
        const int before = libraryCount();
        QCOMPARE(errorCode([&]() { createWorktree(QStringLiteral("bad..name")); }), QStringLiteral("INVALID_BRANCH"));
        QCOMPARE(errorCode([&]() { createWorktree(QStringLiteral("main")); }), QStringLiteral("BRANCH_EXISTS"));
        QCOMPARE(errorCode([&]() { createWorktree(QStringLiteral("nope"), {{QStringLiteral("newBranch"), false}}); }), QStringLiteral("BRANCH_NOT_FOUND"));
        QCOMPARE(errorCode([&]() { createWorktree(QStringLiteral("fresh"), {{QStringLiteral("baseRef"), QStringLiteral("does-not-exist")}}); }), QStringLiteral("BASE_NOT_FOUND"));

        const QString occupied = m_base + QStringLiteral("/occupied");
        writeFile(occupied + QStringLiteral("/keep.txt"), "keep\n");
        QCOMPARE(errorCode([&]() { createWorktree(QStringLiteral("fresh"), {{QStringLiteral("path"), occupied}}); }), QStringLiteral("PATH_EXISTS"));
        QCOMPARE(readFile(occupied + QStringLiteral("/keep.txt")), QByteArray("keep\n"));

        QCOMPARE(
            errorCode([&]() { createWorktree(QStringLiteral("fresh"), {{QStringLiteral("path"), m_repo + QStringLiteral("/nested")}}); }),
            QStringLiteral("PATH_INSIDE_WORKTREE")
        );

        QCOMPARE(libraryCount(), before);
        QVERIFY(!GitWorktrees::branchExists(m_repo, QStringLiteral("fresh")));
        QCOMPARE(GitWorktrees::list(m_repo).size(), 1);
    }

    void sameRelativeFileStaysSeparatePerWorktree()
    {
        const QVariantMap created = createWorktree(QStringLiteral("parallel"));
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        const QString worktreePath = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();

        LibraryFiles::scan(m_database, m_libraryId);
        LibraryFiles::scan(m_database, worktreeLibraryId);

        const auto mainFile = LibraryFileRepository::fileByRelativePath(m_database, m_libraryId, QStringLiteral("shared.txt"));
        const auto worktreeFile = LibraryFileRepository::fileByRelativePath(m_database, worktreeLibraryId, QStringLiteral("shared.txt"));
        QVERIFY(mainFile && worktreeFile);
        QVERIFY(mainFile->value(QStringLiteral("id")) != worktreeFile->value(QStringLiteral("id")));

        LibraryFileOperations::writeText(
            m_database,
            worktreeLibraryId,
            worktreeFile->value(QStringLiteral("id")).toString(),
            QStringLiteral("worktree version\n"),
            worktreeFile->value(QStringLiteral("modifiedAt")).toString()
        );
        LibraryFileOperations::writeText(
            m_database,
            m_libraryId,
            mainFile->value(QStringLiteral("id")).toString(),
            QStringLiteral("main edit\n"),
            mainFile->value(QStringLiteral("modifiedAt")).toString()
        );

        QCOMPARE(readFile(worktreePath + QStringLiteral("/shared.txt")), QByteArray("worktree version\n"));
        QCOMPARE(readFile(m_repo + QStringLiteral("/shared.txt")), QByteArray("main edit\n"));

        // Git status is per worktree as well.
        QCOMPARE(LibraryFiles::gitStatus(m_database, worktreeLibraryId).value(QStringLiteral("branch")).toString(), QStringLiteral("parallel"));
        QCOMPARE(LibraryFiles::gitStatus(m_database, m_libraryId).value(QStringLiteral("branch")).toString(), QStringLiteral("main"));
    }

    void statusReportsDirtyConflictsAndMissingFolders()
    {
        const QVariantMap created = createWorktree(QStringLiteral("status-check"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();

        GitWorktrees::Status clean = GitWorktrees::status(path);
        QVERIFY(clean.available);
        QVERIFY(!clean.dirty());
        QCOMPARE(clean.branch, QStringLiteral("status-check"));

        writeFile(path + QStringLiteral("/shared.txt"), "changed\n");
        writeFile(path + QStringLiteral("/new.txt"), "new\n");
        const GitWorktrees::Status dirty = GitWorktrees::status(path);
        QCOMPARE(dirty.modified, 1);
        QCOMPARE(dirty.untracked, 1);
        QVERIFY(dirty.dirty());

        // A real merge conflict.
        git(path, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-am"), QStringLiteral("branch side")});
        writeFile(m_repo + QStringLiteral("/shared.txt"), "main side\n");
        git(m_repo, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-am"), QStringLiteral("main side")});
        GitWorktrees::run(path, {QStringLiteral("-c"), QStringLiteral("user.name=T"), QStringLiteral("-c"), QStringLiteral("user.email=t@t"), QStringLiteral("merge"), QStringLiteral("main")});
        const GitWorktrees::Status conflicted = GitWorktrees::status(path);
        QCOMPARE(conflicted.conflicted, 1);
        QCOMPARE(conflicted.conflictedPaths, QStringList{QStringLiteral("shared.txt")});
        QCOMPARE(conflicted.operation, QStringLiteral("merge"));

        const GitWorktrees::Status missing = GitWorktrees::status(m_base + QStringLiteral("/gone"));
        QVERIFY(missing.missing);
        QVERIFY(!missing.available);

        const QVariantMap statuses = WorktreeRepository::statuses({path, m_repo});
        QCOMPARE(statuses.value(path).toMap().value(QStringLiteral("conflicted")).toInt(), 1);
        QVERIFY(!statuses.value(m_repo).toMap().value(QStringLiteral("dirty")).toBool());
    }

    void removalRefusesAnythingThatWouldLoseWork()
    {
        const QVariantMap created = createWorktree(QStringLiteral("careful"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();

        QVERIFY(WorktreeRepository::removalPreview(m_database, m_libraryId, path, 0).value(QStringLiteral("allowed")).toBool());

        QVariantMap preview = WorktreeRepository::removalPreview(m_database, m_libraryId, path, 2);
        QVERIFY(!preview.value(QStringLiteral("allowed")).toBool());
        QCOMPARE(blockerCodes(preview), QStringList{QStringLiteral("UNSAVED_EDITORS")});

        writeFile(path + QStringLiteral("/shared.txt"), "precious edit\n");
        writeFile(path + QStringLiteral("/untracked.txt"), "precious note\n");
        preview = WorktreeRepository::removalPreview(m_database, m_libraryId, path, 0);
        QVERIFY(blockerCodes(preview).contains(QStringLiteral("UNCOMMITTED_CHANGES")));
        QVERIFY(blockerCodes(preview).contains(QStringLiteral("UNTRACKED_FILES")));
        QCOMPARE(errorCode([&]() { WorktreeRepository::removeWorktree(m_database, m_libraryId, path, 0); }), QStringLiteral("REMOVAL_BLOCKED"));
        QCOMPARE(readFile(path + QStringLiteral("/shared.txt")), QByteArray("precious edit\n"));
        QCOMPARE(readFile(path + QStringLiteral("/untracked.txt")), QByteArray("precious note\n"));

        git(path, {QStringLiteral("checkout"), QStringLiteral("--"), QStringLiteral("shared.txt")});
        QFile::remove(path + QStringLiteral("/untracked.txt"));
        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("lock"), QStringLiteral("--reason"), QStringLiteral("agent busy"), path});
        preview = WorktreeRepository::removalPreview(m_database, m_libraryId, path, 0);
        QCOMPARE(blockerCodes(preview), QStringList{QStringLiteral("LOCKED")});
        QCOMPARE(errorCode([&]() { WorktreeRepository::removeWorktree(m_database, m_libraryId, path, 0); }), QStringLiteral("REMOVAL_BLOCKED"));
        QVERIFY(QFileInfo(path).isDir());
        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("unlock"), path});

        // A running AI run in the worktree's Library blocks removal.
        const QString chatId = ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), worktreeLibraryId}}).value(QStringLiteral("id")).toString();
        const QVariantMap userMessage = ChatRepository::insertMessage(m_database, chatId, QStringLiteral("user"), QStringLiteral("hi"), QStringLiteral("complete"));
        const QVariantMap assistantMessage = ChatRepository::insertMessage(m_database, chatId, QStringLiteral("assistant"), QString(), QStringLiteral("streaming"));
        const QVariantMap run = AIRunRepository::createRun(m_database, {
            {QStringLiteral("chatId"), chatId},
            {QStringLiteral("libraryId"), worktreeLibraryId},
            {QStringLiteral("agentId"), AgentDefaults::defaultAgentId},
            {QStringLiteral("userMessageId"), userMessage.value(QStringLiteral("id"))},
            {QStringLiteral("assistantMessageId"), assistantMessage.value(QStringLiteral("id"))},
            {QStringLiteral("contextCompiler"), QVariantMap{{QStringLiteral("id"), QStringLiteral("recent-history")}, {QStringLiteral("version"), 1}}},
            {QStringLiteral("provider"), QStringLiteral("openai")},
            {QStringLiteral("model"), QStringLiteral("gpt-5-mini")},
            {QStringLiteral("workspace"), WorktreeRepository::executionContext(path)},
        });
        QCOMPARE(run.value(QStringLiteral("workspace")).toMap().value(QStringLiteral("branch")).toString(), QStringLiteral("careful"));
        QCOMPARE(run.value(QStringLiteral("workspace")).toMap().value(QStringLiteral("root")).toString(), path);

        const QVariantMap overview = WorktreeRepository::overview(m_database, m_libraryId);
        QCOMPARE(entryFor(overview, path).value(QStringLiteral("runningRuns")).toList().size(), 1);
        QCOMPARE(blockerCodes(WorktreeRepository::removalPreview(m_database, m_libraryId, path, 0)), QStringList{QStringLiteral("AGENT_RUNNING")});
        AIRunRepository::completeRun(m_database, run.value(QStringLiteral("id")).toString(), QStringLiteral("done"), QVariant());

        // The anchor's own checkout can never be removed from here.
        const QStringList anchorCodes = blockerCodes(WorktreeRepository::removalPreview(m_database, m_libraryId, m_repo, 0));
        QVERIFY(anchorCodes.contains(QStringLiteral("ANCHOR_CHECKOUT")));
        QVERIFY(anchorCodes.contains(QStringLiteral("MAIN_WORKTREE")));
    }

    void removingACleanWorktreeKeepsItsBranchAndHistory()
    {
        const QVariantMap created = createWorktree(QStringLiteral("done-soon"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        const QString chatId = ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), worktreeLibraryId}}).value(QStringLiteral("id")).toString();

        const QVariantMap removed = WorktreeRepository::removeWorktree(m_database, m_libraryId, path, 0);
        QVERIFY(removed.value(QStringLiteral("removed")).toBool());
        QCOMPARE(removed.value(QStringLiteral("archivedLibraryId")).toString(), worktreeLibraryId);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(GitWorktrees::branchExists(m_repo, QStringLiteral("done-soon")));
        QVERIFY(!WorktreeRepository::linkFor(m_database, worktreeLibraryId).has_value());

        const auto library = LibraryRepository::libraryById(m_database, worktreeLibraryId);
        QVERIFY(library.has_value());
        QVERIFY(!library->value(QStringLiteral("archivedAt")).isNull());
        QVERIFY(ChatRepository::chatById(m_database, chatId).has_value());
        QVERIFY(journalKinds(worktreeLibraryId).contains(QStringLiteral("worktree.removed")));
        QCOMPARE(WorktreeRepository::overview(m_database, m_libraryId).value(QStringLiteral("worktrees")).toList().size(), 1);
    }

    void missingFoldersArePrunedNotForced()
    {
        const QVariantMap created = createWorktree(QStringLiteral("vanished"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(QDir(path).removeRecursively());

        const QVariantMap entry = entryFor(WorktreeRepository::overview(m_database, m_libraryId), path);
        QVERIFY(!entry.value(QStringLiteral("exists")).toBool());
        QVERIFY(entry.value(QStringLiteral("prunable")).toBool());
        QCOMPARE(entry.value(QStringLiteral("libraryId")).toString(), worktreeLibraryId);

        WorktreeRepository::removeWorktree(m_database, m_libraryId, path, 0);
        QCOMPARE(GitWorktrees::list(m_repo).size(), 1);
        QVERIFY(GitWorktrees::branchExists(m_repo, QStringLiteral("vanished")));
    }

    void orphanedLinksCanBeForgotten()
    {
        const QVariantMap created = createWorktree(QStringLiteral("orphan"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();

        QCOMPARE(errorCode([&]() { WorktreeRepository::forgetWorktree(m_database, worktreeLibraryId); }), QStringLiteral("WORKTREE_STILL_PRESENT"));

        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("remove"), path});
        const QVariantMap entry = entryFor(WorktreeRepository::overview(m_database, m_libraryId), path);
        QVERIFY(entry.value(QStringLiteral("orphaned")).toBool());

        WorktreeRepository::forgetWorktree(m_database, worktreeLibraryId);
        QVERIFY(!LibraryRepository::libraryById(m_database, worktreeLibraryId)->value(QStringLiteral("archivedAt")).isNull());
        QVERIFY(entryFor(WorktreeRepository::overview(m_database, m_libraryId), path).isEmpty());
    }

    void collectionsIncludeWorktreesThroughTheirAnchor()
    {
        const QVariantMap created = createWorktree(QStringLiteral("scoped"));
        const QString worktreeLibraryId = created.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        const QString chatId = ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), worktreeLibraryId}}).value(QStringLiteral("id")).toString();

        const QString collectionId = CollectionRepository::createCollection(m_database, {
            {QStringLiteral("name"), QStringLiteral("Work")},
            {QStringLiteral("libraryIds"), QVariantList{m_libraryId}},
        }).value(QStringLiteral("id")).toString();
        const QVariantMap scope = CollectionRepository::scope(m_database, collectionId);

        QCOMPARE(scope.value(QStringLiteral("libraryIds")).toList(), QVariantList{m_libraryId});
        QCOMPARE(scope.value(QStringLiteral("worktreeLibraryIds")).toList(), QVariantList{worktreeLibraryId});
        QVERIFY(scope.value(QStringLiteral("chatIds")).toList().contains(chatId));
    }

    void scansSkipNestedLinkedWorktrees()
    {
        git(m_repo, {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("nested"), m_repo + QStringLiteral("/.trees/nested")});
        QVERIFY(GitWorktrees::isLinkedWorktreeRoot(m_repo + QStringLiteral("/.trees/nested")));
        QVERIFY(!GitWorktrees::isLinkedWorktreeRoot(m_repo));

        LibraryFiles::scan(m_database, m_libraryId);
        QVERIFY(LibraryFileRepository::fileByRelativePath(m_database, m_libraryId, QStringLiteral("shared.txt")).has_value());
        QVERIFY(!LibraryFileRepository::fileByRelativePath(m_database, m_libraryId, QStringLiteral(".trees/nested/shared.txt")).has_value());
        QVERIFY(!LibraryFiles::directories(m_database, m_libraryId).contains(QStringLiteral(".trees/nested")));
    }

    void librarySubfoldersMapIntoEachWorktree()
    {
        const QString docsLibraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_repo + QStringLiteral("/docs")}})
                                          .value(QStringLiteral("library"))
                                          .toMap()
                                          .value(QStringLiteral("id"))
                                          .toString();
        const QVariantMap created = WorktreeRepository::createWorktree(m_database, docsLibraryId, {{QStringLiteral("branch"), QStringLiteral("docs-work")}});
        const QString worktreePath = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(
            created.value(QStringLiteral("library")).toMap().value(QStringLiteral("rootPath")).toString(),
            GitWorktrees::canonicalPath(worktreePath + QStringLiteral("/docs"))
        );
        QCOMPARE(entryFor(WorktreeRepository::overview(m_database, docsLibraryId), m_repo).value(QStringLiteral("libraryId")).toString(), docsLibraryId);
    }

    void journalPagesEventsInOrder()
    {
        WorkspaceChangeJournal::Event saved;
        saved.kind = QStringLiteral("file.modified");
        saved.actorType = QStringLiteral("user");
        saved.path = QStringLiteral("shared.txt");
        const qint64 first = WorkspaceChangeJournal::append(m_database, m_libraryId, saved);

        WorkspaceChangeJournal::Event checkpoint;
        checkpoint.kind = QStringLiteral("checkpoint");
        checkpoint.actorType = QStringLiteral("system");
        checkpoint.details = {{QStringLiteral("head"), QStringLiteral("abc")}};
        const qint64 second = WorkspaceChangeJournal::append(m_database, m_libraryId, checkpoint);
        QVERIFY(second > first);
        QCOMPARE(WorkspaceChangeJournal::latestSequence(m_database, m_libraryId), second);

        const QVariantList after = WorkspaceChangeJournal::eventsAfter(m_database, m_libraryId, first);
        QCOMPARE(after.size(), 1);
        const QVariantMap event = after.first().toMap();
        QCOMPARE(event.value(QStringLiteral("kind")).toString(), QStringLiteral("checkpoint"));
        QCOMPARE(event.value(QStringLiteral("evidence")).toString(), QStringLiteral("performed"));
        QCOMPARE(event.value(QStringLiteral("actor")).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("system"));
        QCOMPARE(event.value(QStringLiteral("details")).toMap().value(QStringLiteral("head")).toString(), QStringLiteral("abc"));

        WorkspaceChangeJournal::Event bad;
        bad.kind = QStringLiteral("movie.frame");
        bool threw = false;

        try {
            WorkspaceChangeJournal::append(m_database, m_libraryId, bad);
        } catch (const CoreError &) {
            threw = true;
        }

        QVERIFY(threw);
    }

    void snapshotsTurnIntoObservedEvents()
    {
        const WorkspaceChangeJournal::Snapshot before = WorkspaceChangeJournal::capture(m_repo);
        QVERIFY(before.available);
        QVERIFY(before.entries.isEmpty());

        writeFile(m_repo + QStringLiteral("/shared.txt"), "edited in a terminal\n");
        writeFile(m_repo + QStringLiteral("/fresh.txt"), "new file\n");
        QFile::remove(m_repo + QStringLiteral("/docs/readme.md"));
        const WorkspaceChangeJournal::Snapshot changed = WorkspaceChangeJournal::capture(m_repo);

        QHash<QString, QString> kinds;

        for (const WorkspaceChangeJournal::Event &event : WorkspaceChangeJournal::diff(before, changed)) {
            QCOMPARE(event.evidence, QStringLiteral("observed"));
            QCOMPARE(event.actorType, QStringLiteral("unknown"));
            kinds.insert(event.path, event.kind);
        }

        QCOMPARE(kinds.value(QStringLiteral("shared.txt")), QStringLiteral("file.modified"));
        QCOMPARE(kinds.value(QStringLiteral("fresh.txt")), QStringLiteral("file.created"));
        QCOMPARE(kinds.value(QStringLiteral("docs/readme.md")), QStringLiteral("file.deleted"));
        QVERIFY(before.fingerprint() != changed.fingerprint());

        // Staging changes the index, not the files.
        git(m_repo, {QStringLiteral("add"), QStringLiteral("fresh.txt")});
        QVERIFY(WorkspaceChangeJournal::diff(changed, WorkspaceChangeJournal::capture(m_repo)).isEmpty());

        // A commit is reported as HEAD moving, not as files changing back.
        const WorkspaceChangeJournal::Snapshot staged = WorkspaceChangeJournal::capture(m_repo);
        git(m_repo, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-am"), QStringLiteral("work")});
        const QList<WorkspaceChangeJournal::Event> committed = WorkspaceChangeJournal::diff(staged, WorkspaceChangeJournal::capture(m_repo));
        QCOMPARE(committed.size(), 1);
        QCOMPARE(committed.first().kind, QStringLiteral("git.head"));

        // A Library on a subfolder only sees its own paths, relative to itself.
        writeFile(m_repo + QStringLiteral("/docs/guide.md"), "guide\n");
        writeFile(m_repo + QStringLiteral("/outside.txt"), "outside\n");
        const WorkspaceChangeJournal::Snapshot docs = WorkspaceChangeJournal::capture(m_repo + QStringLiteral("/docs"));
        QCOMPARE(docs.entries.keys(), QStringList{QStringLiteral("guide.md")});
    }

    void executionContextNamesTheWorktree()
    {
        const QVariantMap created = createWorktree(QStringLiteral("agent-desk"));
        const QString path = created.value(QStringLiteral("worktree")).toMap().value(QStringLiteral("path")).toString();
        const QVariantMap context = WorktreeRepository::executionContext(path);
        QCOMPARE(context.value(QStringLiteral("branch")).toString(), QStringLiteral("agent-desk"));
        QCOMPARE(context.value(QStringLiteral("head")).toString().size(), 40);
        QCOMPARE(context.value(QStringLiteral("repositoryKey")).toString(), GitWorktrees::inspect(m_repo).key);
        QCOMPARE(context.value(QStringLiteral("workspaceRoot")).toString(), path);

        const QString plain = m_base + QStringLiteral("/plain");
        QDir().mkpath(plain);
        QVERIFY(WorktreeRepository::executionContext(plain).value(QStringLiteral("repositoryKey")).toString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(CoreWorktreesTest)
#include "core_worktrees_test.moc"
