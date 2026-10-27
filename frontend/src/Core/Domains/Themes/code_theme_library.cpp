#include "code_theme_library.h"

#include "code_theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace
{
QString settingsFileName()
{
    return QStringLiteral("settings.json");
}

bool validId(const QString &id)
{
    return !id.isEmpty() && CodeThemes::themeId(id) == id;
}

QVariantMap summary(const QJsonObject &theme)
{
    const QJsonObject chat = theme.value(QStringLiteral("chatSyntax")).toObject();
    QVariantList swatches;

    for (const char *key : {"keyword", "string", "function", "type", "comment"}) {
        swatches.append(chat.value(QLatin1String(key)).toString());
    }

    return {
        {QStringLiteral("id"), theme.value(QStringLiteral("id")).toString()},
        {QStringLiteral("name"), theme.value(QStringLiteral("name")).toString()},
        {QStringLiteral("type"), theme.value(QStringLiteral("type")).toString()},
        {QStringLiteral("sourceFormat"), theme.value(QStringLiteral("sourceFormat")).toString()},
        {QStringLiteral("builtIn"), false},
        {QStringLiteral("background"), theme.value(QStringLiteral("background")).toString()},
        {QStringLiteral("foreground"), theme.value(QStringLiteral("foreground")).toString()},
        {QStringLiteral("swatches"), swatches},
    };
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
} // namespace

CodeThemeLibrary::CodeThemeLibrary(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory.isEmpty() ? defaultDirectory() : directory)
{
    reload();
}

QString CodeThemeLibrary::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("themes"));
}

QVariantList CodeThemeLibrary::themes() const
{
    QVariantList list;
    list.append(QVariantMap{
        {QStringLiteral("id"), QString::fromLatin1(builtInThemeId)},
        {QStringLiteral("name"), QStringLiteral("Archivist")},
        {QStringLiteral("type"), QStringLiteral("dark")},
        {QStringLiteral("sourceFormat"), QString()},
        {QStringLiteral("builtIn"), true},
        {QStringLiteral("background"), QStringLiteral("#201f1b")},
        {QStringLiteral("foreground"), QStringLiteral("#d8d3c9")},
        {QStringLiteral("swatches"),
         QVariantList{QStringLiteral("#c4a5e0"), QStringLiteral("#b5cc8e"), QStringLiteral("#8fb6e0"),
                      QStringLiteral("#e6c88a"), QStringLiteral("#918a7e")}},
    });

    for (const QJsonObject &theme : m_themes) {
        list.append(summary(theme));
    }

    return list;
}

void CodeThemeLibrary::reload()
{
    m_themes.clear();
    const QDir directory(m_directory);
    const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files);

    for (const QString &file : files) {
        if (file == settingsFileName()) {
            continue;
        }

        const QString id = QFileInfo(file).completeBaseName();
        const QJsonObject theme = readTheme(id);

        if (!theme.isEmpty()) {
            m_themes.append(theme);
        }
    }

    std::sort(m_themes.begin(), m_themes.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("name")).toString().compare(b.value(QStringLiteral("name")).toString(),
                                                                  Qt::CaseInsensitive)
            < 0;
    });

    emit themesChanged();
    loadSelection();
}

QString CodeThemeLibrary::themePath(const QString &id) const
{
    return QDir(m_directory).filePath(id + QStringLiteral(".json"));
}

QJsonObject CodeThemeLibrary::readTheme(const QString &id) const
{
    if (!validId(id) || id == QLatin1String(builtInThemeId)) {
        return {};
    }

    QFile file(themePath(id));

    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonObject theme = QJsonDocument::fromJson(file.readAll()).object();

    if (theme.value(QStringLiteral("format")).toInt() != 1 || theme.value(QStringLiteral("id")).toString() != id) {
        return {};
    }

    return theme;
}

void CodeThemeLibrary::loadSelection()
{
    QFile file(QDir(m_directory).filePath(settingsFileName()));
    QString id = QString::fromLatin1(builtInThemeId);

    if (file.open(QIODevice::ReadOnly)) {
        const QString saved = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("current")).toString();

        if (!readTheme(saved).isEmpty()) {
            id = saved;
        }
    }

    const QVariantMap theme = id == QLatin1String(builtInThemeId) ? QVariantMap() : readTheme(id).toVariantMap();

    if (id != m_currentId || theme != m_currentTheme) {
        m_currentId = id;
        m_currentTheme = theme;
        emit currentThemeChanged();
    }
}

void CodeThemeLibrary::saveSelection() const
{
    QDir().mkpath(m_directory);
    QSaveFile file(QDir(m_directory).filePath(settingsFileName()));

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("current"), m_currentId}}).toJson());
        file.commit();
    }
}

void CodeThemeLibrary::setCurrentThemeId(const QString &id)
{
    QVariantMap theme;

    if (id != QLatin1String(builtInThemeId)) {
        const QJsonObject json = readTheme(id);

        if (json.isEmpty()) {
            setLastError(QStringLiteral("That theme is no longer installed."));
            return;
        }

        theme = json.toVariantMap();
    }

    if (id == m_currentId && theme == m_currentTheme) {
        return;
    }

    m_currentId = id;
    m_currentTheme = theme;
    saveSelection();
    emit currentThemeChanged();
}

QVariantMap CodeThemeLibrary::importFile(const QUrl &fileUrl)
{
    return importPath(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString());
}

QVariantMap CodeThemeLibrary::importPath(const QString &filePath)
{
    const CodeThemes::ImportResult result = CodeThemes::importThemeFile(filePath);

    if (!result.ok()) {
        setLastError(result.error);
        return failure(result.error);
    }

    QJsonObject theme = result.theme;
    QString id = theme.value(QStringLiteral("id")).toString();

    if (id == QLatin1String(builtInThemeId)) {
        id = QStringLiteral("archivist-imported");
        theme.insert(QStringLiteral("id"), id);
    }

    if (!QDir().mkpath(m_directory)) {
        const QString message = QStringLiteral("The themes folder could not be created.");
        setLastError(message);
        return failure(message);
    }

    QSaveFile file(themePath(id));

    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(theme).toJson()) < 0 || !file.commit()) {
        const QString message = QStringLiteral("The theme could not be saved.");
        setLastError(message);
        return failure(message);
    }

    setLastError(QString());
    reload();
    setCurrentThemeId(id);

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), theme.value(QStringLiteral("name")).toString()},
    };
}

bool CodeThemeLibrary::removeTheme(const QString &id)
{
    if (!validId(id) || id == QLatin1String(builtInThemeId) || !QFile::exists(themePath(id))) {
        return false;
    }

    if (!QFile::remove(themePath(id))) {
        setLastError(QStringLiteral("The theme could not be removed."));
        return false;
    }

    if (id == m_currentId) {
        setCurrentThemeId(QString::fromLatin1(builtInThemeId));
    }

    reload();
    return true;
}

QByteArray CodeThemeLibrary::archivistThemeJson()
{
    QFile file(QStringLiteral(":/archivist/themes/archivist-dark-color-theme.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QVariantMap CodeThemeLibrary::exportArchivistTheme(const QUrl &fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const QByteArray contents = archivistThemeJson();

    if (contents.isEmpty()) {
        return failure(QStringLiteral("The Archivist theme is missing from this build."));
    }

    QSaveFile file(path);

    if (path.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(contents) < 0 || !file.commit()) {
        const QString message = QStringLiteral("The theme could not be written to %1.").arg(QFileInfo(path).fileName());
        setLastError(message);
        return failure(message);
    }

    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

QVariantMap CodeThemeLibrary::exportArchivistExtension(const QUrl &folderUrl)
{
    const QString parent = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    const QByteArray theme = archivistThemeJson();

    if (theme.isEmpty()) {
        return failure(QStringLiteral("The Archivist theme is missing from this build."));
    }

    if (parent.isEmpty() || !QFileInfo(parent).isDir()) {
        const QString message = QStringLiteral("Pick a folder to export into.");
        setLastError(message);
        return failure(message);
    }

    const QDir extension(QDir(parent).filePath(QStringLiteral("archivist-theme")));
    const QJsonObject manifest{
        {QStringLiteral("name"), QStringLiteral("archivist-theme")},
        {QStringLiteral("displayName"), QStringLiteral("Archivist Theme")},
        {QStringLiteral("description"), QStringLiteral("The warm dark colors of the Archivist app.")},
        {QStringLiteral("version"), QStringLiteral("0.1.0")},
        {QStringLiteral("publisher"), QStringLiteral("archivist")},
        {QStringLiteral("engines"), QJsonObject{{QStringLiteral("vscode"), QStringLiteral("^1.70.0")}}},
        {QStringLiteral("categories"), QJsonArray{QStringLiteral("Themes")}},
        {QStringLiteral("contributes"),
         QJsonObject{{QStringLiteral("themes"),
                      QJsonArray{QJsonObject{
                          {QStringLiteral("label"), QStringLiteral("Archivist Dark")},
                          {QStringLiteral("uiTheme"), QStringLiteral("vs-dark")},
                          {QStringLiteral("path"), QStringLiteral("./themes/archivist-dark-color-theme.json")},
                      }}}}},
    };
    const QByteArray readme =
        "# Archivist Theme\n\n"
        "The colors of the Archivist app for VS Code and editors built on it.\n\n"
        "Install: run **Developer: Install Extension from Location...** and pick this folder,\n"
        "or package it with `npx @vscode/vsce package` to share as a .vsix or publish to Open VSX.\n";

    const auto write = [](const QString &path, const QByteArray &contents) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
    };

    const bool ok = extension.mkpath(QStringLiteral("themes"))
        && write(extension.filePath(QStringLiteral("package.json")), QJsonDocument(manifest).toJson())
        && write(extension.filePath(QStringLiteral("themes/archivist-dark-color-theme.json")), theme)
        && write(extension.filePath(QStringLiteral("README.md")), readme);

    if (!ok) {
        const QString message = QStringLiteral("The theme could not be written to %1.").arg(extension.absolutePath());
        setLastError(message);
        return failure(message);
    }

    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), extension.absolutePath()}};
}

void CodeThemeLibrary::setLastError(const QString &message)
{
    if (message != m_lastError) {
        m_lastError = message;
        emit lastErrorChanged();
    }
}
