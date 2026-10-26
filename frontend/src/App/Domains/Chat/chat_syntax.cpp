#include "chat_syntax.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace ChatSyntax
{
namespace
{
struct Definition
{
    QStringList lineComments;
    QString blockOpen;
    QString blockClose;
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> constants;
    bool preprocessor = false;   // C-family #include lines
    bool attributes = false;     // Rust #[...]
    bool decorators = false;     // @name
    bool backticks = false;      // `template` or raw strings
    bool tripleQuotes = false;   // Python """docstrings"""
    bool lifetimes = false;      // Rust 'a
    bool macros = false;         // Rust name!
    bool caseInsensitive = false;
    bool variables = false;      // shell $NAME
    bool capitalizedTypes = true;
    bool stringPrefixes = false; // r"..", b"..", f".."
    bool dollarIdentifiers = false;
};

QSet<QString> words(const char *list)
{
    QSet<QString> result;
    const auto parts = QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);

    for (const QString &part : parts) {
        result.insert(part);
    }

    return result;
}

const Definition &python()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("#")};
        d.keywords = words("and as assert async await break class continue def del elif else except finally for from "
                           "global if import in is lambda nonlocal not or pass raise return try while with yield match "
                           "case");
        d.constants = words("True False None self cls");
        d.types = words("int str float bool list dict set tuple bytes object type complex frozenset");
        d.decorators = true;
        d.tripleQuotes = true;
        d.stringPrefixes = true;
        return d;
    }();
    return definition;
}

const Definition &javascript()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("//")};
        d.blockOpen = QStringLiteral("/*");
        d.blockClose = QStringLiteral("*/");
        d.keywords = words("break case catch class const continue debugger default delete do else export extends "
                           "finally for from function if import in instanceof let new of return super switch this "
                           "throw try typeof var void while with yield async await static get set as interface type "
                           "enum implements private protected public readonly declare namespace abstract keyof infer "
                           "satisfies property signal required alias");
        d.constants = words("true false null undefined NaN Infinity");
        d.types = words("string number boolean any unknown never object bigint symbol");
        d.decorators = true;
        d.backticks = true;
        d.dollarIdentifiers = true;
        return d;
    }();
    return definition;
}

const Definition &cFamily()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("//")};
        d.blockOpen = QStringLiteral("/*");
        d.blockClose = QStringLiteral("*/");
        d.keywords = words(
            "alignas auto break case catch class const constexpr consteval constinit const_cast continue co_await "
            "co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern final for "
            "friend goto if inline mutable namespace new noexcept operator override private protected public "
            "reinterpret_cast return sizeof static static_assert static_cast struct switch template this "
            "thread_local throw try typedef typeid typename union using virtual volatile while abstract extends "
            "implements import package interface instanceof synchronized throws var val fun let func guard where is "
            "in out internal object companion data sealed when init deinit self super signals slots emit");
        d.types = words("bool char char8_t char16_t char32_t double float int long short signed unsigned void "
                        "wchar_t size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t "
                        "uint64_t qreal qint64 quint64 string byte boolean");
        d.constants = words("true false nullptr NULL null nil");
        d.preprocessor = true;
        d.decorators = true;
        return d;
    }();
    return definition;
}

const Definition &rust()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("//")};
        d.blockOpen = QStringLiteral("/*");
        d.blockClose = QStringLiteral("*/");
        d.keywords = words("as async await break const continue crate dyn else enum extern fn for if impl in let loop "
                           "match mod move mut pub ref return static struct super trait type unsafe use where while");
        d.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str");
        d.constants = words("true false self Self None Some Ok Err");
        d.attributes = true;
        d.lifetimes = true;
        d.macros = true;
        d.stringPrefixes = true;
        return d;
    }();
    return definition;
}

const Definition &go()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("//")};
        d.blockOpen = QStringLiteral("/*");
        d.blockClose = QStringLiteral("*/");
        d.keywords = words("break case chan const continue default defer else fallthrough for func go goto if import "
                           "interface map package range return select struct switch type var");
        d.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "
                        "uint uint8 uint16 uint32 uint64 uintptr any");
        d.constants = words("true false nil iota");
        d.backticks = true;
        return d;
    }();
    return definition;
}

const Definition &sql()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("--")};
        d.blockOpen = QStringLiteral("/*");
        d.blockClose = QStringLiteral("*/");
        d.keywords = words("select from where and or not insert into values update set delete create table index view "
                           "drop alter add column primary key foreign references join left right inner outer full "
                           "cross on as group by order having limit offset union all distinct case when then else end "
                           "is like in between exists returning with begin commit rollback transaction if default "
                           "constraint unique check cascade asc desc pragma trigger replace conflict do nothing using");
        d.types = words("int integer bigint smallint text varchar char boolean bool date time timestamp timestamptz real "
                        "double numeric decimal json jsonb uuid serial blob float");
        d.constants = words("null true false");
        d.caseInsensitive = true;
        d.capitalizedTypes = false;
        return d;
    }();
    return definition;
}

const Definition &shell()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("#")};
        d.keywords = words("if then else elif fi for while until do done case esac in function return local export "
                           "readonly declare unset shift break continue exit set source alias select time");
        d.variables = true;
        d.capitalizedTypes = false;
        return d;
    }();
    return definition;
}

const Definition &config()
{
    static const Definition definition = [] {
        Definition d;
        d.lineComments = {QStringLiteral("#")};
        d.constants = words("true false null yes no on off");
        d.capitalizedTypes = false;
        return d;
    }();
    return definition;
}

bool isIdentifierStart(QChar c, bool dollar)
{
    return c.isLetter() || c == QLatin1Char('_') || (dollar && c == QLatin1Char('$'));
}

bool isIdentifierPart(QChar c, bool dollar)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || (dollar && c == QLatin1Char('$'));
}

// SCREAMING_CASE names read as constants.
bool isConstantName(const QString &word)
{
    if (word.size() < 2) {
        return false;
    }

    bool letter = false;

    for (const QChar c : word) {
        if (c.isLower()) {
            return false;
        }

        letter = letter || c.isLetter();
    }

    return letter;
}

class Scanner
{
public:
    Scanner(const QString &text, QList<Span> &spans)
        : m_text(text)
        , m_spans(spans)
        , m_size(int(text.size()))
    {
    }

    void push(int start, int end, Kind kind)
    {
        if (end <= start) {
            return;
        }

        if (!m_spans.isEmpty()) {
            Span &last = m_spans.last();

            if (last.kind == kind && last.start + last.length == start) {
                last.length += end - start;
                return;
            }
        }

        m_spans.append({start, end - start, kind});
    }

    [[nodiscard]] int lineEnd(int from) const
    {
        const int end = int(m_text.indexOf(QLatin1Char('\n'), from));
        return end < 0 ? m_size : end;
    }

    [[nodiscard]] bool startsWith(int at, const QString &token) const
    {
        return !token.isEmpty() && QStringView(m_text).mid(at).startsWith(token);
    }

    // A quoted string from `at` (the opening quote) to its closing quote.
    [[nodiscard]] int stringEnd(int at, QChar quote, bool multiline) const
    {
        int index = at + 1;

        while (index < m_size) {
            const QChar c = m_text.at(index);

            if (c == QLatin1Char('\\')) {
                index += 2;
                continue;
            }

            if (c == quote) {
                return index + 1;
            }

            if (c == QLatin1Char('\n') && !multiline) {
                return index;
            }

            ++index;
        }

        return m_size;
    }

    [[nodiscard]] int numberEnd(int at) const
    {
        int index = at;
        bool exponent = false;

        while (index < m_size) {
            const QChar c = m_text.at(index);

            if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
                exponent = (c == QLatin1Char('e') || c == QLatin1Char('E'))
                    && !m_text.mid(at, 2).startsWith(QLatin1String("0x"), Qt::CaseInsensitive);
                ++index;
                continue;
            }

            if (exponent && (c == QLatin1Char('+') || c == QLatin1Char('-'))) {
                exponent = false;
                ++index;
                continue;
            }

            break;
        }

        // "1." followed by a method call ("1..toString") is rare; a trailing
        // dot that starts a range ("0..10") is not part of the number.
        while (index > at + 1 && m_text.at(index - 1) == QLatin1Char('.')) {
            --index;
        }

        return index;
    }

    [[nodiscard]] QChar nextNonSpace(int from) const
    {
        for (int index = from; index < m_size; ++index) {
            const QChar c = m_text.at(index);

            if (c != QLatin1Char(' ') && c != QLatin1Char('\t')) {
                return c;
            }
        }

        return {};
    }

    const QString &m_text;
    QList<Span> &m_spans;
    int m_size;
};

void scanCode(const QString &text, const Definition &d, QList<Span> &spans)
{
    Scanner s(text, spans);
    const int size = int(text.size());
    bool lineStart = true;
    int index = 0;

    while (index < size) {
        const QChar c = text.at(index);

        if (c == QLatin1Char('\n')) {
            lineStart = true;
            ++index;
            continue;
        }

        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            ++index;
            continue;
        }

        const bool atLineStart = lineStart;
        lineStart = false;

        if (d.preprocessor && atLineStart && c == QLatin1Char('#')) {
            const int end = s.lineEnd(index);
            s.push(index, end, Kind::Meta);
            index = end;
            continue;
        }

        if (d.attributes && c == QLatin1Char('#') && index + 1 < size
            && (text.at(index + 1) == QLatin1Char('[') || text.at(index + 1) == QLatin1Char('!'))) {
            const int close = int(text.indexOf(QLatin1Char(']'), index));
            const int end = close < 0 ? s.lineEnd(index) : std::min(close + 1, s.lineEnd(index));
            s.push(index, end, Kind::Meta);
            index = end;
            continue;
        }

        if (!d.blockOpen.isEmpty() && s.startsWith(index, d.blockOpen)) {
            const int close = int(text.indexOf(d.blockClose, index + d.blockOpen.size()));
            const int end = close < 0 ? size : close + int(d.blockClose.size());
            s.push(index, end, Kind::Comment);
            index = end;
            continue;
        }

        bool comment = false;

        for (const QString &token : d.lineComments) {
            if (s.startsWith(index, token)) {
                // Shell "#" only starts a comment at a word boundary ($# and
                // ${#x} are not comments).
                if (d.variables && index > 0 && !text.at(index - 1).isSpace()) {
                    continue;
                }

                const int end = s.lineEnd(index);
                s.push(index, end, Kind::Comment);
                index = end;
                comment = true;
                break;
            }
        }

        if (comment) {
            continue;
        }

        if (d.tripleQuotes && (s.startsWith(index, QStringLiteral("\"\"\"")) || s.startsWith(index, QStringLiteral("'''")))) {
            const QString quote = text.mid(index, 3);
            const int close = int(text.indexOf(quote, index + 3));
            const int end = close < 0 ? size : close + 3;
            s.push(index, end, Kind::String);
            index = end;
            continue;
        }

        if (c == QLatin1Char('"') || (c == QLatin1Char('`') && d.backticks)) {
            const int end = s.stringEnd(index, c, c == QLatin1Char('`'));
            s.push(index, end, Kind::String);
            index = end;
            continue;
        }

        if (c == QLatin1Char('\'')) {
            if (d.lifetimes) {
                // 'a' or '\n' is a char; 'a (no closing quote) is a lifetime.
                const bool escaped = index + 1 < size && text.at(index + 1) == QLatin1Char('\\');
                const int close = escaped ? int(text.indexOf(QLatin1Char('\''), index + 2)) : index + 2;

                if (close < size && close > index && text.at(close) == QLatin1Char('\'') && close - index <= 10) {
                    s.push(index, close + 1, Kind::String);
                    index = close + 1;
                    continue;
                }

                int end = index + 1;

                while (end < size && isIdentifierPart(text.at(end), false)) {
                    ++end;
                }

                s.push(index, end, Kind::Meta);
                index = end;
                continue;
            }

            const int end = s.stringEnd(index, c, false);
            s.push(index, end, Kind::String);
            index = end;
            continue;
        }

        if (c.isDigit() || (c == QLatin1Char('.') && index + 1 < size && text.at(index + 1).isDigit())) {
            if (index > 0 && isIdentifierPart(text.at(index - 1), d.dollarIdentifiers)) {
                ++index;
                continue;
            }

            const int end = s.numberEnd(index);
            s.push(index, end, Kind::Number);
            index = end;
            continue;
        }

        if (d.variables && c == QLatin1Char('$') && index + 1 < size) {
            int end = index + 1;

            if (text.at(end) == QLatin1Char('{')) {
                const int close = int(text.indexOf(QLatin1Char('}'), end));
                end = close < 0 ? s.lineEnd(index) : std::min(close + 1, s.lineEnd(index));
            } else if (text.at(end) == QLatin1Char('(')) {
                end = index + 1;
            } else {
                while (end < size && (isIdentifierPart(text.at(end), false) || (end == index + 1 && QStringLiteral("?#@*!$-").contains(text.at(end))))) {
                    ++end;
                }
            }

            if (end > index + 1) {
                s.push(index, end, Kind::Constant);
            }

            index = std::max(end, index + 1);
            continue;
        }

        if (d.decorators && c == QLatin1Char('@') && index + 1 < size && isIdentifierStart(text.at(index + 1), false)) {
            int end = index + 1;

            while (end < size && (isIdentifierPart(text.at(end), false) || text.at(end) == QLatin1Char('.'))) {
                ++end;
            }

            s.push(index, end, Kind::Meta);
            index = end;
            continue;
        }

        if (isIdentifierStart(c, d.dollarIdentifiers)) {
            int end = index + 1;

            while (end < size && isIdentifierPart(text.at(end), d.dollarIdentifiers)) {
                ++end;
            }

            // r"raw", b"bytes", f"{format}" and friends.
            if (d.stringPrefixes && end - index <= 2 && end < size
                && (text.at(end) == QLatin1Char('"') || text.at(end) == QLatin1Char('\''))) {
                const QString prefix = text.mid(index, end - index).toLower();
                static const QSet<QString> prefixes = words("r b f u rb br fr rf");

                if (prefixes.contains(prefix)) {
                    const int stringEnd = s.stringEnd(end, text.at(end), false);
                    s.push(index, stringEnd, Kind::String);
                    index = stringEnd;
                    continue;
                }
            }

            const QString word = text.mid(index, end - index);
            const QString key = d.caseInsensitive ? word.toLower() : word;
            const QChar next = s.nextNonSpace(end);
            Kind kind;
            bool highlighted = true;

            if (d.keywords.contains(key)) {
                kind = Kind::Keyword;
            } else if (d.constants.contains(key)) {
                kind = Kind::Constant;
            } else if (d.types.contains(key)) {
                kind = Kind::Type;
            } else if (d.macros && end < size && text.at(end) == QLatin1Char('!')) {
                kind = Kind::Function;
                ++end;
            } else if (next == QLatin1Char('(')) {
                kind = Kind::Function;
            } else if (d.capitalizedTypes && isConstantName(word)) {
                kind = Kind::Constant;
            } else if (d.capitalizedTypes && word.at(0).isUpper()) {
                kind = Kind::Type;
            } else {
                highlighted = false;
                kind = Kind::Keyword;
            }

            if (highlighted) {
                s.push(index, end, kind);
            }

            index = end;
            continue;
        }

        ++index;
    }
}

void scanJson(const QString &text, QList<Span> &spans)
{
    Scanner s(text, spans);
    const int size = int(text.size());
    int index = 0;

    while (index < size) {
        const QChar c = text.at(index);

        if (s.startsWith(index, QStringLiteral("//"))) {
            const int end = s.lineEnd(index);
            s.push(index, end, Kind::Comment);
            index = end;
            continue;
        }

        if (c == QLatin1Char('"')) {
            const int end = s.stringEnd(index, c, false);
            // A string followed by ':' is a key.
            s.push(index, end, s.nextNonSpace(end) == QLatin1Char(':') ? Kind::Type : Kind::String);
            index = end;
            continue;
        }

        if (c.isDigit() || (c == QLatin1Char('-') && index + 1 < size && text.at(index + 1).isDigit())) {
            const int end = s.numberEnd(index + 1);
            s.push(index, end, Kind::Number);
            index = end;
            continue;
        }

        if (c.isLetter()) {
            int end = index + 1;

            while (end < size && text.at(end).isLetter()) {
                ++end;
            }

            const QString word = text.mid(index, end - index);

            if (word == QLatin1String("true") || word == QLatin1String("false") || word == QLatin1String("null")) {
                s.push(index, end, Kind::Constant);
            }

            index = end;
            continue;
        }

        ++index;
    }
}

void scanConfig(const QString &text, QList<Span> &spans)
{
    // YAML / TOML / INI: keys at the start of a line, then values.
    QList<Span> valueSpans;
    scanCode(text, config(), valueSpans);
    Scanner s(text, spans);
    const int size = int(text.size());
    int lineStart = 0;
    int valueIndex = 0;

    while (lineStart <= size) {
        const int end = s.lineEnd(lineStart);
        int keyStart = lineStart;

        while (keyStart < end && (text.at(keyStart).isSpace() || text.at(keyStart) == QLatin1Char('-'))) {
            ++keyStart;
        }

        int keyEnd = keyStart;

        while (keyEnd < end && text.at(keyEnd) != QLatin1Char(':') && text.at(keyEnd) != QLatin1Char('=')
               && text.at(keyEnd) != QLatin1Char('#') && text.at(keyEnd) != QLatin1Char('"')) {
            ++keyEnd;
        }

        const bool isSection = keyStart < end && text.at(keyStart) == QLatin1Char('[');

        if (isSection) {
            s.push(keyStart, end, Kind::Keyword);
        } else if (keyEnd < end && keyEnd > keyStart && (text.at(keyEnd) == QLatin1Char(':') || text.at(keyEnd) == QLatin1Char('='))) {
            s.push(keyStart, keyEnd, Kind::Type);
        }

        // Values on the rest of the line come from the generic scan.
        const int valueFrom = isSection ? end : (keyEnd < end ? keyEnd : keyStart);

        while (valueIndex < valueSpans.size() && valueSpans.at(valueIndex).start < end) {
            const Span span = valueSpans.at(valueIndex);

            if (span.start >= valueFrom) {
                s.push(span.start, std::min(span.start + span.length, end), span.kind);
            }

            ++valueIndex;
        }

        lineStart = end + 1;
    }
}

void scanMarkup(const QString &text, QList<Span> &spans)
{
    Scanner s(text, spans);
    const int size = int(text.size());
    int index = 0;

    while (index < size) {
        if (s.startsWith(index, QStringLiteral("<!--"))) {
            const int close = int(text.indexOf(QStringLiteral("-->"), index + 4));
            const int end = close < 0 ? size : close + 3;
            s.push(index, end, Kind::Comment);
            index = end;
            continue;
        }

        if (text.at(index) != QLatin1Char('<')) {
            ++index;
            continue;
        }

        int cursor = index + 1;

        if (cursor < size && (text.at(cursor) == QLatin1Char('/') || text.at(cursor) == QLatin1Char('!') || text.at(cursor) == QLatin1Char('?'))) {
            ++cursor;
        }

        int nameEnd = cursor;

        while (nameEnd < size && (text.at(nameEnd).isLetterOrNumber() || QStringLiteral("-_:.").contains(text.at(nameEnd)))) {
            ++nameEnd;
        }

        if (nameEnd == cursor) {
            ++index;
            continue;
        }

        s.push(cursor, nameEnd, Kind::Keyword);
        index = nameEnd;

        while (index < size && text.at(index) != QLatin1Char('>')) {
            const QChar c = text.at(index);

            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                const int end = s.stringEnd(index, c, true);
                s.push(index, end, Kind::String);
                index = end;
                continue;
            }

            if (c.isLetter()) {
                int end = index + 1;

                while (end < size && (text.at(end).isLetterOrNumber() || QStringLiteral("-_:@.").contains(text.at(end)))) {
                    ++end;
                }

                s.push(index, end, Kind::Type);
                index = end;
                continue;
            }

            ++index;
        }
    }
}

void scanDiff(const QString &text, QList<Span> &spans)
{
    Scanner s(text, spans);
    const int size = int(text.size());
    int lineStart = 0;

    while (lineStart < size) {
        const int end = s.lineEnd(lineStart);
        const QStringView line = QStringView(text).mid(lineStart, end - lineStart);

        if (line.startsWith(u"+++") || line.startsWith(u"---") || line.startsWith(u"diff ") || line.startsWith(u"index ")) {
            s.push(lineStart, end, Kind::Meta);
        } else if (line.startsWith(u"@@")) {
            s.push(lineStart, end, Kind::Keyword);
        } else if (line.startsWith(u"+")) {
            s.push(lineStart, end, Kind::Added);
        } else if (line.startsWith(u"-")) {
            s.push(lineStart, end, Kind::Removed);
        }

        lineStart = end + 1;
    }
}
} // namespace

QString family(const QString &language)
{
    static const QHash<QString, QString> families = [] {
        QHash<QString, QString> map;
        const auto add = [&map](const char *names, const char *family) {
            for (const QString &name : words(names)) {
                map.insert(name, QString::fromLatin1(family));
            }
        };
        add("py python python3 py3 pyi ipython", "python");
        add("js javascript jsx mjs cjs ts typescript tsx mts cts node qml qmljs json5 es6", "javascript");
        add("c cpp c++ cc cxx h hpp hh hxx objc objective-c objcpp cs csharp c# java kotlin kt swift dart "
            "scala groovy glsl hlsl cuda arduino ino zig",
            "c");
        add("rs rust", "rust");
        add("go golang", "go");
        add("sql postgres postgresql psql mysql sqlite plsql tsql", "sql");
        add("sh bash zsh shell fish console shellscript ksh terminal ps1 powershell pwsh dockerfile docker make makefile",
            "shell");
        add("json jsonc jsonl geojson", "json");
        add("yaml yml toml ini conf cfg properties env dotenv", "config");
        add("diff patch udiff git-diff", "diff");
        add("html htm xml xhtml svg vue svelte plist xaml", "markup");
        return map;
    }();

    return families.value(language.trimmed().toLower());
}

QList<Span> tokenize(const QString &text, const QString &language)
{
    QList<Span> spans;
    const QString kind = family(language);

    if (kind.isEmpty() || text.isEmpty()) {
        return spans;
    }

    spans.reserve(text.size() / 6);

    if (kind == QLatin1String("python")) {
        scanCode(text, python(), spans);
    } else if (kind == QLatin1String("javascript")) {
        scanCode(text, javascript(), spans);
    } else if (kind == QLatin1String("c")) {
        scanCode(text, cFamily(), spans);
    } else if (kind == QLatin1String("rust")) {
        scanCode(text, rust(), spans);
    } else if (kind == QLatin1String("go")) {
        scanCode(text, go(), spans);
    } else if (kind == QLatin1String("sql")) {
        scanCode(text, sql(), spans);
    } else if (kind == QLatin1String("shell")) {
        scanCode(text, shell(), spans);
    } else if (kind == QLatin1String("json")) {
        scanJson(text, spans);
    } else if (kind == QLatin1String("config")) {
        scanConfig(text, spans);
    } else if (kind == QLatin1String("diff")) {
        scanDiff(text, spans);
    } else if (kind == QLatin1String("markup")) {
        scanMarkup(text, spans);
    }

    return spans;
}
} // namespace ChatSyntax
