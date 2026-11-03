#pragma once

#include <QString>

// Which embedding a vector is. Two vectors are comparable only when every
// field matches, so changing any of them (another model, other dimensions, a
// different way of preparing the input text) makes the index recompute.
namespace Retrieval
{
struct EmbeddingConfig
{
    QString model = QStringLiteral("text-embedding-3-small");
    int dimensions = 1536;
    // Bump when the text sent for a given content hash changes (truncation,
    // prefixes, normalization). Stored with every vector.
    int inputVersion = 1;
    // Inputs are cut to this many characters (about 6000 tokens), under the
    // model's 8191-token input limit. Chunks are far smaller in practice.
    int maxInputCharacters = 24000;

    [[nodiscard]] QString key() const
    {
        return QStringLiteral("%1/%2/v%3").arg(model).arg(dimensions).arg(inputVersion);
    }

    bool operator==(const EmbeddingConfig &other) const
    {
        return model == other.model && dimensions == other.dimensions && inputVersion == other.inputVersion;
    }
};

// The configuration new vectors are made with.
[[nodiscard]] inline EmbeddingConfig currentEmbeddingConfig()
{
    return EmbeddingConfig{};
}

// Source types and scopes. A source is anything with text that can be
// embedded; the scope decides who may retrieve it.
namespace SourceType
{
inline const QString libraryChunk = QStringLiteral("library-chunk");
// Reserved for the Knowledge Store; nothing produces them yet.
inline const QString wikiPage = QStringLiteral("wiki-page");
inline const QString memory = QStringLiteral("memory");
}

namespace ScopeType
{
inline const QString library = QStringLiteral("library");
}
}
