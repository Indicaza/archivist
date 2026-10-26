#include "Core/Domains/Themes/code_theme.h"
#include "Core/Domains/Themes/code_theme_library.h"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QJsonObject token(const QJsonObject &theme, const char *name)
{
    return theme.value(QStringLiteral("tokens")).toObject().value(QLatin1String(name)).toObject();
}

QString editorColor(const QJsonObject &theme, const char *name)
{
    return token(theme, name).value(QStringLiteral("foreground")).toString();
}

QString chatColor(const QJsonObject &theme, const char *name)
{
    return theme.value(QStringLiteral("chatSyntax")).toObject().value(QLatin1String(name)).toString();
}

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(ARCHIVIST_TEST_FIXTURES "/code-themes/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class CoreCodeThemesTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsRelaxedJson()
    {
        QString error;
        const QJsonObject json = CodeThemes::parseRelaxedJson(fixture("relaxed.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(json.value(QStringLiteral("a")).toString(), QStringLiteral("http://x//y"));
        QCOMPARE(json.value(QStringLiteral("b")).toArray().size(), 2);
        QCOMPARE(json.value(QStringLiteral("c")).toString(), QStringLiteral("/* kept */"));
    }

    void convertsVsCodeThemesWithIncludes()
    {
        const CodeThemes::ImportResult result =
            CodeThemes::importTheme(fixture("dark-plus-like.json"), QStringLiteral("dark-plus-like.json"), [](const QString &path) {
                return path == QLatin1String("./base.json") ? fixture("base.json") : QByteArray();
            });
        QVERIFY2(result.ok(), qPrintable(result.error));
        const QJsonObject theme = result.theme;

        QCOMPARE(theme.value(QStringLiteral("id")).toString(), QStringLiteral("dark-plus-like"));
        QCOMPARE(theme.value(QStringLiteral("type")).toString(), QStringLiteral("dark"));
        QCOMPARE(theme.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("vscode"));
        QCOMPARE(theme.value(QStringLiteral("background")).toString(), QStringLiteral("#1e1e1e"));
        QCOMPARE(editorColor(theme, ""), QStringLiteral("#d4d4d4"));

        // "keyword" keeps the general color; flow keywords use keyword.control.
        QCOMPARE(editorColor(theme, "keyword"), QStringLiteral("#569cd6"));
        QCOMPARE(editorColor(theme, "keyword.flow"), QStringLiteral("#c586c0"));
        QCOMPARE(editorColor(theme, "string"), QStringLiteral("#ce9178"));
        QCOMPARE(editorColor(theme, "number"), QStringLiteral("#b5cea8"));
        QCOMPARE(editorColor(theme, "function"), QStringLiteral("#dcdcaa"));
        QCOMPARE(editorColor(theme, "type"), QStringLiteral("#4ec9b0"));
        QCOMPARE(token(theme, "type").value(QStringLiteral("fontStyle")).toString(), QStringLiteral("italic bold"));
        // Unmatched kinds read as plain text rather than Monaco's defaults.
        QCOMPARE(editorColor(theme, "tag"), QStringLiteral("#d4d4d4"));
        QCOMPARE(token(theme, "emphasis").value(QStringLiteral("fontStyle")).toString(), QStringLiteral("italic"));

        QCOMPARE(chatColor(theme, "keyword"), QStringLiteral("#569cd6"));
        QCOMPARE(chatColor(theme, "comment"), QStringLiteral("#6a9955"));
        QCOMPARE(chatColor(theme, "added"), QStringLiteral("#b5cea8"));
        QCOMPARE(chatColor(theme, "addedBackground"), QStringLiteral("#22b5cea8"));
        QVERIFY(!theme.value(QStringLiteral("chatSyntax")).toObject().contains(QStringLiteral("removed")));
        QCOMPARE(theme.value(QStringLiteral("chatSyntax")).toObject().value(QStringLiteral("commentFontStyle")),
                 QJsonValue(QString()));

        const QJsonObject colors = theme.value(QStringLiteral("editorColors")).toObject();
        QCOMPARE(colors.value(QStringLiteral("editor.selectionBackground")).toString(), QStringLiteral("#264f78"));
        QCOMPARE(colors.value(QStringLiteral("editorCursor.foreground")).toString(), QStringLiteral("#aeafad80"));
    }

    void reportsMissingIncludes()
    {
        const CodeThemes::ImportResult result = CodeThemes::importTheme(fixture("dark-plus-like.json"), QStringLiteral("x.json"));
        QVERIFY(!result.ok());
        QVERIFY(result.error.contains(QStringLiteral("./base.json")));
    }

    void convertsTextMateThemes()
    {
        const CodeThemes::ImportResult result = CodeThemes::importTheme(fixture("twilight-like.tmTheme"), QStringLiteral("Twilight.tmTheme"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        const QJsonObject theme = result.theme;

        QCOMPARE(theme.value(QStringLiteral("name")).toString(), QStringLiteral("Twilight Like"));
        QCOMPARE(theme.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("tmtheme"));
        QCOMPARE(theme.value(QStringLiteral("background")).toString(), QStringLiteral("#141414"));
        QCOMPARE(editorColor(theme, "comment"), QStringLiteral("#5f5a60"));
        QCOMPARE(token(theme, "comment").value(QStringLiteral("fontStyle")).toString(), QStringLiteral("italic"));
        QCOMPARE(editorColor(theme, "keyword"), QStringLiteral("#cda869"));
        QCOMPARE(chatColor(theme, "string"), QStringLiteral("#8f9d6a"));
        QCOMPARE(theme.value(QStringLiteral("editorColors")).toObject().value(QStringLiteral("editor.selectionBackground")),
                 QJsonValue(QStringLiteral("#ddf0ff33")));
    }

    void convertsSublimeColorSchemes()
    {
        const CodeThemes::ImportResult result =
            CodeThemes::importTheme(fixture("mocha-like.sublime-color-scheme"), QStringLiteral("Mocha.sublime-color-scheme"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        const QJsonObject theme = result.theme;

        QCOMPARE(theme.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("sublime"));
        QCOMPARE(theme.value(QStringLiteral("background")).toString(), QStringLiteral("#1e1e2e"));
        QCOMPARE(theme.value(QStringLiteral("foreground")).toString(), QStringLiteral("#cdd6f4"));
        QCOMPARE(editorColor(theme, "keyword"), QStringLiteral("#cba6f7"));
        QCOMPARE(token(theme, "keyword").value(QStringLiteral("fontStyle")).toString(), QStringLiteral("italic"));
        QCOMPARE(editorColor(theme, "string"), QStringLiteral("#a6e3a1"));
        QCOMPARE(theme.value(QStringLiteral("editorColors")).toObject().value(QStringLiteral("editor.selectionBackground")),
                 QJsonValue(QStringLiteral("#cba6f740")));
    }

    void detectsLightThemesFromTheirBackground()
    {
        const CodeThemes::ImportResult result = CodeThemes::importTheme(
            R"T({ "colors": { "editor.background": "#fdf6e3" },
                 "tokenColors": [{ "scope": "keyword", "settings": { "foreground": "#859900" } }] })T",
            QStringLiteral("solarized-light-color-theme.json"));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.theme.value(QStringLiteral("type")).toString(), QStringLiteral("light"));
        QCOMPARE(result.theme.value(QStringLiteral("name")).toString(), QStringLiteral("solarized light"));
    }

    void rejectsFilesThatAreNotThemes()
    {
        QVERIFY(!CodeThemes::importTheme("{ \"version\": 1 }", QStringLiteral("package-lock.json")).ok());
        QVERIFY(!CodeThemes::importTheme("not json", QStringLiteral("x.json")).ok());
        QVERIFY(!CodeThemes::importTheme("{ \"contributes\": { \"themes\": [] } }", QStringLiteral("package.json")).ok());
        QVERIFY(!CodeThemes::importTheme("<plist><dict></dict></plist>", QStringLiteral("x.tmTheme")).ok());
    }

    void importsSelectsAndRemovesThemes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(source));
        QVERIFY(writeFile(source + QStringLiteral("/base.json"), fixture("base.json")));
        QVERIFY(writeFile(source + QStringLiteral("/dark-plus-like.json"), fixture("dark-plus-like.json")));

        const QString themes = directory.filePath(QStringLiteral("themes"));
        CodeThemeLibrary library(themes);
        QCOMPARE(library.currentThemeId(), QStringLiteral("archivist"));
        QVERIFY(library.currentTheme().isEmpty());
        QCOMPARE(library.themes().size(), 1);

        QSignalSpy changed(&library, &CodeThemeLibrary::currentThemeChanged);
        const QVariantMap imported =
            library.importFile(QUrl::fromLocalFile(source + QStringLiteral("/dark-plus-like.json")));
        QVERIFY2(imported.value(QStringLiteral("ok")).toBool(), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("id")).toString(), QStringLiteral("dark-plus-like"));
        QCOMPARE(library.currentThemeId(), QStringLiteral("dark-plus-like"));
        QCOMPARE(library.currentTheme().value(QStringLiteral("name")).toString(), QStringLiteral("Dark Plus Like"));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(library.themes().size(), 2);

        // The selection survives a restart.
        {
            CodeThemeLibrary reopened(themes);
            QCOMPARE(reopened.currentThemeId(), QStringLiteral("dark-plus-like"));
            QCOMPARE(reopened.themes().size(), 2);
        }

        library.setCurrentThemeId(QStringLiteral("archivist"));
        QVERIFY(library.currentTheme().isEmpty());
        library.setCurrentThemeId(QStringLiteral("missing"));
        QCOMPARE(library.currentThemeId(), QStringLiteral("archivist"));
        QVERIFY(!library.lastError().isEmpty());

        library.setCurrentThemeId(QStringLiteral("dark-plus-like"));
        QVERIFY(!library.removeTheme(QStringLiteral("archivist")));
        QVERIFY(!library.removeTheme(QStringLiteral("../settings")));
        QVERIFY(library.removeTheme(QStringLiteral("dark-plus-like")));
        QCOMPARE(library.currentThemeId(), QStringLiteral("archivist"));
        QCOMPARE(library.themes().size(), 1);

        const QVariantMap failed = library.importPath(directory.filePath(QStringLiteral("missing.json")));
        QVERIFY(!failed.value(QStringLiteral("ok")).toBool());
        QVERIFY(!library.lastError().isEmpty());
    }

    void exportsArchivistAsAVsCodeTheme()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CodeThemeLibrary library(directory.filePath(QStringLiteral("themes")));
        const QString path = directory.filePath(QStringLiteral("archivist-dark-color-theme.json"));

        const QVariantMap exported = library.exportArchivistTheme(QUrl::fromLocalFile(path));
        QVERIFY2(exported.value(QStringLiteral("ok")).toBool(), qPrintable(exported.value(QStringLiteral("error")).toString()));

        // What Archivist exports, it can read back.
        const CodeThemes::ImportResult result = CodeThemes::importThemeFile(path);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.theme.value(QStringLiteral("name")).toString(), QStringLiteral("Archivist Dark"));
        QCOMPARE(editorColor(result.theme, "keyword"), QStringLiteral("#b7aad2"));
        QCOMPARE(editorColor(result.theme, "string"), QStringLiteral("#aab38c"));
        QCOMPARE(result.theme.value(QStringLiteral("background")).toString(), QStringLiteral("#181713"));

        // The extension folder installs in VS Code as it is.
        const QVariantMap extension = library.exportArchivistExtension(QUrl::fromLocalFile(directory.path()));
        QVERIFY2(extension.value(QStringLiteral("ok")).toBool(), qPrintable(extension.value(QStringLiteral("error")).toString()));
        const QString root = extension.value(QStringLiteral("path")).toString();
        QFile manifestFile(root + QStringLiteral("/package.json"));
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        const QJsonObject contributed = manifest.value(QStringLiteral("contributes")).toObject()
            .value(QStringLiteral("themes")).toArray().first().toObject();
        QCOMPARE(contributed.value(QStringLiteral("uiTheme")).toString(), QStringLiteral("vs-dark"));
        QVERIFY(QFile::exists(QDir(root).filePath(contributed.value(QStringLiteral("path")).toString())));
        QVERIFY(!library.exportArchivistExtension(QUrl::fromLocalFile(directory.filePath(QStringLiteral("nope"))))
                     .value(QStringLiteral("ok")).toBool());
    }
};

QTEST_GUILESS_MAIN(CoreCodeThemesTest)

#include "core_code_themes_test.moc"
