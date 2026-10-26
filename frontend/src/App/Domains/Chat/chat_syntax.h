#pragma once

#include <QList>
#include <QString>

// A small, fast tokenizer for the code blocks in chat replies.
//
// It is not a parser: it recognises comments, strings, numbers, keywords,
// types, calls and a few language-specific marks, which is what makes code
// readable at a glance. It runs once per code block when the block is laid
// out (the result is cached with the layout), so it has to be quick and must
// never fail on partial code while a reply streams in.
namespace ChatSyntax
{
enum class Kind {
    Keyword,
    String,
    Comment,
    Number,
    Type,
    Function,
    Constant,
    Meta,
    Added,
    Removed,
};

struct Span
{
    int start = 0;
    int length = 0;
    Kind kind = Kind::Keyword;
};

// The highlighter family for a fence language ("py", "tsx", "c++" ...), or
// an empty string when the language is not highlighted.
QString family(const QString &language);

// Highlighted ranges in `text`, in order and never overlapping.
QList<Span> tokenize(const QString &text, const QString &language);
}
