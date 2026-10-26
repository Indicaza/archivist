#include "language_support.h"

#include "Core/Domains/Ide/ide_http_error.h"
#include "Core/Domains/Ide/web_socket_connection.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <memory>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <unistd.h>

using LanguageServers::Definition;
using LanguageServers::Workspace;

namespace
{
constexpr qint64 pendingLifetimeMilliseconds = 60'000;
constexpr int maximumPendingSessions = 32;
constexpr int stderrTailLineLimit = 40;
constexpr int maximumEvents = 500;

bool verboseServerStderr()
{
    return qEnvironmentVariable("ARCHIVIST_LANGUAGE_SERVER_TRACE") == QStringLiteral("1");
}

int maximumActiveSessions()
{
    bool ok = false;
    const double configured = qEnvironmentVariable("ARCHIVIST_MAX_LANGUAGE_SERVERS", QStringLiteral("24")).toDouble(&ok);

    if (!ok || !std::isfinite(configured)) {
        return 24;
    }

    return std::max(4, std::min(64, static_cast<int>(std::floor(configured))));
}

bool isRoutineServerStderr(const QString &serverId, const QString &message)
{
    if (serverId == QStringLiteral("clangd") && message.startsWith(QStringLiteral("I["))) {
        return true;
    }

    if (serverId == QStringLiteral("markdown")) {
        static const QRegularExpression info(QStringLiteral("\\bINF\\b"));

        if (info.match(message).hasMatch()) {
            return true;
        }
    }

    if (serverId == QStringLiteral("qml")) {
        return message.contains(QStringLiteral("Did Setup"))
            || message.contains(QStringLiteral("Using build directories passed by"))
            || message.contains(QStringLiteral("Using import directories passed by"))
            || message.contains(QStringLiteral("Disabling CMake calls"));
    }

    return serverId == QStringLiteral("rust")
        && message.contains(QStringLiteral("No path was found"))
        && message.contains(QStringLiteral("rust-analyzer.toml"));
}

QString isoNow(qint64 offsetMilliseconds = 0)
{
    return JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc().addMSecs(offsetMilliseconds));
}

bool isWithin(const QString &parent, const QString &child)
{
    if (parent == child) {
        return true;
    }

    const QString prefix = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return child.startsWith(prefix);
}

// fs.realpathSync.native + isDirectory, or IdeHttpError.
QString realDirectory(const QString &candidate, const QString &message)
{
    const QString resolved = QFileInfo(QDir::cleanPath(QDir::current().absoluteFilePath(candidate))).canonicalFilePath();

    if (resolved.isEmpty()) {
        throw IdeHttpError(404, message);
    }

    if (!QFileInfo(resolved).isDir()) {
        throw IdeHttpError(409, message);
    }

    return resolved;
}

QString optionalRealDirectory(const QString &candidate)
{
    const QString resolved = QFileInfo(candidate).canonicalFilePath();
    return !resolved.isEmpty() && QFileInfo(resolved).isDir() ? resolved : QString();
}

bool isExecutable(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && ::access(QFile::encodeName(path).constData(), X_OK) == 0;
}

QProcessEnvironment minimalEnvironment()
{
    const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment environment;

    for (const QString &key : {
             QStringLiteral("PATH"), QStringLiteral("HOME"), QStringLiteral("USER"), QStringLiteral("LOGNAME"),
             QStringLiteral("SHELL"), QStringLiteral("TMPDIR"), QStringLiteral("TMP"), QStringLiteral("TEMP"),
             QStringLiteral("LANG"), QStringLiteral("LC_ALL"),
         }) {
        const QString value = system.value(key);

        if (!value.isEmpty()) {
            environment.insert(key, value);
        }
    }

    environment.insert(QStringLiteral("ARCHIVIST_LANGUAGE_SUPPORT"), QStringLiteral("1"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    return environment;
}

QString randomToken()
{
    QByteArray bytes(32, '\0');

    for (char &byte : bytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }

    return QString::fromLatin1(bytes.toHex());
}

bool isUuid(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    ));
    return pattern.match(value).hasMatch();
}

// z.string().trim().min(1).max(8192).optional()
std::optional<QString> optionalPath(const QVariantMap &input, const QString &key, bool &valid)
{
    if (!input.contains(key) || !input.value(key).isValid()) {
        return std::nullopt;
    }

    if (input.value(key).typeId() != QMetaType::QString) {
        valid = false;
        return std::nullopt;
    }

    const QString value = JsCompat::trimmed(input.value(key).toString());

    if (value.isEmpty() || value.size() > 8192) {
        valid = false;
        return std::nullopt;
    }

    return value;
}

std::optional<QString> optionalBoundedString(const QVariantMap &input, const QString &key, int maximum, bool &valid)
{
    if (!input.contains(key) || !input.value(key).isValid()) {
        return std::nullopt;
    }

    if (input.value(key).typeId() != QMetaType::QString) {
        valid = false;
        return std::nullopt;
    }

    const QString value = JsCompat::trimmed(input.value(key).toString());

    if (value.isEmpty() || value.size() > maximum) {
        valid = false;
        return std::nullopt;
    }

    return value;
}

bool isNumber(const QVariant &value)
{
    const int type = value.typeId();
    return type == QMetaType::Double || type == QMetaType::Int || type == QMetaType::LongLong
        || type == QMetaType::UInt || type == QMetaType::ULongLong;
}

bool integerInRange(const QVariant &value, double minimum, double maximum)
{
    if (!isNumber(value)) {
        return false;
    }

    const double number = value.toDouble();
    return number == std::floor(number) && number >= minimum && number <= maximum;
}

// languageSupportClientEventSchema (strict). Returns the trimmed event.
std::optional<QVariantMap> parseClientEvent(const QVariant &value)
{
    if (value.typeId() != QMetaType::QVariantMap) {
        return std::nullopt;
    }

    static const QSet<QString> allowed{
        QStringLiteral("kind"), QStringLiteral("provider"), QStringLiteral("serverId"), QStringLiteral("workspaceRoot"),
        QStringLiteral("filePath"), QStringLiteral("uri"), QStringLiteral("monacoLanguageId"), QStringLiteral("lspLanguageId"),
        QStringLiteral("version"), QStringLiteral("capabilities"), QStringLiteral("diagnostics"), QStringLiteral("message"),
    };
    static const QSet<QString> kinds{
        QStringLiteral("document-classified"), QStringLiteral("initialized"), QStringLiteral("document-open"),
        QStringLiteral("diagnostics"), QStringLiteral("markers"), QStringLiteral("disconnected"), QStringLiteral("client-error"),
    };
    const QVariantMap input = value.toMap();
    QVariantMap event;
    bool valid = true;

    for (auto iterator = input.cbegin(); iterator != input.cend(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            return std::nullopt;
        }
    }

    if (!kinds.contains(input.value(QStringLiteral("kind")).toString()) || input.value(QStringLiteral("kind")).typeId() != QMetaType::QString) {
        return std::nullopt;
    }

    event.insert(QStringLiteral("kind"), input.value(QStringLiteral("kind")));

    if (input.contains(QStringLiteral("provider"))) {
        const QString provider = input.value(QStringLiteral("provider")).toString();

        if (provider != QStringLiteral("lsp") && provider != QStringLiteral("monaco")) {
            return std::nullopt;
        }

        event.insert(QStringLiteral("provider"), provider);
    }

    for (const auto &[key, maximum] : {
             std::pair{QStringLiteral("serverId"), 100}, std::pair{QStringLiteral("monacoLanguageId"), 100},
             std::pair{QStringLiteral("lspLanguageId"), 100}, std::pair{QStringLiteral("message"), 4000},
         }) {
        if (const auto text = optionalBoundedString(input, key, maximum, valid)) {
            event.insert(key, *text);
        }
    }

    for (const QString &key : {QStringLiteral("workspaceRoot"), QStringLiteral("filePath"), QStringLiteral("uri")}) {
        if (const auto text = optionalPath(input, key, valid)) {
            event.insert(key, *text);
        }
    }

    if (input.contains(QStringLiteral("version"))) {
        if (!integerInRange(input.value(QStringLiteral("version")), 0, 2147483647)) {
            return std::nullopt;
        }

        event.insert(QStringLiteral("version"), input.value(QStringLiteral("version")).toLongLong());
    }

    if (input.contains(QStringLiteral("capabilities"))) {
        if (input.value(QStringLiteral("capabilities")).typeId() != QMetaType::QVariantList) {
            return std::nullopt;
        }

        const QVariantList list = input.value(QStringLiteral("capabilities")).toList();

        if (list.size() > 100) {
            return std::nullopt;
        }

        QVariantList capabilities;

        for (const QVariant &item : list) {
            const QString text = JsCompat::trimmed(item.toString());

            if (item.typeId() != QMetaType::QString || text.isEmpty() || text.size() > 100) {
                return std::nullopt;
            }

            capabilities.append(text);
        }

        event.insert(QStringLiteral("capabilities"), capabilities);
    }

    if (input.contains(QStringLiteral("diagnostics"))) {
        if (input.value(QStringLiteral("diagnostics")).typeId() != QMetaType::QVariantMap) {
            return std::nullopt;
        }

        const QVariantMap diagnostics = input.value(QStringLiteral("diagnostics")).toMap();
        static const QSet<QString> diagnosticKeys{
            QStringLiteral("total"), QStringLiteral("errors"), QStringLiteral("warnings"),
            QStringLiteral("info"), QStringLiteral("hints"), QStringLiteral("details"),
        };
        QVariantMap parsed;

        for (auto iterator = diagnostics.cbegin(); iterator != diagnostics.cend(); ++iterator) {
            if (!diagnosticKeys.contains(iterator.key())) {
                return std::nullopt;
            }
        }

        for (const QString &key : {QStringLiteral("total"), QStringLiteral("errors"), QStringLiteral("warnings"), QStringLiteral("info"), QStringLiteral("hints")}) {
            if (!integerInRange(diagnostics.value(key), 0, 1'000'000)) {
                return std::nullopt;
            }

            parsed.insert(key, diagnostics.value(key).toLongLong());
        }

        if (diagnostics.contains(QStringLiteral("details"))) {
            const QVariantList details = diagnostics.value(QStringLiteral("details")).toList();

            if (diagnostics.value(QStringLiteral("details")).typeId() != QMetaType::QVariantList || details.size() > 8) {
                return std::nullopt;
            }

            QVariantList parsedDetails;
            static const QSet<QString> severities{QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("info"), QStringLiteral("hint")};
            static const QSet<QString> detailKeys{QStringLiteral("owner"), QStringLiteral("severity"), QStringLiteral("line"), QStringLiteral("column"), QStringLiteral("message")};

            for (const QVariant &item : details) {
                const QVariantMap detail = item.toMap();

                if (item.typeId() != QMetaType::QVariantMap) {
                    return std::nullopt;
                }

                for (auto iterator = detail.cbegin(); iterator != detail.cend(); ++iterator) {
                    if (!detailKeys.contains(iterator.key())) {
                        return std::nullopt;
                    }
                }

                QVariantMap parsedDetail;

                if (const auto owner = optionalBoundedString(detail, QStringLiteral("owner"), 100, valid)) {
                    parsedDetail.insert(QStringLiteral("owner"), *owner);
                }

                const QString message = JsCompat::trimmed(detail.value(QStringLiteral("message")).toString());

                if (!severities.contains(detail.value(QStringLiteral("severity")).toString())
                    || !integerInRange(detail.value(QStringLiteral("line")), 1, 10'000'000)
                    || !integerInRange(detail.value(QStringLiteral("column")), 1, 10'000'000)
                    || detail.value(QStringLiteral("message")).typeId() != QMetaType::QString
                    || message.isEmpty() || message.size() > 4000) {
                    return std::nullopt;
                }

                parsedDetail.insert(QStringLiteral("severity"), detail.value(QStringLiteral("severity")));
                parsedDetail.insert(QStringLiteral("line"), detail.value(QStringLiteral("line")).toLongLong());
                parsedDetail.insert(QStringLiteral("column"), detail.value(QStringLiteral("column")).toLongLong());
                parsedDetail.insert(QStringLiteral("message"), message);
                parsedDetails.append(parsedDetail);
            }

            parsed.insert(QStringLiteral("details"), parsedDetails);
        }

        event.insert(QStringLiteral("diagnostics"), parsed);
    }

    if (!valid) {
        return std::nullopt;
    }

    return event;
}

QStringList childDirectories(const QString &parent)
{
    QStringList result;

    for (const QFileInfo &entry : QDir(parent).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        result.append(entry.filePath());
    }

    return result;
}

QString cmakeSourceDirectory(const QString &buildDirectory)
{
    QFile cache(QDir(buildDirectory).filePath(QStringLiteral("CMakeCache.txt")));

    if (!cache.open(QIODevice::ReadOnly)) {
        return {};
    }

    static const QRegularExpression pattern(
        QStringLiteral("^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$"),
        QRegularExpression::MultilineOption
    );
    const auto match = pattern.match(QString::fromUtf8(cache.readAll()));
    return match.hasMatch() ? QFileInfo(match.captured(1).trimmed()).canonicalFilePath() : QString();
}
}

namespace LanguageServers
{
const QList<Definition> &definitions()
{
    static const QList<Definition> list{
        {QStringLiteral("typescript"), QStringLiteral("TypeScript / JavaScript"),
         {QStringLiteral("typescript"), QStringLiteral("javascript"), QStringLiteral("typescriptreact"), QStringLiteral("javascriptreact")},
         {QStringLiteral("typescript-language-server")}, {QStringLiteral("--stdio")},
         {QStringLiteral("tsconfig.json"), QStringLiteral("jsconfig.json"), QStringLiteral("package.json"), QStringLiteral(".git")}, true},
        {QStringLiteral("qml"), QStringLiteral("QML"), {QStringLiteral("qml")}, {QStringLiteral("qmlls"), QStringLiteral("qmlls6")}, {},
         {QStringLiteral(".qmlls.ini"), QStringLiteral("qmldir"), QStringLiteral("CMakeLists.txt"), QStringLiteral(".git")}, true},
        {QStringLiteral("clangd"), QStringLiteral("C / C++"),
         {QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("objective-c"), QStringLiteral("objective-cpp")},
         {QStringLiteral("clangd")}, {QStringLiteral("--background-index")},
         {QStringLiteral("compile_commands.json"), QStringLiteral("compile_flags.txt"), QStringLiteral("CMakeLists.txt"), QStringLiteral(".git")}, true},
        {QStringLiteral("rust"), QStringLiteral("Rust"), {QStringLiteral("rust")}, {QStringLiteral("rust-analyzer")}, {},
         {QStringLiteral("Cargo.toml"), QStringLiteral(".git")}, true},
        {QStringLiteral("python"), QStringLiteral("Python"), {QStringLiteral("python")}, {QStringLiteral("pyright-langserver")}, {QStringLiteral("--stdio")},
         {QStringLiteral("pyrightconfig.json"), QStringLiteral("pyproject.toml"), QStringLiteral("setup.cfg"), QStringLiteral("requirements.txt"), QStringLiteral(".git")}, true},
        {QStringLiteral("go"), QStringLiteral("Go"), {QStringLiteral("go")}, {QStringLiteral("gopls")}, {},
         {QStringLiteral("go.work"), QStringLiteral("go.mod"), QStringLiteral(".git")}, true},
        {QStringLiteral("html"), QStringLiteral("HTML"), {QStringLiteral("html")}, {QStringLiteral("vscode-html-language-server")}, {QStringLiteral("--stdio")},
         {QStringLiteral("package.json"), QStringLiteral(".git")}, false},
        {QStringLiteral("css"), QStringLiteral("CSS / SCSS / Less"), {QStringLiteral("css"), QStringLiteral("scss"), QStringLiteral("less")},
         {QStringLiteral("vscode-css-language-server")}, {QStringLiteral("--stdio")}, {QStringLiteral("package.json"), QStringLiteral(".git")}, false},
        {QStringLiteral("json"), QStringLiteral("JSON"), {QStringLiteral("json")}, {QStringLiteral("vscode-json-language-server")}, {QStringLiteral("--stdio")},
         {QStringLiteral("package.json"), QStringLiteral(".git")}, false},
        {QStringLiteral("yaml"), QStringLiteral("YAML"), {QStringLiteral("yaml")}, {QStringLiteral("yaml-language-server")}, {QStringLiteral("--stdio")},
         {QStringLiteral(".yamllint"), QStringLiteral("package.json"), QStringLiteral(".git")}, true},
        {QStringLiteral("sql"), QStringLiteral("SQL"),
         {QStringLiteral("sql"), QStringLiteral("mysql"), QStringLiteral("postgres"), QStringLiteral("postgresql"), QStringLiteral("sqlite"), QStringLiteral("sqlite3")},
         {QStringLiteral("sql-language-server")}, {QStringLiteral("up"), QStringLiteral("--method"), QStringLiteral("stdio")},
         {QStringLiteral(".sqllsrc.json"), QStringLiteral("package.json"), QStringLiteral("composer.json"), QStringLiteral(".git")}, false},
        {QStringLiteral("bash"), QStringLiteral("Bash / Shell"), {QStringLiteral("shell"), QStringLiteral("bash"), QStringLiteral("sh")},
         {QStringLiteral("bash-language-server")}, {QStringLiteral("start")}, {QStringLiteral(".git")}, true},
        {QStringLiteral("markdown"), QStringLiteral("Markdown"), {QStringLiteral("markdown")}, {QStringLiteral("marksman")}, {QStringLiteral("server")},
         {QStringLiteral(".marksman.toml"), QStringLiteral(".git")}, true},
    };
    return list;
}

const Definition *definition(const QString &serverId)
{
    for (const Definition &candidate : definitions()) {
        if (candidate.id == serverId) {
            return &candidate;
        }
    }

    return nullptr;
}

Workspace resolveWorkspace(
    CoreDatabase &database,
    const Definition &definition,
    const QString &libraryId,
    const QString &requestedWorkspaceRoot,
    const QString &requestedFilePath
)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw IdeHttpError(404, QStringLiteral("Library not found."));
    }

    if (!library->value(QStringLiteral("archivedAt")).isNull()) {
        throw IdeHttpError(409, QStringLiteral("Archived Libraries cannot start language servers."));
    }

    const QString libraryRoot = realDirectory(
        library->value(QStringLiteral("rootPath")).toString(),
        QStringLiteral("The Library root could not be resolved.")
    );
    const QString requestedRoot = requestedWorkspaceRoot.isEmpty()
        ? libraryRoot
        : realDirectory(requestedWorkspaceRoot, QStringLiteral("The requested workspace root could not be resolved."));

    if (!isWithin(libraryRoot, requestedRoot)) {
        throw IdeHttpError(403, QStringLiteral("The requested workspace is outside the Library."));
    }

    QString filePath;

    if (!requestedFilePath.isEmpty()) {
        filePath = QFileInfo(QDir::cleanPath(QDir::current().absoluteFilePath(requestedFilePath))).canonicalFilePath();

        if (filePath.isEmpty()) {
            throw IdeHttpError(404, QStringLiteral("The language-support file could not be resolved."));
        }

        if (!isWithin(libraryRoot, filePath)) {
            throw IdeHttpError(403, QStringLiteral("The language-support file is outside the Library."));
        }

        if (!QFileInfo(filePath).isFile()) {
            throw IdeHttpError(409, QStringLiteral("The language-support target is not a file."));
        }

        if (!isWithin(requestedRoot, filePath)) {
            throw IdeHttpError(403, QStringLiteral("The requested file is outside the workspace."));
        }
    }

    QString current = filePath.isEmpty() ? requestedRoot : QFileInfo(filePath).absolutePath();
    QString workspaceRoot = requestedRoot;

    while (isWithin(requestedRoot, current)) {
        const bool marked = std::any_of(definition.rootMarkers.cbegin(), definition.rootMarkers.cend(), [&](const QString &marker) {
            return QFileInfo::exists(QDir(current).filePath(marker));
        });

        if (marked) {
            workspaceRoot = current;
            break;
        }

        if (current == requestedRoot) {
            break;
        }

        const QString parent = QFileInfo(current).absolutePath();

        if (parent == current) {
            break;
        }

        current = parent;
    }

    return {library->value(QStringLiteral("id")).toString(), libraryRoot, workspaceRoot, filePath};
}

QString resolveExecutable(const Definition &definition, const QString &workspaceRoot)
{
    QStringList directories;
    const auto add = [&directories](const QString &directory) {
        if (!directory.isEmpty() && !directories.contains(directory)) {
            directories.append(directory);
        }
    };

    if (!workspaceRoot.isEmpty()) {
        add(QDir(workspaceRoot).filePath(QStringLiteral("node_modules/.bin")));
    }

#ifdef ARCHIVIST_REPOSITORY_ROOT
    // The Node backend ran from backend/, so it looked in backend/ and the repository root.
    add(QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT)).filePath(QStringLiteral("backend/node_modules/.bin")));
    add(QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT)).filePath(QStringLiteral("node_modules/.bin")));
#endif

    for (const QString &entry : qEnvironmentVariable("PATH").split(QLatin1Char(':'))) {
        add(entry.trimmed());
    }

    for (const QString &candidate : definition.executableCandidates) {
        if (candidate.contains(QLatin1Char('/'))) {
            const QString resolved = QDir::cleanPath(QDir::current().absoluteFilePath(candidate));

            if (isExecutable(resolved)) {
                return resolved;
            }

            continue;
        }

        for (const QString &directory : std::as_const(directories)) {
            const QString resolved = QDir(directory).filePath(candidate);

            if (isExecutable(resolved)) {
                return resolved;
            }
        }
    }

    return {};
}

Launch resolveLaunch(const Definition &definition, const Workspace &workspace)
{
    if (definition.id != QStringLiteral("qml")) {
        return {definition.args, {}, {}};
    }

    const QString workspaceRoot = QFileInfo(workspace.workspaceRoot).canonicalFilePath();
    const QString libraryBuildRoot = QDir(workspace.libraryRoot).filePath(QStringLiteral("build"));
    const QString workspaceBuildRoot = QDir(workspace.workspaceRoot).filePath(QStringLiteral("build"));
    QStringList candidates{
        QDir(libraryBuildRoot).filePath(QStringLiteral("frontend-debug")),
        QDir(libraryBuildRoot).filePath(QStringLiteral("qt-debug")),
        workspaceBuildRoot,
    };
    candidates += childDirectories(libraryBuildRoot);
    candidates += childDirectories(workspaceBuildRoot);
    candidates.removeDuplicates();

    QStringList resolved;

    for (const QString &candidate : std::as_const(candidates)) {
        const QString directory = optionalRealDirectory(candidate);

        if (!directory.isEmpty()) {
            resolved.append(directory);
        }
    }

    QString buildDirectory;

    for (const QString &candidate : std::as_const(resolved)) {
        if (cmakeSourceDirectory(candidate) == workspaceRoot) {
            buildDirectory = candidate;
            break;
        }
    }

    if (buildDirectory.isEmpty()) {
        for (const QString &candidate : std::as_const(resolved)) {
            if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("CMakeCache.txt")))) {
                buildDirectory = candidate;
                break;
            }
        }
    }

    QStringList importDirectories;

    for (const QString &name : {QStringLiteral("qml-tooling"), QStringLiteral("qml")}) {
        const QString directory = optionalRealDirectory(QDir(workspace.workspaceRoot).filePath(name));

        if (!directory.isEmpty()) {
            importDirectories.append(directory);
        }
    }

    QStringList args = definition.args;
    args.append(QStringLiteral("--no-cmake-calls"));

    if (!buildDirectory.isEmpty()) {
        args << QStringLiteral("--build-dir") << buildDirectory;
    }

    for (const QString &directory : std::as_const(importDirectories)) {
        args << QStringLiteral("-I") << directory;
    }

    return {args, buildDirectory, importDirectories};
}
}

LanguageSupportService::LanguageSupportService(QObject *parent)
    : QObject(parent)
{
    m_cleanupTimer.setInterval(15'000);
    connect(&m_cleanupTimer, &QTimer::timeout, this, &LanguageSupportService::removeExpiredPendingSessions);
    m_cleanupTimer.start();
}

LanguageSupportService::~LanguageSupportService()
{
    close();
}

void LanguageSupportService::setPort(quint16 port)
{
    m_port = port;
}

QVariantMap LanguageSupportService::describe(CoreDatabase &database, const Definition &definition, const QVariantMap &input) const
{
    QVariantList languageIds;

    for (const QString &languageId : definition.languageIds) {
        languageIds.append(languageId);
    }

    if (!definition.enabledByDefault) {
        return {
            {QStringLiteral("id"), definition.id},
            {QStringLiteral("displayName"), definition.displayName},
            {QStringLiteral("languageIds"), languageIds},
            {QStringLiteral("state"), QStringLiteral("disabled")},
            {QStringLiteral("executablePath"), QVariant()},
            {QStringLiteral("workspaceRoot"), QVariant()},
        };
    }

    QString workspaceRoot = input.value(QStringLiteral("workspaceRoot")).toString().trimmed();
    const QString libraryId = input.value(QStringLiteral("libraryId")).toString();

    if (!libraryId.isEmpty()) {
        workspaceRoot = LanguageServers::resolveWorkspace(
            database,
            definition,
            libraryId,
            workspaceRoot,
            input.value(QStringLiteral("filePath")).toString()
        ).workspaceRoot;
    }

    const QString executablePath = LanguageServers::resolveExecutable(definition, workspaceRoot);

    return {
        {QStringLiteral("id"), definition.id},
        {QStringLiteral("displayName"), definition.displayName},
        {QStringLiteral("languageIds"), languageIds},
        {QStringLiteral("state"), executablePath.isEmpty() ? QStringLiteral("missing") : QStringLiteral("available")},
        {QStringLiteral("executablePath"), executablePath.isEmpty() ? QVariant() : QVariant(executablePath)},
        {QStringLiteral("workspaceRoot"), workspaceRoot.isEmpty() ? QVariant() : QVariant(workspaceRoot)},
    };
}

QVariantMap LanguageSupportService::config(CoreDatabase &database, const QVariantMap &query) const
{
    bool valid = true;
    const auto libraryId = query.value(QStringLiteral("libraryId")).toString();
    const auto workspaceRoot = optionalPath(query, QStringLiteral("workspaceRoot"), valid);
    const auto filePath = optionalPath(query, QStringLiteral("filePath"), valid);
    const auto serverId = optionalBoundedString(query, QStringLiteral("serverId"), 100, valid);

    if (!valid || (query.contains(QStringLiteral("libraryId")) && !isUuid(libraryId)) || ((workspaceRoot || filePath) && libraryId.isEmpty())) {
        throw IdeHttpError(400, QStringLiteral("Invalid language-support query."));
    }

    const QVariantMap input{
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("workspaceRoot"), workspaceRoot.value_or(QString())},
        {QStringLiteral("filePath"), filePath.value_or(QString())},
    };

    if (serverId) {
        const Definition *definition = LanguageServers::definition(*serverId);

        if (!definition) {
            throw IdeHttpError(404, QStringLiteral("Language server is not registered."));
        }

        return {{QStringLiteral("ok"), true}, {QStringLiteral("server"), describe(database, *definition, input)}};
    }

    QVariantList servers;

    for (const Definition &definition : LanguageServers::definitions()) {
        servers.append(describe(database, definition, input));
    }

    return {{QStringLiteral("ok"), true}, {QStringLiteral("servers"), servers}};
}

QVariantMap LanguageSupportService::createSession(CoreDatabase &database, const QVariantMap &input)
{
    bool valid = true;
    const QString libraryId = input.value(QStringLiteral("libraryId")).toString();
    const auto serverId = optionalBoundedString(input, QStringLiteral("serverId"), 100, valid);
    const auto workspaceRoot = optionalPath(input, QStringLiteral("workspaceRoot"), valid);
    const auto filePath = optionalPath(input, QStringLiteral("filePath"), valid);

    if (!valid || !serverId || input.value(QStringLiteral("libraryId")).typeId() != QMetaType::QString || !isUuid(libraryId)) {
        throw IdeHttpError(400, QStringLiteral("Invalid language-support session request."));
    }

    if (m_port == 0) {
        throw IdeHttpError(503, QStringLiteral("Language Support is still starting."));
    }

    removeExpiredPendingSessions();

    if (m_pending.size() >= maximumPendingSessions) {
        throw IdeHttpError(429, QStringLiteral("Too many language-support sessions are pending."));
    }

    const Definition *definition = LanguageServers::definition(*serverId);

    if (!definition) {
        throw IdeHttpError(404, QStringLiteral("Language server is not registered."));
    }

    if (!definition->enabledByDefault) {
        throw IdeHttpError(409, QStringLiteral("This language server is disabled."));
    }

    const Workspace workspace = LanguageServers::resolveWorkspace(
        database,
        *definition,
        libraryId,
        workspaceRoot.value_or(QString()),
        filePath.value_or(QString())
    );
    const QString executablePath = LanguageServers::resolveExecutable(*definition, workspace.workspaceRoot);

    if (executablePath.isEmpty()) {
        throw IdeHttpError(409, QStringLiteral("%1 is not installed.").arg(definition->displayName));
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString token = randomToken();
    const qint64 expiresAtMilliseconds = QDateTime::currentMSecsSinceEpoch() + pendingLifetimeMilliseconds;
    QUrl socketUrl(QStringLiteral("ws://127.0.0.1:%1/language-support").arg(m_port));
    QUrlQuery socketQuery;
    socketQuery.addQueryItem(QStringLiteral("sessionId"), sessionId);
    socketQuery.addQueryItem(QStringLiteral("token"), token);
    socketUrl.setQuery(socketQuery);
    QVariantList languageIds;

    for (const QString &languageId : definition->languageIds) {
        languageIds.append(languageId);
    }

    const QVariantMap descriptor{
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("serverId"), definition->id},
        {QStringLiteral("displayName"), definition->displayName},
        {QStringLiteral("languageIds"), languageIds},
        {QStringLiteral("state"), QStringLiteral("pending")},
        {QStringLiteral("workspaceRoot"), workspace.workspaceRoot},
        {QStringLiteral("filePath"), workspace.filePath.isEmpty() ? QVariant() : QVariant(workspace.filePath)},
        {QStringLiteral("socketUrl"), socketUrl.toString()},
        {QStringLiteral("expiresAt"), JsCompat::isoTimestamp(QDateTime::fromMSecsSinceEpoch(expiresAtMilliseconds))},
    };

    m_pending.insert(sessionId, {
        descriptor,
        token,
        *definition,
        workspace,
        executablePath,
        LanguageServers::resolveLaunch(*definition, workspace),
        expiresAtMilliseconds,
    });

    qInfo().noquote() << QStringLiteral("[Language Support:%1] session requested session=%2 workspace=%3 file=%4")
                             .arg(definition->id, sessionId, workspace.workspaceRoot, workspace.filePath.isEmpty() ? QStringLiteral("none") : workspace.filePath);
    return descriptor;
}

QVariantList LanguageSupportService::listSessions()
{
    removeExpiredPendingSessions();
    QVariantList sessions;

    for (const Pending &pending : std::as_const(m_pending)) {
        sessions.append(QVariantMap{
            {QStringLiteral("sessionId"), pending.descriptor.value(QStringLiteral("sessionId"))},
            {QStringLiteral("serverId"), pending.descriptor.value(QStringLiteral("serverId"))},
            {QStringLiteral("displayName"), pending.descriptor.value(QStringLiteral("displayName"))},
            {QStringLiteral("state"), QStringLiteral("pending")},
            {QStringLiteral("workspaceRoot"), pending.descriptor.value(QStringLiteral("workspaceRoot"))},
            {QStringLiteral("filePath"), pending.descriptor.value(QStringLiteral("filePath"))},
            {QStringLiteral("processId"), QVariant()},
            {QStringLiteral("connectedAt"), QVariant()},
            {QStringLiteral("expiresAt"), pending.descriptor.value(QStringLiteral("expiresAt"))},
        });
    }

    for (const Active &active : std::as_const(m_active)) {
        sessions.append(active.summary);
    }

    return sessions;
}

void LanguageSupportService::appendEvents(const QVariant &body)
{
    const QVariantMap request = body.toMap();
    const QVariantList inputs = request.value(QStringLiteral("events")).toList();

    if (body.typeId() != QMetaType::QVariantMap || request.size() != 1
        || request.value(QStringLiteral("events")).typeId() != QMetaType::QVariantList
        || inputs.isEmpty() || inputs.size() > 50) {
        throw IdeHttpError(400, QStringLiteral("Invalid language-support client events."));
    }

    QVariantList parsed;

    for (const QVariant &input : inputs) {
        const auto event = parseClientEvent(input);

        if (!event) {
            throw IdeHttpError(400, QStringLiteral("Invalid language-support client events."));
        }

        parsed.append(*event);
    }

    const QString recordedAt = isoNow();

    for (const QVariant &value : std::as_const(parsed)) {
        QVariantMap event = value.toMap();
        event.insert(QStringLiteral("sequence"), m_nextEventSequence++);
        event.insert(QStringLiteral("recordedAt"), recordedAt);
        m_events.append(event);
    }

    if (m_events.size() > maximumEvents) {
        m_events.erase(m_events.begin(), m_events.begin() + (m_events.size() - maximumEvents));
    }
}

QVariantList LanguageSupportService::events(int limit) const
{
    const int normalized = std::max(1, std::min(maximumEvents, limit));
    return m_events.mid(std::max<qsizetype>(0, m_events.size() - normalized));
}

void LanguageSupportService::removeExpiredPendingSessions()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto iterator = m_pending.begin(); iterator != m_pending.end();) {
        iterator = iterator->expiresAtMilliseconds <= now ? m_pending.erase(iterator) : std::next(iterator);
    }
}

QString LanguageSupportService::consumePendingSession(const QString &sessionId, const QString &token)
{
    static const QRegularExpression tokenPattern(QStringLiteral("^[a-fA-F0-9]{64}$"));

    if (!isUuid(sessionId) || !tokenPattern.match(token).hasMatch()) {
        throw IdeHttpError(400, QStringLiteral("Invalid language-support connection."));
    }

    const auto pending = m_pending.constFind(sessionId);

    if (pending == m_pending.cend() || pending->token != token || pending->expiresAtMilliseconds <= QDateTime::currentMSecsSinceEpoch()) {
        m_pending.remove(sessionId);
        throw IdeHttpError(403, QStringLiteral("The language-support session expired or is invalid."));
    }

    m_consumed.insert(sessionId, *pending);
    m_pending.remove(sessionId);
    return sessionId;
}

void LanguageSupportService::launch(const QString &pendingKey, WebSocketConnection *socket)
{
    const Pending pending = m_consumed.take(pendingKey);
    socket->setParent(this);
    connect(socket, &WebSocketConnection::closed, socket, &QObject::deleteLater);

    const QString key = pending.definition.id + QLatin1Char(':') + pending.workspace.workspaceRoot;
    const QString existing = m_activeIdByKey.value(key);

    if (!existing.isEmpty() && m_active.contains(existing)) {
        finishActive(existing, QStringLiteral("replaced by a new workspace session"));
    }

    const int maximumActive = maximumActiveSessions();

    if (m_active.size() >= maximumActive) {
        QStringList active;

        for (const Active &session : std::as_const(m_active)) {
            active.append(session.summary.value(QStringLiteral("serverId")).toString() + QLatin1Char('@')
                          + session.summary.value(QStringLiteral("workspaceRoot")).toString());
        }

        qWarning().noquote() << QStringLiteral("[Language Support] Refused %1 workspace=%2 active=%3/%4 sessions=%5")
                                    .arg(pending.definition.id, pending.workspace.workspaceRoot)
                                    .arg(m_active.size())
                                    .arg(maximumActive)
                                    .arg(active.isEmpty() ? QStringLiteral("none") : active.join(QStringLiteral(" | ")));
        socket->close(1013, QStringLiteral("Too many language servers are active."));
        return;
    }

    if (!pending.launch.buildDirectory.isEmpty()) {
        qInfo().noquote() << QStringLiteral("[Language Support:%1] Using build directory %2").arg(pending.definition.id, pending.launch.buildDirectory);
    }

    if (!pending.launch.importDirectories.isEmpty()) {
        qInfo().noquote() << QStringLiteral("[Language Support:%1] Using import paths %2")
                                 .arg(pending.definition.id, pending.launch.importDirectories.join(QStringLiteral(", ")));
    }

    const QString sessionId = pending.descriptor.value(QStringLiteral("sessionId")).toString();
    qInfo().noquote() << QStringLiteral("[Language Support:%1] launching session=%2 active=%3/%4 cwd=%5 executable=%6 args=%7")
                             .arg(pending.definition.id, sessionId)
                             .arg(m_active.size() + 1)
                             .arg(maximumActive)
                             .arg(pending.workspace.workspaceRoot, pending.executablePath, JsonValues::toJsonString(pending.launch.args));

    auto *process = new QProcess(this);
    process->setProgram(pending.executablePath);
    process->setArguments(pending.launch.args);
    process->setWorkingDirectory(pending.workspace.workspaceRoot);
    process->setProcessEnvironment(minimalEnvironment());

    Active active;
    active.key = key;
    active.process = process;
    active.socket = socket;
    active.startedAtMilliseconds = QDateTime::currentMSecsSinceEpoch();
    active.summary = {
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("serverId"), pending.definition.id},
        {QStringLiteral("displayName"), pending.definition.displayName},
        {QStringLiteral("state"), QStringLiteral("connected")},
        {QStringLiteral("workspaceRoot"), pending.workspace.workspaceRoot},
        {QStringLiteral("filePath"), pending.workspace.filePath.isEmpty() ? QVariant() : QVariant(pending.workspace.filePath)},
        {QStringLiteral("processId"), QVariant()},
        {QStringLiteral("connectedAt"), isoNow()},
        {QStringLiteral("expiresAt"), pending.descriptor.value(QStringLiteral("expiresAt"))},
    };
    m_active.insert(sessionId, active);
    m_activeIdByKey.insert(key, sessionId);
    const QString serverId = pending.definition.id;

    // The editor usually sends `initialize` as soon as the socket opens, which
    // can be before the process has started; hold those until it is running.
    auto queued = std::make_shared<QList<QByteArray>>();
    connect(socket, &WebSocketConnection::textMessageReceived, process, [process, queued](const QByteArray &message) {
        QJsonParseError error;
        QJsonDocument::fromJson(message, &error);

        if (error.error != QJsonParseError::NoError) {
            return;
        }

        const QByteArray framed = "Content-Length: " + QByteArray::number(message.size()) + "\r\n\r\n" + message;

        if (process->state() == QProcess::Starting) {
            queued->append(framed);
        } else if (process->state() == QProcess::Running) {
            process->write(framed);
        }
    });
    connect(socket, &WebSocketConnection::closed, this, [this, sessionId](quint16 code, const QString &reason) {
        finishActive(sessionId, QStringLiteral("socket close %1 %2").arg(code).arg(reason).trimmed());
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, sessionId]() { readServerOutput(sessionId); });
    connect(process, &QProcess::readyReadStandardError, this, [this, sessionId, process]() {
        recordServerStderr(sessionId, process->readAllStandardError(), false);
    });
    connect(process, &QProcess::started, this, [this, sessionId, serverId, process, pending, queued]() {
        for (const QByteArray &framed : std::as_const(*queued)) {
            process->write(framed);
        }

        queued->clear();

        if (!m_active.contains(sessionId)) {
            return;
        }

        m_active[sessionId].summary.insert(QStringLiteral("processId"), process->processId());
        qInfo().noquote() << QStringLiteral("[Language Support:%1] transport connected session=%2 pid=%3 workspace=%4")
                                 .arg(serverId, sessionId)
                                 .arg(process->processId())
                                 .arg(pending.workspace.workspaceRoot);
    });
    connect(process, &QProcess::errorOccurred, this, [this, sessionId, serverId, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart && error != QProcess::Crashed) {
            return;
        }

        if (error == QProcess::FailedToStart) {
            qCritical().noquote() << QStringLiteral("[Language Support:%1] process error session=%2 message=%3")
                                         .arg(serverId, sessionId, process->errorString());
            finishActive(sessionId, QStringLiteral("process error"));
        }
    });
    connect(process, &QProcess::finished, this, [this, sessionId, serverId, process](int exitCode, QProcess::ExitStatus status) {
        recordServerStderr(sessionId, process->readAllStandardError(), true);
        const auto active = m_active.constFind(sessionId);

        if (active != m_active.cend()) {
            const QString detail = active->stderrTail.isEmpty()
                ? QString()
                : QStringLiteral(" stderrTail=") + JsonValues::toJsonString(active->stderrTail);
            qWarning().noquote() << QStringLiteral("[Language Support:%1] process exited session=%2 code=%3 signal=%4 durationMs=%5%6")
                                        .arg(serverId, sessionId)
                                        .arg(status == QProcess::NormalExit ? QString::number(exitCode) : QStringLiteral("null"))
                                        .arg(status == QProcess::CrashExit ? QStringLiteral("SIGNAL") : QStringLiteral("none"))
                                        .arg(QDateTime::currentMSecsSinceEpoch() - active->startedAtMilliseconds)
                                        .arg(detail);
        }

        finishActive(sessionId, QStringLiteral("process exit"));
        process->deleteLater();
    });

    process->start();
}

void LanguageSupportService::readServerOutput(const QString &sessionId)
{
    auto active = m_active.find(sessionId);

    if (active == m_active.end() || !active->process) {
        return;
    }

    active->stdoutBuffer += active->process->readAllStandardOutput();

    while (true) {
        const qsizetype headerEnd = active->stdoutBuffer.indexOf("\r\n\r\n");

        if (headerEnd < 0) {
            return;
        }

        qsizetype length = -1;

        for (const QByteArray &line : active->stdoutBuffer.left(headerEnd).split('\n')) {
            const QByteArray trimmed = line.trimmed();

            if (trimmed.toLower().startsWith("content-length:")) {
                length = trimmed.mid(15).trimmed().toLongLong();
            }
        }

        if (length < 0) {
            // Not a framed message; drop the header block like a strict reader would.
            active->stdoutBuffer.remove(0, headerEnd + 4);
            continue;
        }

        if (active->stdoutBuffer.size() < headerEnd + 4 + length) {
            return;
        }

        const QByteArray body = active->stdoutBuffer.mid(headerEnd + 4, length);
        active->stdoutBuffer.remove(0, headerEnd + 4 + length);

        if (active->socket) {
            active->socket->sendText(body);
        }
    }
}

void LanguageSupportService::recordServerStderr(const QString &sessionId, const QByteArray &data, bool flush)
{
    auto active = m_active.find(sessionId);

    if (active == m_active.end()) {
        return;
    }

    const QString serverId = active->summary.value(QStringLiteral("serverId")).toString();
    active->stderrBuffer += QString::fromUtf8(data);
    static const QRegularExpression lineBreak(QStringLiteral("\\r?\\n"));
    QStringList lines = active->stderrBuffer.split(lineBreak);
    active->stderrBuffer = flush ? QString() : lines.takeLast();

    for (const QString &line : std::as_const(lines)) {
        const QString message = line.trimmed();

        if (message.isEmpty() || (!verboseServerStderr() && isRoutineServerStderr(serverId, message))) {
            continue;
        }

        active->stderrTail.append(message);

        if (active->stderrTail.size() > stderrTailLineLimit) {
            active->stderrTail.removeFirst();
        }

        qWarning().noquote() << QStringLiteral("[Language Support:%1] stderr %2").arg(serverId, message);
    }
}

void LanguageSupportService::finishActive(const QString &sessionId, const QString &reason)
{
    if (!m_active.contains(sessionId)) {
        return;
    }

    const Active active = m_active.take(sessionId);

    if (m_activeIdByKey.value(active.key) == sessionId) {
        m_activeIdByKey.remove(active.key);
    }

    qInfo().noquote() << QStringLiteral("[Language Support:%1] session closed session=%2 reason=%3 active=%4/%5 workspace=%6")
                             .arg(active.summary.value(QStringLiteral("serverId")).toString(), sessionId, reason)
                             .arg(m_active.size())
                             .arg(maximumActiveSessions())
                             .arg(active.summary.value(QStringLiteral("workspaceRoot")).toString());

    if (active.socket) {
        active.socket->close();
    }

    if (active.process && active.process->state() != QProcess::NotRunning) {
        QProcess *process = active.process;
        process->terminate();
        QTimer::singleShot(3000, process, [process]() {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
            }
        });
    }
}

void LanguageSupportService::close()
{
    m_cleanupTimer.stop();
    const QStringList sessions = m_active.keys();

    for (const QString &sessionId : sessions) {
        finishActive(sessionId, QStringLiteral("shutdown"));
    }

    m_pending.clear();
    m_consumed.clear();
}
