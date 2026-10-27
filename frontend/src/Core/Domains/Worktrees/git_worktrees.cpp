#include "git_worktrees.h"

#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <algorithm>
#include <optional>

namespace
{
QVariant nullableString(const QString &value)
{
    return value.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(value);
}

QString firstLine(const QString &text)
{
    const QString trimmed = text.trimmed();
    const qsizetype newline = trimmed.indexOf(QLatin1Char('\n'));
    return newline < 0 ? trimmed : trimmed.left(newline).trimmed();
}

QString gitFailureMessage(const GitWorktrees::CommandResult &result)
{
    if (!result.started) {
        return QStringLiteral("Git is not available: %1").arg(result.errors.trimmed());
    }

    if (result.timedOut) {
        return QStringLiteral("Git did not respond in time.");
    }

    const QString message = result.errors.trimmed();
    return message.isEmpty() ? QStringLiteral("Git exited with code %1.").arg(result.exitCode) : message;
}

[[noreturn]] void fail(const QString &code, const QString &message, QVariantMap details = {})
{
    details.insert(QStringLiteral("code"), code);
    throw CoreError(message, details);
}

QString resolveAgainst(const QString &base, const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(base).filePath(path));
}

// The git directory of a worktree top level, without running git: `.git` is
// either the directory itself or a file holding "gitdir: <path>".
QString gitDirectoryOf(const QString &topLevel)
{
    const QString dotGit = QDir(topLevel).filePath(QStringLiteral(".git"));
    const QFileInfo info(dotGit);

    if (info.isDir()) {
        return dotGit;
    }

    if (!info.isFile()) {
        return {};
    }

    QFile file(dotGit);

    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QString line = firstLine(QString::fromUtf8(file.read(4096)));

    if (!line.startsWith(QStringLiteral("gitdir:"))) {
        return {};
    }

    return resolveAgainst(topLevel, line.mid(7).trimmed());
}

QString operationInProgress(const QString &gitDirectory)
{
    if (gitDirectory.isEmpty()) {
        return {};
    }

    const QDir directory(gitDirectory);

    if (directory.exists(QStringLiteral("rebase-merge")) || directory.exists(QStringLiteral("rebase-apply"))) {
        return QStringLiteral("rebase");
    }

    if (directory.exists(QStringLiteral("MERGE_HEAD"))) {
        return QStringLiteral("merge");
    }

    if (directory.exists(QStringLiteral("CHERRY_PICK_HEAD"))) {
        return QStringLiteral("cherry-pick");
    }

    if (directory.exists(QStringLiteral("REVERT_HEAD"))) {
        return QStringLiteral("revert");
    }

    if (directory.exists(QStringLiteral("BISECT_LOG"))) {
        return QStringLiteral("bisect");
    }

    return {};
}

QString shortBranch(const QString &reference)
{
    static const QString prefix = QStringLiteral("refs/heads/");
    return reference.startsWith(prefix) ? reference.mid(prefix.size()) : reference;
}

QVariantMap blocker(const QString &code, const QString &message)
{
    return {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
}
}

namespace GitWorktrees
{
CommandResult run(const QString &directory, const QStringList &arguments, int timeoutMilliseconds)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    // Archivist decides which repository a command targets through -C alone.
    for (const QString &name : {
             QStringLiteral("GIT_DIR"),
             QStringLiteral("GIT_WORK_TREE"),
             QStringLiteral("GIT_INDEX_FILE"),
             QStringLiteral("GIT_COMMON_DIR"),
             QStringLiteral("GIT_PREFIX"),
         }) {
        environment.remove(name);
    }

    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProgram(QStringLiteral("git"));

    QStringList fullArguments{QStringLiteral("-C"), directory};
    fullArguments.append(arguments);
    process.setArguments(fullArguments);
    process.start(QIODevice::ReadOnly);

    CommandResult result;
    result.started = process.waitForStarted(5000);

    if (!result.started) {
        result.errors = process.errorString();
        return result;
    }

    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(1000);
        result.timedOut = true;
        result.errors = QStringLiteral("git timed out");
        return result;
    }

    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.errors = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QString canonicalPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

bool samePath(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }

    return canonicalPath(left) == canonicalPath(right);
}

bool isInside(const QString &path, const QString &root)
{
    const QString child = canonicalPath(path);
    const QString parent = canonicalPath(root);

    if (child.isEmpty() || parent.isEmpty()) {
        return false;
    }

    if (child == parent) {
        return true;
    }

    const QString prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return child.startsWith(prefix);
}

Repository inspect(const QString &path)
{
    Repository repository;

    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return repository;
    }

    const CommandResult inside = run(path, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}, 10000);

    if (!inside.ok() || inside.output.trimmed() != QStringLiteral("true")) {
        const CommandResult bare = run(path, {QStringLiteral("rev-parse"), QStringLiteral("--is-bare-repository")}, 10000);
        repository.bare = bare.ok() && bare.output.trimmed() == QStringLiteral("true");
        return repository;
    }

    CommandResult details = run(path, {
        QStringLiteral("rev-parse"),
        QStringLiteral("--path-format=absolute"),
        QStringLiteral("--git-common-dir"),
        QStringLiteral("--git-dir"),
        QStringLiteral("--show-toplevel"),
        QStringLiteral("--show-prefix"),
    }, 10000);

    // Git before 2.31 has no --path-format; relative answers are relative to `path`.
    if (!details.ok()) {
        details = run(path, {
            QStringLiteral("rev-parse"),
            QStringLiteral("--git-common-dir"),
            QStringLiteral("--git-dir"),
            QStringLiteral("--show-toplevel"),
            QStringLiteral("--show-prefix"),
        }, 10000);
    }

    if (!details.ok()) {
        return repository;
    }

    QStringList lines = details.output.split(QLatin1Char('\n'));

    while (lines.size() < 4) {
        lines.append(QString());
    }

    repository.repository = true;
    repository.commonDirectory = canonicalPath(resolveAgainst(path, lines.at(0).trimmed()));
    repository.gitDirectory = canonicalPath(resolveAgainst(path, lines.at(1).trimmed()));
    repository.topLevel = canonicalPath(lines.at(2).trimmed());
    QString prefix = lines.at(3).trimmed();

    while (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }

    repository.prefix = prefix;
    repository.key = repository.commonDirectory;
    repository.linkedWorktree = !repository.gitDirectory.isEmpty()
        && repository.gitDirectory != repository.commonDirectory;
    return repository;
}

QVariantMap Worktree::toMap() const
{
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("head"), nullableString(head)},
        {QStringLiteral("branch"), nullableString(branch)},
        {QStringLiteral("detached"), detached},
        {QStringLiteral("bare"), bare},
        {QStringLiteral("main"), main},
        {QStringLiteral("locked"), locked},
        {QStringLiteral("lockReason"), nullableString(lockReason)},
        {QStringLiteral("prunable"), prunable},
        {QStringLiteral("prunableReason"), nullableString(prunableReason)},
        {QStringLiteral("exists"), exists},
    };
}

QList<Worktree> parseList(const QString &output, bool nulSeparated)
{
    QList<Worktree> worktrees;
    QStringList lines = output.split(nulSeparated ? QLatin1Char('\0') : QLatin1Char('\n'));
    std::optional<Worktree> current;

    auto finish = [&]() {
        if (current && !current->path.isEmpty()) {
            current->main = worktrees.isEmpty();
            worktrees.append(*current);
        }

        current.reset();
    };

    for (const QString &rawLine : lines) {
        const QString line = nulSeparated ? rawLine : QString(rawLine).remove(QLatin1Char('\r'));

        if (line.isEmpty()) {
            finish();
            continue;
        }

        if (line.startsWith(QStringLiteral("worktree "))) {
            finish();
            current = Worktree{};
            current->path = line.mid(9);
            continue;
        }

        if (!current) {
            continue;
        }

        if (line.startsWith(QStringLiteral("HEAD "))) {
            current->head = line.mid(5).trimmed();
        } else if (line.startsWith(QStringLiteral("branch "))) {
            current->branch = shortBranch(line.mid(7).trimmed());
        } else if (line == QStringLiteral("detached")) {
            current->detached = true;
        } else if (line == QStringLiteral("bare")) {
            current->bare = true;
        } else if (line == QStringLiteral("locked") || line.startsWith(QStringLiteral("locked "))) {
            current->locked = true;
            current->lockReason = line.mid(6).trimmed();
        } else if (line == QStringLiteral("prunable") || line.startsWith(QStringLiteral("prunable "))) {
            current->prunable = true;
            current->prunableReason = line.mid(8).trimmed();
        }
    }

    finish();
    return worktrees;
}

QList<Worktree> list(const QString &repositoryPath)
{
    bool nulSeparated = true;
    CommandResult result = run(repositoryPath, {
        QStringLiteral("worktree"),
        QStringLiteral("list"),
        QStringLiteral("--porcelain"),
        QStringLiteral("-z"),
    });

    // -z arrived in git 2.36.
    if (result.started && !result.timedOut && result.exitCode != 0) {
        nulSeparated = false;
        result = run(repositoryPath, {
            QStringLiteral("worktree"),
            QStringLiteral("list"),
            QStringLiteral("--porcelain"),
        });
    }

    if (!result.ok()) {
        fail(
            QStringLiteral("GIT_FAILED"),
            QStringLiteral("Archivist could not list this repository's worktrees."),
            {{QStringLiteral("cause"), gitFailureMessage(result)}}
        );
    }

    QList<Worktree> worktrees = parseList(result.output, nulSeparated);

    for (Worktree &worktree : worktrees) {
        worktree.exists = QFileInfo(worktree.path).isDir();

        if (worktree.exists) {
            worktree.path = canonicalPath(worktree.path);
        }
    }

    return worktrees;
}

QVariantMap Status::toMap() const
{
    return {
        {QStringLiteral("available"), available},
        {QStringLiteral("missing"), missing},
        {QStringLiteral("error"), nullableString(error)},
        {QStringLiteral("branch"), nullableString(branch)},
        {QStringLiteral("head"), nullableString(head)},
        {QStringLiteral("detached"), detached},
        {QStringLiteral("unborn"), unborn},
        {QStringLiteral("upstream"), nullableString(upstream)},
        {QStringLiteral("ahead"), ahead},
        {QStringLiteral("behind"), behind},
        {QStringLiteral("staged"), staged},
        {QStringLiteral("modified"), modified},
        {QStringLiteral("deleted"), deleted},
        {QStringLiteral("renamed"), renamed},
        {QStringLiteral("untracked"), untracked},
        {QStringLiteral("conflicted"), conflicted},
        {QStringLiteral("conflictedPaths"), conflictedPaths},
        {QStringLiteral("changedCount"), int(changes.size())},
        {QStringLiteral("dirty"), dirty()},
        {QStringLiteral("operation"), nullableString(operation)},
    };
}

Status status(const QString &worktreePath)
{
    return status(worktreePath, {}, false);
}

Status status(const QString &worktreePath, const QStringList &pathspec, bool allUntracked)
{
    Status status;

    if (!QFileInfo(worktreePath).isDir()) {
        status.missing = true;
        status.error = QStringLiteral("The worktree folder is missing.");
        return status;
    }

    QStringList arguments{
        QStringLiteral("status"),
        QStringLiteral("--porcelain=v2"),
        QStringLiteral("--branch"),
        QStringLiteral("-z"),
        allUntracked ? QStringLiteral("--untracked-files=all") : QStringLiteral("--untracked-files=normal"),
        QStringLiteral("--ignore-submodules=dirty"),
    };

    if (!pathspec.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(pathspec);
    }

    const CommandResult result = run(worktreePath, arguments);

    if (!result.ok()) {
        status.error = gitFailureMessage(result);
        return status;
    }

    status.available = true;
    static const QRegularExpression aheadBehind(QStringLiteral("^\\+(\\d+)\\s+-(\\d+)$"));
    const QStringList tokens = result.output.split(QLatin1Char('\0'), Qt::SkipEmptyParts);

    // The path is the last space-separated field after a fixed number of fields.
    auto pathAfterFields = [](const QString &token, int fields) {
        qsizetype position = 0;

        for (int field = 0; field < fields; ++field) {
            position = token.indexOf(QLatin1Char(' '), position);

            if (position < 0) {
                return QString();
            }

            ++position;
        }

        return token.mid(position);
    };

    for (qsizetype index = 0; index < tokens.size(); ++index) {
        const QString &token = tokens.at(index);

        if (token.startsWith(QStringLiteral("# branch.oid "))) {
            const QString value = token.mid(13).trimmed();
            status.unborn = value == QStringLiteral("(initial)");
            status.head = status.unborn ? QString() : value;
            continue;
        }

        if (token.startsWith(QStringLiteral("# branch.head "))) {
            const QString value = token.mid(14).trimmed();
            status.detached = value == QStringLiteral("(detached)");
            status.branch = status.detached ? QString() : value;
            continue;
        }

        if (token.startsWith(QStringLiteral("# branch.upstream "))) {
            status.upstream = token.mid(18).trimmed();
            continue;
        }

        if (token.startsWith(QStringLiteral("# branch.ab "))) {
            const auto match = aheadBehind.match(token.mid(12).trimmed());

            if (match.hasMatch()) {
                status.ahead = match.captured(1).toInt();
                status.behind = match.captured(2).toInt();
            }

            continue;
        }

        if (token.startsWith(QLatin1Char('#')) || token.size() < 2) {
            continue;
        }

        const QChar type = token.at(0);

        if (type == QLatin1Char('?')) {
            ++status.untracked;
            status.changes.append({token.mid(2), QLatin1Char('?')});
            continue;
        }

        if (type == QLatin1Char('!')) {
            continue;
        }

        const QString xy = token.mid(2, 2);
        const QChar indexState = xy.size() > 0 ? xy.at(0) : QLatin1Char('.');
        const QChar worktreeState = xy.size() > 1 ? xy.at(1) : QLatin1Char('.');

        if (type == QLatin1Char('u')) {
            const QString path = pathAfterFields(token, 10);
            ++status.conflicted;
            status.changes.append({path, QLatin1Char('U')});

            if (status.conflictedPaths.size() < 50) {
                status.conflictedPaths.append(path);
            }

            continue;
        }

        if (type == QLatin1Char('2')) {
            status.changes.append({pathAfterFields(token, 9), QLatin1Char('R')});
            ++status.renamed;
            ++index; // the original path follows as its own token
            continue;
        }

        if (type != QLatin1Char('1')) {
            continue;
        }

        const QString path = pathAfterFields(token, 8);

        if (indexState == QLatin1Char('D') || worktreeState == QLatin1Char('D')) {
            ++status.deleted;
            status.changes.append({path, QLatin1Char('D')});
        } else if (indexState == QLatin1Char('A')) {
            ++status.staged;
            status.changes.append({path, QLatin1Char('A')});
        } else if (worktreeState != QLatin1Char('.')) {
            ++status.modified;
            status.changes.append({path, QLatin1Char('M')});
        } else if (indexState != QLatin1Char('.')) {
            ++status.staged;
            status.changes.append({path, QLatin1Char('S')});
        }
    }

    status.operation = operationInProgress(gitDirectoryOf(worktreePath));
    status.inProgressOperation = !status.operation.isEmpty();
    return status;
}

QStringList localBranches(const QString &repositoryPath)
{
    const CommandResult result = run(repositoryPath, {
        QStringLiteral("for-each-ref"),
        QStringLiteral("--format=%(refname:short)"),
        QStringLiteral("refs/heads"),
    });

    if (!result.ok()) {
        return {};
    }

    QStringList branches = result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (QString &branch : branches) {
        branch = branch.trimmed();
    }

    branches.removeAll(QString());
    std::sort(branches.begin(), branches.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return branches;
}

bool branchExists(const QString &repositoryPath, const QString &branch)
{
    if (branch.trimmed().isEmpty()) {
        return false;
    }

    return run(repositoryPath, {
               QStringLiteral("show-ref"),
               QStringLiteral("--verify"),
               QStringLiteral("--quiet"),
               QStringLiteral("refs/heads/") + branch,
           })
        .ok();
}

bool isValidBranchName(const QString &repositoryPath, const QString &branch)
{
    const QString trimmed = branch.trimmed();

    if (trimmed.isEmpty() || trimmed != branch || trimmed.startsWith(QLatin1Char('-'))) {
        return false;
    }

    return run(repositoryPath, {QStringLiteral("check-ref-format"), QStringLiteral("--branch"), trimmed}, 10000).ok();
}

bool refExists(const QString &repositoryPath, const QString &ref)
{
    if (ref.trimmed().isEmpty() || ref.startsWith(QLatin1Char('-'))) {
        return false;
    }

    return run(repositoryPath, {
               QStringLiteral("rev-parse"),
               QStringLiteral("--verify"),
               QStringLiteral("--quiet"),
               ref + QStringLiteral("^{commit}"),
           })
        .ok();
}

QString slugForBranch(const QString &branch)
{
    QString slug;
    slug.reserve(branch.size());

    for (const QChar character : branch.trimmed()) {
        if (character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_')
            || character == QLatin1Char('.')) {
            slug.append(character);
        } else if (!slug.endsWith(QLatin1Char('-'))) {
            slug.append(QLatin1Char('-'));
        }
    }

    while (slug.startsWith(QLatin1Char('.')) || slug.startsWith(QLatin1Char('-'))) {
        slug.remove(0, 1);
    }

    while (slug.endsWith(QLatin1Char('-')) || slug.endsWith(QLatin1Char('.'))) {
        slug.chop(1);
    }

    return slug.isEmpty() ? QStringLiteral("worktree") : slug.left(80);
}

QString suggestedPath(const QString &mainWorktreePath, const QString &branch)
{
    const QFileInfo main(canonicalPath(mainWorktreePath));
    const QString container = QDir(main.absolutePath()).filePath(main.fileName() + QStringLiteral(".worktrees"));
    const QString slug = slugForBranch(branch);
    QString candidate = QDir(container).filePath(slug);

    for (int suffix = 2; QFileInfo::exists(candidate) && suffix < 1000; ++suffix) {
        candidate = QDir(container).filePath(slug + QStringLiteral("-") + QString::number(suffix));
    }

    return QDir::cleanPath(candidate);
}

Worktree create(const QString &repositoryPath, const CreateOptions &options)
{
    const Repository repository = inspect(repositoryPath);

    if (!repository.repository) {
        fail(QStringLiteral("NOT_A_REPOSITORY"), QStringLiteral("This Library is not inside a Git repository."));
    }

    const QString branch = options.branch.trimmed();

    if (!isValidBranchName(repositoryPath, branch)) {
        fail(QStringLiteral("INVALID_BRANCH"), QStringLiteral("\"%1\" is not a valid branch name.").arg(options.branch));
    }

    const QList<Worktree> existing = list(repositoryPath);
    const QString mainPath = existing.isEmpty() ? repository.topLevel : existing.first().path;
    const bool exists = branchExists(repositoryPath, branch);

    if (options.newBranch && exists) {
        fail(
            QStringLiteral("BRANCH_EXISTS"),
            QStringLiteral("A branch named \"%1\" already exists. Choose it as an existing branch instead.").arg(branch)
        );
    }

    if (!options.newBranch && !exists) {
        fail(QStringLiteral("BRANCH_NOT_FOUND"), QStringLiteral("There is no local branch named \"%1\".").arg(branch));
    }

    if (!options.newBranch) {
        for (const Worktree &worktree : existing) {
            if (worktree.branch == branch) {
                fail(
                    QStringLiteral("BRANCH_IN_USE"),
                    QStringLiteral("\"%1\" is already checked out in %2.").arg(branch, worktree.path),
                    {{QStringLiteral("worktreePath"), worktree.path}}
                );
            }
        }
    }

    QString baseRef = options.baseRef.trimmed();

    if (options.newBranch && !baseRef.isEmpty() && !refExists(repositoryPath, baseRef)) {
        fail(QStringLiteral("BASE_NOT_FOUND"), QStringLiteral("Git can't find \"%1\" to branch from.").arg(baseRef));
    }

    const QString requestedPath = options.path.trimmed();
    const QString target = requestedPath.isEmpty()
        ? suggestedPath(mainPath, branch)
        : QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath());
    const QFileInfo targetInfo(target);

    if (targetInfo.exists()) {
        const bool emptyDirectory = targetInfo.isDir()
            && QDir(target).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty();

        if (!emptyDirectory) {
            fail(
                QStringLiteral("PATH_EXISTS"),
                QStringLiteral("%1 already exists. Pick an empty or new folder.").arg(target),
                {{QStringLiteral("path"), target}}
            );
        }
    }

    // A worktree inside another checkout would be scanned, indexed and
    // searched as part of that checkout.
    for (const Worktree &worktree : existing) {
        if (worktree.exists && isInside(target, worktree.path)) {
            fail(
                QStringLiteral("PATH_INSIDE_WORKTREE"),
                QStringLiteral("Create the worktree outside %1 so the two checkouts stay separate.").arg(worktree.path),
                {{QStringLiteral("path"), target}}
            );
        }
    }

    QDir().mkpath(targetInfo.absolutePath());

    QStringList arguments{QStringLiteral("worktree"), QStringLiteral("add")};

    if (options.newBranch) {
        arguments << QStringLiteral("-b") << branch << QStringLiteral("--") << target;

        if (!baseRef.isEmpty()) {
            arguments << baseRef;
        }
    } else {
        arguments << QStringLiteral("--") << target << branch;
    }

    const CommandResult result = run(repositoryPath, arguments, 120000);

    if (!result.ok()) {
        fail(
            QStringLiteral("GIT_FAILED"),
            QStringLiteral("Git could not create the worktree: %1").arg(firstLine(gitFailureMessage(result))),
            {{QStringLiteral("cause"), gitFailureMessage(result)}}
        );
    }

    const QString created = canonicalPath(target);

    for (const Worktree &worktree : list(repositoryPath)) {
        if (samePath(worktree.path, created)) {
            return worktree;
        }
    }

    fail(QStringLiteral("GIT_FAILED"), QStringLiteral("Git reported success but the new worktree is not listed."));
}

RemovalCheck removalCheck(const QString &repositoryPath, const QString &worktreePath)
{
    RemovalCheck check;
    QList<Worktree> worktrees;

    try {
        worktrees = list(repositoryPath);
    } catch (const CoreError &error) {
        check.blockers.append(blocker(QStringLiteral("GIT_FAILED"), error.message()));
        return check;
    }

    const Worktree *target = nullptr;

    for (const Worktree &worktree : worktrees) {
        if (samePath(worktree.path, worktreePath) || worktree.path == worktreePath) {
            target = &worktree;
            break;
        }
    }

    if (!target) {
        check.blockers.append(blocker(QStringLiteral("NOT_A_WORKTREE"), QStringLiteral("Git doesn't list this folder as a worktree of the repository.")));
        return check;
    }

    if (target->main) {
        check.blockers.append(blocker(QStringLiteral("MAIN_WORKTREE"), QStringLiteral("The repository's main checkout can't be removed.")));
    }

    if (target->locked) {
        check.blockers.append(blocker(
            QStringLiteral("LOCKED"),
            target->lockReason.isEmpty()
                ? QStringLiteral("The worktree is locked. Unlock it with `git worktree unlock` first.")
                : QStringLiteral("The worktree is locked: %1").arg(target->lockReason)
        ));
    }

    if (!target->exists) {
        // Nothing on disk to lose; removal means pruning git's record.
        check.status.missing = true;
        check.allowed = check.blockers.isEmpty();
        return check;
    }

    check.status = status(target->path);

    if (!check.status.available) {
        check.blockers.append(blocker(QStringLiteral("STATUS_UNAVAILABLE"), check.status.error));
    } else {
        if (check.status.conflicted > 0) {
            check.blockers.append(blocker(QStringLiteral("CONFLICTS"), QStringLiteral("The worktree has %1 unresolved conflict(s).").arg(check.status.conflicted)));
        }

        if (check.status.inProgressOperation) {
            check.blockers.append(blocker(QStringLiteral("OPERATION_IN_PROGRESS"), QStringLiteral("A %1 is in progress in this worktree.").arg(check.status.operation)));
        }

        const int uncommitted = check.status.staged + check.status.modified + check.status.deleted + check.status.renamed;

        if (uncommitted > 0) {
            check.blockers.append(blocker(QStringLiteral("UNCOMMITTED_CHANGES"), QStringLiteral("The worktree has %1 uncommitted change(s).").arg(uncommitted)));
        }

        if (check.status.untracked > 0) {
            check.blockers.append(blocker(QStringLiteral("UNTRACKED_FILES"), QStringLiteral("The worktree has %1 untracked file(s) or folder(s).").arg(check.status.untracked)));
        }
    }

    check.allowed = check.blockers.isEmpty();
    return check;
}

void remove(const QString &repositoryPath, const QString &worktreePath)
{
    const RemovalCheck check = removalCheck(repositoryPath, worktreePath);

    if (!check.allowed) {
        QStringList messages;

        for (const QVariant &value : check.blockers) {
            messages.append(value.toMap().value(QStringLiteral("message")).toString());
        }

        fail(
            QStringLiteral("REMOVAL_BLOCKED"),
            messages.isEmpty() ? QStringLiteral("The worktree can't be removed safely.") : messages.join(QLatin1Char(' ')),
            {{QStringLiteral("blockers"), check.blockers}}
        );
    }

    if (check.status.missing) {
        prune(repositoryPath);
        return;
    }

    const CommandResult result = run(repositoryPath, {
        QStringLiteral("worktree"),
        QStringLiteral("remove"),
        QStringLiteral("--"),
        canonicalPath(worktreePath),
    }, 120000);

    if (!result.ok()) {
        fail(
            QStringLiteral("GIT_FAILED"),
            QStringLiteral("Git refused to remove the worktree: %1").arg(firstLine(gitFailureMessage(result))),
            {{QStringLiteral("cause"), gitFailureMessage(result)}}
        );
    }
}

void prune(const QString &repositoryPath)
{
    const CommandResult result = run(repositoryPath, {QStringLiteral("worktree"), QStringLiteral("prune")});

    if (!result.ok()) {
        fail(
            QStringLiteral("GIT_FAILED"),
            QStringLiteral("Git could not prune missing worktrees: %1").arg(firstLine(gitFailureMessage(result)))
        );
    }
}

bool isLinkedWorktreeRoot(const QString &directoryPath)
{
    const QFileInfo dotGit(QDir(directoryPath).filePath(QStringLiteral(".git")));

    if (!dotGit.isFile()) {
        return false;
    }

    const QString gitDirectory = gitDirectoryOf(directoryPath);
    return gitDirectory.contains(QStringLiteral("/worktrees/"));
}
}
