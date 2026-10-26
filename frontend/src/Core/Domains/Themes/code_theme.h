#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>

// Converts color themes from other editors into Archivist's code colors.
//
// Supported sources:
//   - VS Code color themes (.json, with comments and trailing commas, and
//     "include" of a base theme)
//   - TextMate / Sublime Text .tmTheme property lists
//   - Sublime Text .sublime-color-scheme files
//
// Those editors color code by TextMate scope ("keyword.control",
// "entity.name.function" ...). Archivist's highlighters use a short list of
// token kinds instead, so each kind is given the color of the theme rule
// that best matches a few representative scopes. The editor's own colors
// (background, selection, cursor, gutter) carry over as they are, because
// the IDE editor is Monaco and reads the same color keys as VS Code.
//
// The converted theme is a JSON object:
//   {
//     "format": 1, "id", "name", "type": "dark" | "light" | "hc-dark" | "hc-light",
//     "sourceFormat": "vscode" | "tmtheme" | "sublime", "sourceFile",
//     "background", "foreground",            // #rrggbb
//     "lineNumbers",                         // #rrggbb, or "" if unset
//     "tokens": { "<monaco token>": { "foreground": "#rrggbb",
//                                     "fontStyle": "italic bold" } },
//     "chatSyntax": { "keyword": "#rrggbb", ..., "commentFontStyle": "" },
//     "editorColors": { "<VS Code color key>": "#rrggbb[aa]" }
//   }
namespace CodeThemes
{
// Reads a file another theme refers to ("include", or tokenColors given as
// a path), relative to the theme being imported. Returns an empty array when
// the file cannot be read.
using IncludeResolver = std::function<QByteArray(const QString &relativePath)>;

struct ImportResult
{
    QJsonObject theme;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// Converts theme file contents. `fileName` picks the format by extension
// and gives the theme a name when the file has none.
ImportResult importTheme(const QByteArray &contents, const QString &fileName,
                         const IncludeResolver &resolveInclude = {});

// Reads and converts a theme file, resolving includes beside it.
ImportResult importThemeFile(const QString &filePath);

// A stable id for a theme name: lower case, dashes, no other punctuation.
QString themeId(const QString &name);

// Parses JSON with // and /* */ comments and trailing commas, as VS Code
// and Sublime theme files are written.
QJsonObject parseRelaxedJson(const QByteArray &contents, QString *error = nullptr);

// The Monaco token names Archivist themes color, in the order rules are
// written.
QStringList editorTokens();

// The chat highlighter kinds (ChatSyntax::Kind), by style key.
QStringList chatSyntaxKeys();
}
