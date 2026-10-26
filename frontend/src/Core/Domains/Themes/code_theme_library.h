#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// The code color themes the user has imported, and which one is in use.
//
// Imported themes are converted once (see CodeThemes::importTheme) and kept
// as JSON in the themes folder, with the selection in settings.json beside
// them. "archivist" is the built-in theme: while it is selected,
// currentTheme is empty and the app keeps its own colors.
class CodeThemeLibrary final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(QString currentThemeId READ currentThemeId WRITE setCurrentThemeId NOTIFY currentThemeChanged)
    Q_PROPERTY(QVariantMap currentTheme READ currentTheme NOTIFY currentThemeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    static constexpr const char *builtInThemeId = "archivist";

    // `directory` defaults to <app data>/themes.
    explicit CodeThemeLibrary(const QString &directory = QString(), QObject *parent = nullptr);

    static QString defaultDirectory();

    QString directory() const { return m_directory; }

    // [{id, name, type, sourceFormat, builtIn, background, foreground,
    //   swatches: [keyword, string, function, type, comment]}], the built-in
    // theme first, then by name.
    QVariantList themes() const;
    QString currentThemeId() const { return m_currentId; }
    void setCurrentThemeId(const QString &id);
    QVariantMap currentTheme() const { return m_currentTheme; }
    QString lastError() const { return m_lastError; }

    // Converts and keeps a theme file, then selects it. Importing a theme
    // with the same name replaces it. Returns {ok, id, name, error}.
    Q_INVOKABLE QVariantMap importFile(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap importPath(const QString &filePath);

    Q_INVOKABLE bool removeTheme(const QString &id);

    // Writes Archivist's own colors as a VS Code color theme file, the format
    // VS Code, Cursor, VSCodium and Open VSX theme extensions use.
    Q_INVOKABLE QVariantMap exportArchivistTheme(const QUrl &fileUrl);

    // Writes an installable VS Code theme extension folder,
    // <folder>/archivist-theme, holding the theme above. Returns
    // {ok, path, error}.
    Q_INVOKABLE QVariantMap exportArchivistExtension(const QUrl &folderUrl);
    static QByteArray archivistThemeJson();

    Q_INVOKABLE void reload();

signals:
    void themesChanged();
    void currentThemeChanged();
    void lastErrorChanged();

private:
    QString themePath(const QString &id) const;
    QJsonObject readTheme(const QString &id) const;
    void loadSelection();
    void saveSelection() const;
    void setLastError(const QString &message);

    QString m_directory;
    QString m_currentId;
    QVariantMap m_currentTheme;
    QList<QJsonObject> m_themes;
    QString m_lastError;
};
