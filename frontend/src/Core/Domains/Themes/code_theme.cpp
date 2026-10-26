#include "code_theme.h"

#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace CodeThemes
{
namespace
{
constexpr int maxIncludeDepth = 8;

// A small color value, so the converter needs nothing beyond QtCore.
struct Rgba
{
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 1;
    bool valid = false;

    static Rgba fromBytes(int red, int green, int blue, int alpha = 255)
    {
        return {red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0, true};
    }

    bool isValid() const { return valid; }
    bool opaque() const { return byte(a) == 255; }
    static int byte(double value) { return int(std::lround(std::clamp(value, 0.0, 1.0) * 255.0)); }
};

Rgba fromHsl(double h, double s, double l)
{
    // h, s and l in 0..1.
    auto channel = [](double p, double q, double t) {
        if (t < 0) {
            t += 1;
        }

        if (t > 1) {
            t -= 1;
        }

        if (t < 1.0 / 6) {
            return p + (q - p) * 6 * t;
        }

        if (t < 0.5) {
            return q;
        }

        if (t < 2.0 / 3) {
            return p + (q - p) * (2.0 / 3 - t) * 6;
        }

        return p;
    };

    if (s <= 0) {
        return {l, l, l, 1, true};
    }

    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    return {channel(p, q, h + 1.0 / 3), channel(p, q, h), channel(p, q, h - 1.0 / 3), 1, true};
}

Rgba namedColor(const QString &name)
{
    static const QHash<QString, Rgba> names = {
        {QStringLiteral("black"), Rgba::fromBytes(0, 0, 0)},
        {QStringLiteral("white"), Rgba::fromBytes(255, 255, 255)},
        {QStringLiteral("transparent"), Rgba::fromBytes(0, 0, 0, 0)},
        {QStringLiteral("red"), Rgba::fromBytes(255, 0, 0)},
        {QStringLiteral("green"), Rgba::fromBytes(0, 128, 0)},
        {QStringLiteral("blue"), Rgba::fromBytes(0, 0, 255)},
        {QStringLiteral("yellow"), Rgba::fromBytes(255, 255, 0)},
        {QStringLiteral("orange"), Rgba::fromBytes(255, 165, 0)},
        {QStringLiteral("purple"), Rgba::fromBytes(128, 0, 128)},
        {QStringLiteral("gray"), Rgba::fromBytes(128, 128, 128)},
        {QStringLiteral("grey"), Rgba::fromBytes(128, 128, 128)},
        {QStringLiteral("cyan"), Rgba::fromBytes(0, 255, 255)},
        {QStringLiteral("magenta"), Rgba::fromBytes(255, 0, 255)},
        {QStringLiteral("pink"), Rgba::fromBytes(255, 192, 203)},
        {QStringLiteral("brown"), Rgba::fromBytes(165, 42, 42)},
    };
    return names.value(name.toLower());
}

// ---------------------------------------------------------------------------
// Colors

int hexDigit(QChar c)
{
    const ushort u = c.unicode();

    if (u >= '0' && u <= '9') {
        return u - '0';
    }

    if (u >= 'a' && u <= 'f') {
        return u - 'a' + 10;
    }

    if (u >= 'A' && u <= 'F') {
        return u - 'A' + 10;
    }

    return -1;
}

// VS Code and TextMate write #rgb, #rgba, #rrggbb and #rrggbbaa. Rgba reads
// eight digits as #aarrggbb, so these are parsed here.
Rgba parseHexColor(const QString &value)
{
    if (!value.startsWith(QLatin1Char('#'))) {
        return {};
    }

    const QString digits = value.mid(1);
    QList<int> parts;

    for (const QChar c : digits) {
        const int digit = hexDigit(c);

        if (digit < 0) {
            return {};
        }

        parts.append(digit);
    }

    if (parts.size() == 3 || parts.size() == 4) {
        return Rgba::fromBytes(parts[0] * 17, parts[1] * 17, parts[2] * 17, parts.size() == 4 ? parts[3] * 17 : 255);
    }

    if (parts.size() == 6 || parts.size() == 8) {
        return Rgba::fromBytes(parts[0] * 16 + parts[1], parts[2] * 16 + parts[3], parts[4] * 16 + parts[5],
                               parts.size() == 8 ? parts[6] * 16 + parts[7] : 255);
    }

    return {};
}

double parseComponent(QString part, double scale)
{
    part = part.trimmed();

    if (part.endsWith(QLatin1Char('%'))) {
        return part.chopped(1).toDouble() / 100.0 * scale;
    }

    return part.toDouble();
}

// Sublime color schemes also use rgb(), hsl(), CSS names, var() and
// color(<base> alpha(<a>)).
Rgba parseCssColor(const QString &raw, const QVariantMap &variables, int depth = 0)
{
    const QString value = raw.trimmed();

    if (value.isEmpty() || depth > 8) {
        return {};
    }

    if (value.startsWith(QLatin1Char('#'))) {
        return parseHexColor(value);
    }

    static const QRegularExpression varPattern(QStringLiteral(R"(^var\(\s*([\w-]+)\s*\)$)"));
    const QRegularExpressionMatch varMatch = varPattern.match(value);

    if (varMatch.hasMatch()) {
        return parseCssColor(variables.value(varMatch.captured(1)).toString(), variables, depth + 1);
    }

    static const QRegularExpression functionPattern(QStringLiteral(R"(^(rgba?|hsla?)\(([^)]*)\)$)"),
                                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch functionMatch = functionPattern.match(value);

    if (functionMatch.hasMatch()) {
        const QString name = functionMatch.captured(1).toLower();
        QString arguments = functionMatch.captured(2);
        arguments.replace(QLatin1Char('/'), QLatin1Char(','));
        const QStringList parts = arguments.contains(QLatin1Char(','))
            ? arguments.split(QLatin1Char(','))
            : arguments.split(QLatin1Char(' '), Qt::SkipEmptyParts);

        if (parts.size() < 3) {
            return {};
        }

        const double alpha = parts.size() > 3 ? parseComponent(parts[3], 1.0) : 1.0;
        Rgba color;

        if (name.startsWith(QLatin1String("rgb"))) {
            color = {parseComponent(parts[0], 255.0) / 255.0, parseComponent(parts[1], 255.0) / 255.0,
                     parseComponent(parts[2], 255.0) / 255.0, 1, true};
        } else {
            const double hue = std::fmod(std::fmod(parts[0].trimmed().remove(QStringLiteral("deg")).toDouble(), 360.0)
                                             + 360.0,
                                         360.0);
            color = fromHsl(hue / 360.0, parseComponent(parts[1], 1.0), parseComponent(parts[2], 1.0));
        }

        color.a = std::clamp(alpha, 0.0, 1.0);
        return color;
    }

    if (value.startsWith(QLatin1String("color("), Qt::CaseInsensitive) && value.endsWith(QLatin1Char(')'))) {
        // color(<base> <adjusters>): only the alpha adjuster is applied.
        const QString inner = value.mid(6, value.size() - 7).trimmed();
        int baseEnd = 0;
        int nesting = 0;

        for (; baseEnd < inner.size(); ++baseEnd) {
            const QChar c = inner.at(baseEnd);

            if (c == QLatin1Char('(')) {
                ++nesting;
            } else if (c == QLatin1Char(')')) {
                --nesting;
            } else if (c == QLatin1Char(' ') && nesting == 0) {
                break;
            }
        }

        Rgba color = parseCssColor(inner.left(baseEnd), variables, depth + 1);
        static const QRegularExpression alphaPattern(QStringLiteral(R"(\b(?:alpha|a)\(\s*([\d.]+%?)\s*\))"));
        const QRegularExpressionMatch alphaMatch = alphaPattern.match(inner.mid(baseEnd));

        if (color.isValid() && alphaMatch.hasMatch()) {
            color.a = std::clamp(parseComponent(alphaMatch.captured(1), 1.0), 0.0, 1.0);
        }

        return color;
    }

    return namedColor(value);
}

QString hexByte(double value)
{
    return QStringLiteral("%1").arg(Rgba::byte(value), 2, 16, QLatin1Char('0'));
}

QString hex6(const Rgba &color)
{
    return QLatin1Char('#') + hexByte(color.r) + hexByte(color.g) + hexByte(color.b);
}

// #rrggbb, or #rrggbbaa when translucent: the VS Code and Monaco form.
QString hexRgba(const Rgba &color)
{
    return color.opaque() ? hex6(color) : hex6(color) + hexByte(color.a);
}

// #aarrggbb: the QML form of a translucent color.
QString hexArgb(const Rgba &color)
{
    return QLatin1Char('#') + hexByte(color.a) + hexByte(color.r) + hexByte(color.g) + hexByte(color.b);
}

// A translucent foreground drawn over the background, so text colors are
// always opaque.
Rgba flatten(const Rgba &color, const Rgba &background)
{
    if (!color.isValid() || color.opaque() || !background.isValid()) {
        return color;
    }

    const double a = color.a;
    return {color.r * a + background.r * (1 - a), color.g * a + background.g * (1 - a),
            color.b * a + background.b * (1 - a), 1, true};
}

bool isLight(const Rgba &color)
{
    const double luminance = 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
    return luminance > 0.5;
}

// ---------------------------------------------------------------------------
// Scope rules

struct Rule
{
    QList<QStringList> selectors;
    Rgba foreground;
    QString fontStyle;
    bool hasFontStyle = false;
};

// Only plain selectors ("keyword.control") are kept. Descendant selectors
// ("source.python string") need the full scope stack, which Archivist's
// highlighters do not have; exclusions ("string - string.regexp") keep their
// left side.
QList<QStringList> parseSelectors(const QJsonValue &scope)
{
    QStringList raw;

    if (scope.isArray()) {
        for (const QJsonValue &item : scope.toArray()) {
            raw += item.toString().split(QLatin1Char(','));
        }
    } else {
        raw = scope.toString().split(QLatin1Char(','));
    }

    QList<QStringList> selectors;

    for (QString selector : raw) {
        const int exclusion = selector.indexOf(QLatin1String(" -"));

        if (exclusion >= 0) {
            selector = selector.left(exclusion);
        }

        selector = selector.trimmed();

        if (selector.isEmpty() || selector.contains(QLatin1Char(' ')) || selector.contains(QLatin1Char('('))
            || selector.contains(QLatin1Char('|')) || selector.contains(QLatin1Char('&'))
            || selector.contains(QLatin1Char(':')) || selector.startsWith(QLatin1Char('-'))) {
            continue;
        }

        selectors.append(selector.split(QLatin1Char('.'), Qt::SkipEmptyParts));
    }

    return selectors;
}

int selectorScore(const QStringList &selector, const QStringList &target)
{
    if (selector.isEmpty() || selector.size() > target.size()) {
        return 0;
    }

    for (int index = 0; index < selector.size(); ++index) {
        if (selector[index] != target[index]) {
            return 0;
        }
    }

    return selector.size();
}

struct Style
{
    Rgba foreground;
    QString fontStyle;
    bool hasFontStyle = false;
    bool matched = false;
};

// TextMate resolves each property on its own: the most specific matching
// selector wins, and a later rule wins a tie.
Style resolveScope(const QList<Rule> &rules, const QString &scope)
{
    const QStringList target = scope.split(QLatin1Char('.'));
    Style style;
    int foregroundScore = 0;
    int fontScore = 0;

    for (const Rule &rule : rules) {
        int score = 0;

        for (const QStringList &selector : rule.selectors) {
            score = std::max(score, selectorScore(selector, target));
        }

        if (score == 0) {
            continue;
        }

        if (rule.foreground.isValid() && score >= foregroundScore) {
            foregroundScore = score;
            style.foreground = rule.foreground;
            style.matched = true;
        }

        if (rule.hasFontStyle && score >= fontScore) {
            fontScore = score;
            style.fontStyle = rule.fontStyle;
            style.hasFontStyle = true;
        }
    }

    return style;
}

// The first representative scope the theme colors.
Style resolveCandidates(const QList<Rule> &rules, const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const Style style = resolveScope(rules, candidate);

        if (style.matched) {
            return style;
        }
    }

    return {};
}

QString normalizedFontStyle(const QString &value)
{
    QStringList parts;
    const QStringList words = value.toLower().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);

    for (const QString &word : words) {
        if ((word == QLatin1String("italic") || word == QLatin1String("bold") || word == QLatin1String("underline")
             || word == QLatin1String("strikethrough"))
            && !parts.contains(word)) {
            parts.append(word);
        }
    }

    return parts.join(QLatin1Char(' '));
}

// ---------------------------------------------------------------------------
// Targets

struct Target
{
    const char *token;
    QStringList candidates;
};

const QList<Target> &editorTargets()
{
    static const QList<Target> targets = {
        {"comment", {QStringLiteral("comment.line"), QStringLiteral("comment")}},
        {"comment.doc",
         {QStringLiteral("comment.block.documentation"), QStringLiteral("comment.block"), QStringLiteral("comment")}},
        {"keyword", {QStringLiteral("keyword"), QStringLiteral("storage.type"), QStringLiteral("keyword.control")}},
        {"keyword.flow", {QStringLiteral("keyword.control"), QStringLiteral("keyword")}},
        {"keyword.directive",
         {QStringLiteral("meta.preprocessor"), QStringLiteral("keyword.control.directive"),
          QStringLiteral("keyword.control"), QStringLiteral("keyword")}},
        {"operator", {QStringLiteral("keyword.operator")}},
        {"delimiter", {QStringLiteral("punctuation.separator"), QStringLiteral("punctuation")}},
        {"type",
         {QStringLiteral("entity.name.type"), QStringLiteral("support.type"), QStringLiteral("entity.name.class"),
          QStringLiteral("storage.type")}},
        {"type.identifier",
         {QStringLiteral("entity.name.type"), QStringLiteral("entity.name.class"), QStringLiteral("support.class"),
          QStringLiteral("support.type")}},
        {"function", {QStringLiteral("entity.name.function"), QStringLiteral("support.function")}},
        {"predefined", {QStringLiteral("support.function"), QStringLiteral("entity.name.function")}},
        {"macro",
         {QStringLiteral("entity.name.function.macro"), QStringLiteral("entity.name.function.preprocessor"),
          QStringLiteral("meta.preprocessor"), QStringLiteral("entity.name.function")}},
        {"namespace",
         {QStringLiteral("entity.name.namespace"), QStringLiteral("entity.name.type.namespace"),
          QStringLiteral("entity.name.type")}},
        {"identifier", {QStringLiteral("variable.other.readwrite"), QStringLiteral("variable")}},
        {"variable", {QStringLiteral("variable.other.readwrite"), QStringLiteral("variable")}},
        {"variable.predefined",
         {QStringLiteral("variable.language"), QStringLiteral("support.variable"), QStringLiteral("variable")}},
        {"variable.parameter", {QStringLiteral("variable.parameter"), QStringLiteral("variable")}},
        {"variable.other.property",
         {QStringLiteral("variable.other.property"), QStringLiteral("variable.other.object.property"),
          QStringLiteral("support.type.property-name"), QStringLiteral("variable")}},
        {"string", {QStringLiteral("string.quoted"), QStringLiteral("string")}},
        {"string.escape", {QStringLiteral("constant.character.escape"), QStringLiteral("string")}},
        {"string.escape.invalid", {QStringLiteral("invalid.illegal"), QStringLiteral("invalid")}},
        {"string.key.json",
         {QStringLiteral("support.type.property-name.json"), QStringLiteral("support.type.property-name"),
          QStringLiteral("string")}},
        {"string.link", {QStringLiteral("markup.underline.link"), QStringLiteral("string")}},
        {"regexp", {QStringLiteral("string.regexp"), QStringLiteral("string")}},
        {"number", {QStringLiteral("constant.numeric"), QStringLiteral("constant")}},
        {"constant",
         {QStringLiteral("constant.language"), QStringLiteral("variable.other.constant"), QStringLiteral("constant")}},
        {"annotation",
         {QStringLiteral("meta.decorator"), QStringLiteral("entity.name.function.decorator"),
          QStringLiteral("storage.type.annotation"), QStringLiteral("punctuation.decorator")}},
        {"tag", {QStringLiteral("entity.name.tag")}},
        {"metatag", {QStringLiteral("meta.tag.preprocessor"), QStringLiteral("entity.name.tag"), QStringLiteral("meta.tag")}},
        {"attribute.name", {QStringLiteral("entity.other.attribute-name")}},
        {"attribute.value", {QStringLiteral("string.quoted"), QStringLiteral("string")}},
        {"emphasis", {QStringLiteral("markup.italic")}},
        {"strong", {QStringLiteral("markup.bold")}},
        {"invalid", {QStringLiteral("invalid.illegal"), QStringLiteral("invalid")}},
    };
    return targets;
}

const QList<Target> &chatTargets()
{
    static const QList<Target> targets = {
        {"keyword", {QStringLiteral("keyword"), QStringLiteral("storage.type"), QStringLiteral("keyword.control")}},
        {"string", {QStringLiteral("string.quoted"), QStringLiteral("string")}},
        {"comment", {QStringLiteral("comment.line"), QStringLiteral("comment")}},
        {"number", {QStringLiteral("constant.numeric"), QStringLiteral("constant")}},
        {"type",
         {QStringLiteral("entity.name.type"), QStringLiteral("support.type"), QStringLiteral("entity.name.class"),
          QStringLiteral("storage.type")}},
        {"function",
         {QStringLiteral("entity.name.function"), QStringLiteral("support.function"),
          QStringLiteral("meta.function-call")}},
        {"constant",
         {QStringLiteral("constant.language"), QStringLiteral("variable.other.constant"), QStringLiteral("constant")}},
        {"meta",
         {QStringLiteral("meta.preprocessor"), QStringLiteral("keyword.control.directive"),
          QStringLiteral("meta.decorator"), QStringLiteral("entity.name.function.macro"),
          QStringLiteral("keyword.control")}},
        {"added", {QStringLiteral("markup.inserted")}},
        {"removed", {QStringLiteral("markup.deleted")}},
    };
    return targets;
}

// ---------------------------------------------------------------------------
// Parsed source theme, before conversion

struct SourceTheme
{
    QString name;
    QString type; // "dark", "light", "hc-dark", "hc-light" or empty
    QString format;
    QMap<QString, Rgba> colors; // VS Code color keys
    QList<Rule> rules;
    Rgba globalForeground;
    Rgba globalBackground;
};

Rule ruleFromSettings(const QJsonValue &scope, const QJsonObject &settings, const QVariantMap &variables = {})
{
    Rule rule;
    rule.selectors = parseSelectors(scope);
    rule.foreground = parseCssColor(settings.value(QStringLiteral("foreground")).toString(), variables);

    const QJsonValue fontStyle = settings.contains(QStringLiteral("fontStyle"))
        ? settings.value(QStringLiteral("fontStyle"))
        : settings.value(QStringLiteral("font_style"));

    if (fontStyle.isString()) {
        rule.fontStyle = normalizedFontStyle(fontStyle.toString());
        rule.hasFontStyle = true;
    }

    return rule;
}

// TextMate settings array: the entry without a scope holds the editor colors.
void readTextMateSettings(const QJsonArray &settings, SourceTheme &theme)
{
    static const QList<QPair<const char *, const char *>> globals = {
        {"background", "editor.background"},
        {"foreground", "editor.foreground"},
        {"caret", "editorCursor.foreground"},
        {"selection", "editor.selectionBackground"},
        {"inactiveSelection", "editor.inactiveSelectionBackground"},
        {"lineHighlight", "editor.lineHighlightBackground"},
        {"invisibles", "editorWhitespace.foreground"},
        {"gutter", "editorGutter.background"},
        {"gutterForeground", "editorLineNumber.foreground"},
        {"findHighlight", "editor.findMatchHighlightBackground"},
        {"guide", "editorIndentGuide.background1"},
        {"activeGuide", "editorIndentGuide.activeBackground1"},
    };

    for (const QJsonValue &value : settings) {
        const QJsonObject entry = value.toObject();
        const QJsonObject entrySettings = entry.value(QStringLiteral("settings")).toObject();

        if (!entry.contains(QStringLiteral("scope"))) {
            for (const auto &[key, colorKey] : globals) {
                const Rgba color = parseHexColor(entrySettings.value(QLatin1String(key)).toString());

                if (color.isValid() && !theme.colors.contains(QLatin1String(colorKey))) {
                    theme.colors.insert(QLatin1String(colorKey), color);
                }
            }

            const Rgba foreground = parseHexColor(entrySettings.value(QStringLiteral("foreground")).toString());
            const Rgba background = parseHexColor(entrySettings.value(QStringLiteral("background")).toString());

            if (foreground.isValid()) {
                theme.globalForeground = foreground;
            }

            if (background.isValid()) {
                theme.globalBackground = background;
            }

            continue;
        }

        const Rule rule = ruleFromSettings(entry.value(QStringLiteral("scope")), entrySettings);

        if (!rule.selectors.isEmpty()) {
            theme.rules.append(rule);
        }
    }
}

// ---------------------------------------------------------------------------
// Property lists (.tmTheme)

QVariant readPlistValue(QXmlStreamReader &xml);

QVariant readPlistDict(QXmlStreamReader &xml)
{
    QVariantMap map;
    QString key;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("key")) {
            key = xml.readElementText();
        } else {
            map.insert(key, readPlistValue(xml));
        }
    }

    return map;
}

QVariant readPlistValue(QXmlStreamReader &xml)
{
    const QStringView name = xml.name();

    if (name == QLatin1String("dict")) {
        return readPlistDict(xml);
    }

    if (name == QLatin1String("array")) {
        QVariantList list;

        while (xml.readNextStartElement()) {
            list.append(readPlistValue(xml));
        }

        return list;
    }

    if (name == QLatin1String("true") || name == QLatin1String("false")) {
        const bool value = name == QLatin1String("true");
        xml.skipCurrentElement();
        return value;
    }

    if (name == QLatin1String("integer")) {
        return xml.readElementText().toLongLong();
    }

    if (name == QLatin1String("real")) {
        return xml.readElementText().toDouble();
    }

    return xml.readElementText(QXmlStreamReader::IncludeChildElements);
}

QJsonObject parsePlist(const QByteArray &contents, QString *error)
{
    QXmlStreamReader xml(contents);

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("plist")) {
            continue;
        }

        if (xml.name() == QLatin1String("dict")) {
            const QVariant value = readPlistDict(xml);

            if (xml.hasError()) {
                break;
            }

            return QJsonObject::fromVariantMap(value.toMap());
        }

        xml.skipCurrentElement();
    }

    if (error) {
        *error = xml.hasError() ? QStringLiteral("The property list could not be read: %1").arg(xml.errorString())
                                : QStringLiteral("The file is not a TextMate theme.");
    }

    return {};
}

// ---------------------------------------------------------------------------
// Readers

bool readTmTheme(const QByteArray &contents, SourceTheme &theme, QString &error)
{
    const QJsonObject plist = parsePlist(contents, &error);

    if (plist.isEmpty()) {
        if (error.isEmpty()) {
            error = QStringLiteral("The TextMate theme is empty.");
        }

        return false;
    }

    const QJsonArray settings = plist.value(QStringLiteral("settings")).toArray();

    if (settings.isEmpty()) {
        error = QStringLiteral("The TextMate theme has no settings.");
        return false;
    }

    theme.format = QStringLiteral("tmtheme");
    theme.name = plist.value(QStringLiteral("name")).toString();
    readTextMateSettings(settings, theme);
    return true;
}

bool readVsCodeTheme(const QJsonObject &json, SourceTheme &theme, QString &error,
                     const IncludeResolver &resolveInclude, int depth)
{
    const QString include = json.value(QStringLiteral("include")).toString();

    if (!include.isEmpty()) {
        if (depth >= maxIncludeDepth) {
            error = QStringLiteral("The theme includes too many other themes.");
            return false;
        }

        const QByteArray base = resolveInclude ? resolveInclude(include) : QByteArray();

        if (base.isEmpty()) {
            error = QStringLiteral("The theme builds on \"%1\", which could not be found beside it.").arg(include);
            return false;
        }

        if (include.endsWith(QLatin1String(".tmTheme"), Qt::CaseInsensitive)) {
            if (!readTmTheme(base, theme, error)) {
                return false;
            }
        } else {
            QString parseError;
            const QJsonObject baseJson = parseRelaxedJson(base, &parseError);

            if (baseJson.isEmpty()) {
                error = QStringLiteral("The included theme \"%1\" could not be read. %2").arg(include, parseError);
                return false;
            }

            if (!readVsCodeTheme(baseJson, theme, error, resolveInclude, depth + 1)) {
                return false;
            }
        }
    }

    theme.format = QStringLiteral("vscode");

    if (json.contains(QStringLiteral("name"))) {
        theme.name = json.value(QStringLiteral("name")).toString();
    }

    const QString type = json.value(QStringLiteral("type")).toString().toLower();

    if (type == QLatin1String("light") || type == QLatin1String("vs")) {
        theme.type = QStringLiteral("light");
    } else if (type == QLatin1String("dark") || type == QLatin1String("vs-dark")) {
        theme.type = QStringLiteral("dark");
    } else if (type == QLatin1String("hc") || type == QLatin1String("hcdark") || type == QLatin1String("hc-black")) {
        theme.type = QStringLiteral("hc-dark");
    } else if (type == QLatin1String("hclight") || type == QLatin1String("hc-light")) {
        theme.type = QStringLiteral("hc-light");
    }

    const QJsonObject colors = json.value(QStringLiteral("colors")).toObject();

    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const Rgba color = parseHexColor(it.value().toString());

        if (color.isValid()) {
            theme.colors.insert(it.key(), color);
        } else if (it.value().isNull()) {
            theme.colors.remove(it.key());
        }
    }

    const QJsonValue tokenColors = json.value(QStringLiteral("tokenColors"));

    if (tokenColors.isString()) {
        // Older themes point at a .tmTheme file for their token colors.
        const QByteArray tmTheme = resolveInclude ? resolveInclude(tokenColors.toString()) : QByteArray();
        SourceTheme tokens;
        QString tokenError;

        if (tmTheme.isEmpty() || !readTmTheme(tmTheme, tokens, tokenError)) {
            error = QStringLiteral("The theme's token colors are in \"%1\", which could not be read.")
                        .arg(tokenColors.toString());
            return false;
        }

        theme.rules += tokens.rules;

        if (tokens.globalForeground.isValid()) {
            theme.globalForeground = tokens.globalForeground;
        }

        if (tokens.globalBackground.isValid()) {
            theme.globalBackground = tokens.globalBackground;
        }
    } else {
        readTextMateSettings(tokenColors.toArray(), theme);
    }

    return true;
}

bool readSublimeScheme(const QJsonObject &json, SourceTheme &theme, QString &error)
{
    const QVariantMap variables = json.value(QStringLiteral("variables")).toObject().toVariantMap();
    const QJsonObject globals = json.value(QStringLiteral("globals")).toObject();
    const QJsonArray rules = json.value(QStringLiteral("rules")).toArray();

    if (globals.isEmpty() && rules.isEmpty()) {
        error = QStringLiteral("The file is not a Sublime Text color scheme.");
        return false;
    }

    theme.format = QStringLiteral("sublime");
    theme.name = json.value(QStringLiteral("name")).toString();

    static const QList<QPair<const char *, const char *>> globalKeys = {
        {"background", "editor.background"},
        {"foreground", "editor.foreground"},
        {"caret", "editorCursor.foreground"},
        {"selection", "editor.selectionBackground"},
        {"inactive_selection", "editor.inactiveSelectionBackground"},
        {"line_highlight", "editor.lineHighlightBackground"},
        {"invisibles", "editorWhitespace.foreground"},
        {"gutter", "editorGutter.background"},
        {"gutter_foreground", "editorLineNumber.foreground"},
        {"gutter_foreground_highlight", "editorLineNumber.activeForeground"},
        {"find_highlight", "editor.findMatchHighlightBackground"},
        {"guide", "editorIndentGuide.background1"},
        {"active_guide", "editorIndentGuide.activeBackground1"},
        {"brackets_foreground", "editorBracketMatch.border"},
    };

    for (const auto &[key, colorKey] : globalKeys) {
        const Rgba color = parseCssColor(globals.value(QLatin1String(key)).toString(), variables);

        if (color.isValid()) {
            theme.colors.insert(QLatin1String(colorKey), color);
        }
    }

    theme.globalForeground = theme.colors.value(QStringLiteral("editor.foreground"));
    theme.globalBackground = theme.colors.value(QStringLiteral("editor.background"));

    for (const QJsonValue &value : rules) {
        const QJsonObject entry = value.toObject();
        const Rule rule = ruleFromSettings(entry.value(QStringLiteral("scope")), entry, variables);

        if (!rule.selectors.isEmpty()) {
            theme.rules.append(rule);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Conversion

QString nameFromFile(const QString &fileName)
{
    QString base = QFileInfo(fileName).fileName();

    for (const char *suffix : {".json", ".tmTheme", ".sublime-color-scheme", "-color-theme", ".color-theme"}) {
        if (base.endsWith(QLatin1String(suffix), Qt::CaseInsensitive)) {
            base.chop(int(qstrlen(suffix)));
        }
    }

    base.replace(QLatin1Char('-'), QLatin1Char(' '));
    base.replace(QLatin1Char('_'), QLatin1Char(' '));
    return base.trimmed();
}

QJsonObject styleJson(const Style &style, const Rgba &fallback, const Rgba &background)
{
    QJsonObject json;
    const Rgba color = style.matched ? flatten(style.foreground, background) : fallback;

    if (color.isValid()) {
        json.insert(QStringLiteral("foreground"), hex6(color));
    }

    json.insert(QStringLiteral("fontStyle"), style.hasFontStyle ? style.fontStyle : QString());
    return json;
}

QJsonObject convert(const SourceTheme &source, const QString &fileName)
{
    QString type = source.type;
    Rgba background = source.colors.value(QStringLiteral("editor.background"), source.globalBackground);

    if (type.isEmpty()) {
        type = background.isValid() && isLight(background) ? QStringLiteral("light") : QStringLiteral("dark");
    }

    const bool light = type.endsWith(QLatin1String("light"));

    if (!background.isValid()) {
        background = light ? Rgba::fromBytes(0xff, 0xff, 0xff) : Rgba::fromBytes(0x1e, 0x1e, 0x1e);
    }

    background = flatten(background, light ? Rgba::fromBytes(255, 255, 255) : Rgba::fromBytes(0, 0, 0));
    Rgba foreground = source.colors.value(QStringLiteral("editor.foreground"), source.globalForeground);

    if (!foreground.isValid()) {
        foreground = light ? Rgba::fromBytes(0x33, 0x33, 0x33) : Rgba::fromBytes(0xd4, 0xd4, 0xd4);
    }

    foreground = flatten(foreground, background);

    QJsonObject tokens;
    tokens.insert(QString(), QJsonObject{{QStringLiteral("foreground"), hex6(foreground)}});

    for (const Target &target : editorTargets()) {
        const Style style = resolveCandidates(source.rules, target.candidates);
        QJsonObject json = styleJson(style, foreground, background);
        const QLatin1String token(target.token);

        // Markdown emphasis keeps its meaning when the theme says nothing.
        if (!style.hasFontStyle && token == QLatin1String("emphasis")) {
            json.insert(QStringLiteral("fontStyle"), QStringLiteral("italic"));
        } else if (!style.hasFontStyle && token == QLatin1String("strong")) {
            json.insert(QStringLiteral("fontStyle"), QStringLiteral("bold"));
        }

        tokens.insert(token, json);
    }

    QJsonObject chat;

    for (const Target &target : chatTargets()) {
        const Style style = resolveCandidates(source.rules, target.candidates);
        const QLatin1String key(target.token);
        Rgba color = style.matched ? flatten(style.foreground, background) : Rgba();
        const bool diff = key == QLatin1String("added") || key == QLatin1String("removed");

        // A diff color that is just the text color says nothing.
        if (diff && color.isValid() && hex6(color) == hex6(foreground)) {
            color = Rgba();
        }

        if (!color.isValid() && key == QLatin1String("added")) {
            color = source.colors.value(QStringLiteral("gitDecoration.addedResourceForeground"),
                                        source.colors.value(QStringLiteral("terminal.ansiGreen")));
        } else if (!color.isValid() && key == QLatin1String("removed")) {
            color = source.colors.value(QStringLiteral("gitDecoration.deletedResourceForeground"),
                                        source.colors.value(QStringLiteral("terminal.ansiRed")));
        }

        // Kinds the theme does not color read as plain text, except diff
        // lines, which keep Archivist's green and red.
        if (!color.isValid() && key != QLatin1String("added") && key != QLatin1String("removed")) {
            color = foreground;
        }

        if (color.isValid()) {
            color = flatten(color, background);
            chat.insert(key, hex6(color));

            if (key == QLatin1String("added") || key == QLatin1String("removed")) {
                Rgba tint = color;
                tint.a = 0x22 / 255.0;
                chat.insert(key + QStringLiteral("Background"), hexArgb(tint));
            }
        }

        chat.insert(key + QStringLiteral("FontStyle"), style.hasFontStyle ? style.fontStyle : QString());
    }

    QJsonObject editorColors;

    for (auto it = source.colors.cbegin(); it != source.colors.cend(); ++it) {
        editorColors.insert(it.key(), hexRgba(it.value()));
    }

    editorColors.insert(QStringLiteral("editor.background"), hex6(background));
    editorColors.insert(QStringLiteral("editor.foreground"), hex6(foreground));

    const Rgba lineNumbers = flatten(source.colors.value(QStringLiteral("editorLineNumber.foreground")), background);
    const QString name = source.name.trimmed().isEmpty() ? nameFromFile(fileName) : source.name.trimmed();

    return QJsonObject{
        {QStringLiteral("format"), 1},
        {QStringLiteral("id"), themeId(name)},
        {QStringLiteral("name"), name},
        {QStringLiteral("type"), type},
        {QStringLiteral("sourceFormat"), source.format},
        {QStringLiteral("sourceFile"), QFileInfo(fileName).fileName()},
        {QStringLiteral("background"), hex6(background)},
        {QStringLiteral("foreground"), hex6(foreground)},
        {QStringLiteral("lineNumbers"), lineNumbers.isValid() ? hex6(lineNumbers) : QString()},
        {QStringLiteral("tokens"), tokens},
        {QStringLiteral("chatSyntax"), chat},
        {QStringLiteral("editorColors"), editorColors},
    };
}
} // namespace

QStringList editorTokens()
{
    QStringList tokens{QString()};

    for (const Target &target : editorTargets()) {
        tokens.append(QLatin1String(target.token));
    }

    return tokens;
}

QStringList chatSyntaxKeys()
{
    QStringList keys;

    for (const Target &target : chatTargets()) {
        keys.append(QLatin1String(target.token));
    }

    return keys;
}

QString themeId(const QString &name)
{
    QString id;
    bool dash = false;

    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber()) {
            id += c;
            dash = false;
        } else if (!id.isEmpty() && !dash) {
            id += QLatin1Char('-');
            dash = true;
        }
    }

    while (id.endsWith(QLatin1Char('-'))) {
        id.chop(1);
    }

    return id.isEmpty() ? QStringLiteral("theme") : id;
}

QJsonObject parseRelaxedJson(const QByteArray &contents, QString *error)
{
    // Drop comments and trailing commas outside strings, then parse as JSON.
    const QString text = QString::fromUtf8(contents);
    QString cleaned;
    cleaned.reserve(text.size());
    int index = 0;

    if (text.startsWith(QChar(0xfeff))) {
        index = 1;
    }

    while (index < text.size()) {
        const QChar c = text.at(index);

        if (c == QLatin1Char('"')) {
            const int start = index++;

            while (index < text.size() && text.at(index) != QLatin1Char('"')) {
                index += text.at(index) == QLatin1Char('\\') ? 2 : 1;
            }

            ++index;
            cleaned += QStringView(text).mid(start, std::min<qsizetype>(index, text.size()) - start);
            continue;
        }

        if (c == QLatin1Char('/') && index + 1 < text.size() && text.at(index + 1) == QLatin1Char('/')) {
            while (index < text.size() && text.at(index) != QLatin1Char('\n')) {
                ++index;
            }

            continue;
        }

        if (c == QLatin1Char('/') && index + 1 < text.size() && text.at(index + 1) == QLatin1Char('*')) {
            const int end = text.indexOf(QLatin1String("*/"), index + 2);
            index = end < 0 ? text.size() : end + 2;
            cleaned += QLatin1Char(' ');
            continue;
        }

        if (c == QLatin1Char(',')) {
            int next = index + 1;

            while (next < text.size()) {
                const QChar n = text.at(next);

                if (n.isSpace()) {
                    ++next;
                } else if (n == QLatin1Char('/') && next + 1 < text.size() && text.at(next + 1) == QLatin1Char('/')) {
                    while (next < text.size() && text.at(next) != QLatin1Char('\n')) {
                        ++next;
                    }
                } else if (n == QLatin1Char('/') && next + 1 < text.size() && text.at(next + 1) == QLatin1Char('*')) {
                    const int end = text.indexOf(QLatin1String("*/"), next + 2);
                    next = end < 0 ? text.size() : end + 2;
                } else {
                    break;
                }
            }

            if (next < text.size() && (text.at(next) == QLatin1Char('}') || text.at(next) == QLatin1Char(']'))) {
                ++index;
                continue;
            }
        }

        cleaned += c;
        ++index;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(cleaned.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.error != QJsonParseError::NoError
                ? QStringLiteral("The JSON could not be read: %1.").arg(parseError.errorString())
                : QStringLiteral("The JSON is not an object.");
        }

        return {};
    }

    return document.object();
}

ImportResult importTheme(const QByteArray &contents, const QString &fileName, const IncludeResolver &resolveInclude)
{
    ImportResult result;
    SourceTheme source;
    const QString lowerName = fileName.toLower();
    const bool looksLikePlist = contents.left(512).contains("<plist") || contents.trimmed().startsWith("<?xml");

    if (lowerName.endsWith(QLatin1String(".tmtheme")) || lowerName.endsWith(QLatin1String(".xml"))
        || (looksLikePlist && !lowerName.endsWith(QLatin1String(".json")))) {
        if (!readTmTheme(contents, source, result.error)) {
            return result;
        }
    } else {
        QString parseError;
        const QJsonObject json = parseRelaxedJson(contents, &parseError);

        if (json.isEmpty()) {
            result.error = parseError.isEmpty() ? QStringLiteral("The theme file is empty.") : parseError;
            return result;
        }

        const bool sublime = lowerName.endsWith(QLatin1String(".sublime-color-scheme"))
            || (json.contains(QStringLiteral("rules")) && json.contains(QStringLiteral("globals")));

        if (json.contains(QStringLiteral("contributes"))) {
            result.error = QStringLiteral(
                "This is an extension's package.json. Import one of the theme files it lists under "
                "contributes.themes instead.");
            return result;
        }

        const bool ok = sublime ? readSublimeScheme(json, source, result.error)
                                : readVsCodeTheme(json, source, result.error, resolveInclude, 0);

        if (!ok) {
            return result;
        }

        if (!sublime && source.rules.isEmpty() && source.colors.isEmpty()) {
            result.error = QStringLiteral("The file has no colors or tokenColors, so it is not a color theme.");
            return result;
        }
    }

    if (source.rules.isEmpty()) {
        result.error = QStringLiteral("The theme does not color any code.");
        return result;
    }

    result.theme = convert(source, fileName);
    return result;
}

ImportResult importThemeFile(const QString &filePath)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, QStringLiteral("%1 could not be opened.").arg(QFileInfo(filePath).fileName())};
    }

    const QDir directory = QFileInfo(filePath).absoluteDir();
    const QString root = directory.absolutePath();

    return importTheme(file.readAll(), filePath, [directory, root](const QString &relativePath) {
        const QString path = QDir::cleanPath(directory.absoluteFilePath(relativePath));
        QFile include(path);

        // Includes stay inside the theme's own folder tree.
        if (!path.startsWith(QFileInfo(root).absolutePath()) || !include.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }

        return include.readAll();
    });
}
}
