#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The shared vocabulary of Context Assembly v3 (see CONTEXT_ASSEMBLY.md).
//
// Everything that may reach a model is described by these plain types before
// any of it is rendered into provider messages. Nothing here touches the
// database, a provider or the UI: sources build candidates, the assembler
// budgets and selects them, and the runtime sends the result.
namespace Context
{
// What a piece of context is. The kind decides how the item is rendered and
// which budget line pays for it.
enum class Kind
{
    Instructions,        // system and Agent instructions
    ToolDefinitions,     // the JSON schemas of the tools offered this turn
    CurrentRequest,      // the user message being answered
    ConversationHistory, // earlier messages of this Chat, in order
    ConversationRecall,  // older messages found by search, quoted as reference
    Attachment,          // Library files the user attached to the Chat
    LibraryEvidence,     // excerpts retrieved from the Chat's Library
    WikiKnowledge,       // Living Wiki pages and claims (Core/Domains/Knowledge): AI-maintained interpretation, never an original source
    Memory,              // reserved: personal and relationship memories (not produced yet)
    ToolResult,          // output of a tool the model called
};

// How far the model may trust a piece of context. Only Authoritative items
// may carry instructions; everything a person did not write this turn, or that
// came out of a file or a tool, is reference data.
enum class Trust
{
    Authoritative, // Archivist and the Agent's own instructions
    UserIntent,    // what the user typed (the current request and their history)
    ModelOutput,   // earlier assistant replies
    Untrusted,     // files, retrieved excerpts, recalled messages, tool output
};

// Why a candidate did not reach the model. Stable strings; the Context
// Inspector and tests read them.
namespace Omission
{
inline const QString duplicate = QStringLiteral("duplicate");
inline const QString coveredByAttachment = QStringLiteral("covered-by-attachment");
inline const QString categoryBudget = QStringLiteral("category-budget");
inline const QString totalBudget = QStringLiteral("total-budget");
inline const QString itemLimit = QStringLiteral("item-limit");
inline const QString perFileLimit = QStringLiteral("per-file-limit");
inline const QString outOfScope = QStringLiteral("out-of-scope");
inline const QString olderThanGap = QStringLiteral("older-than-omitted");
inline const QString focusedHistory = QStringLiteral("focused-history");
inline const QString unavailable = QStringLiteral("unavailable");
inline const QString empty = QStringLiteral("empty");
}

inline size_t qHash(Kind kind, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(kind), seed);
}

[[nodiscard]] QString kindName(Kind kind);
[[nodiscard]] QString trustName(Trust trust);
// The trust a kind always has. Sources cannot raise it.
[[nodiscard]] Trust trustFor(Kind kind);

// Where a piece of evidence came from, precisely enough to cite it and to
// open it again. Stable across runs for the same underlying source.
struct EvidenceRef
{
    QString sourceType;    // "chat-message", "library-chunk", "library-file", "tool-call", ...
    QString sourceId;      // id within that source type (message id, chunk id, ...)
    QString libraryId;     // empty when the source is not in a Library
    QString libraryName;
    QString fileId;
    QString relativePath;
    QString chatId;
    int startLine = 0;
    int endLine = 0;
    QString producer;      // what found it: "chat-history", "library-file-fts", "attachment", ...
    QString createdAt;
    // Source-specific facts a citation shows, e.g. a Wiki claim's
    // verification status, confidence, freshness and original sources.
    QVariantMap attributes;

    [[nodiscard]] bool isEmpty() const { return sourceType.isEmpty(); }
    // "<sourceType>:<sourceId>": the identity used for de-duplication.
    [[nodiscard]] QString key() const;
    // "Library/path/to/file.md:12-30"; empty when there is no file.
    [[nodiscard]] QString label() const;
    [[nodiscard]] QVariantMap toVariant() const;
};

// One result from any retrieval system (lexical, semantic, Wiki, memory, chat
// recall). Retrievers only produce these; ranking against everything else and
// budgeting happen in the assembler.
struct SearchCandidate
{
    Kind kind = Kind::LibraryEvidence;
    QString content;
    double score = 0;    // retriever's own relevance, higher is better
    QString reason;      // why the retriever returned it
    QString originalRole; // for recalled messages: who wrote it
    EvidenceRef ref;
};

// A unit of context the assembler may include. `id` is stable and unique per
// request; `dedupeKey` identifies the underlying source so the same text
// found twice is sent once.
struct ContextItem
{
    QString id;
    Kind kind = Kind::ConversationHistory;
    Trust trust = Trust::Untrusted;
    QString role;       // provider role for history items ("user"/"assistant"); empty otherwise
    QString content;
    int priority = 0;   // higher is selected first
    bool required = false;
    double relevance = 0;
    int order = 0;      // position within its kind; lower is newer for history, better for evidence
    QString dedupeKey;
    QString coveredByKey; // omitted when an included item has this dedupeKey (chunk of an attached file)
    QString truncatedNote; // set when the source already shortened the content
    EvidenceRef ref;
};

// Which Libraries this request may draw from. Empty `libraryIds` with
// `restricted` false means no restriction. The identity engine will fill this
// in later; today it is the Chat's Library plus the Libraries of its
// attachments.
struct AccessScope
{
    bool restricted = false;
    QSet<QString> libraryIds;

    [[nodiscard]] bool allows(const EvidenceRef &ref) const;
};

// Per-kind policy: how much a kind may use, how it ranks and how many items
// it may contribute. A cap of -1 means "whatever the total budget allows".
struct KindPolicy
{
    int tokenCap = -1;
    int priority = 0;
    int maxItems = -1;
};

struct Policy
{
    int totalTokens = 32000;
    int responseTokenReserve = 4000;
    // Held back for tool output in later rounds when tools are offered.
    int toolResultTokens = 0;
    int maxExcerptsPerFile = 3;
    // The newest N history messages rank above evidence.
    int historyFloorMessages = 2;
    int historyFloorPriority = 85;
    QHash<Kind, KindPolicy> kinds;

    [[nodiscard]] KindPolicy forKind(Kind kind) const { return kinds.value(kind); }
};

// Everything the assembler needs for one turn. Built by the runtime from the
// sources; contains text and metadata only.
struct ContextRequest
{
    QString chatId;
    QString currentMessageId;
    QString instructions;       // final system text the provider receives
    QString toolDefinitions;    // serialized tool schemas, empty when none
    ContextItem currentRequest;
    QList<ContextItem> items;   // history and pre-built items
    QList<SearchCandidate> candidates; // retrieved material from any retriever
    // Items a source dropped before assembly (unreadable file, focused
    // history, ...), reported with their reason.
    QList<QPair<ContextItem, QString>> preOmitted;
    AccessScope scope;
    Policy policy;
};
}
