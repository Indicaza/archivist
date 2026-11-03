#pragma once

#include "knowledge_repository.h"

#include "Core/Database/core_database.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <optional>

// The Wiki's link to the original Library sources: checking citations when
// they are made (resolve) and keeping them true as the Library changes
// (reconcile).
//
// Only indexed Library text can be cited. A Wiki page or claim is never
// evidence, so a claim cannot prove itself and repeated summaries cannot
// pile up support.
namespace KnowledgeSources
{
// One indexed chunk of an available file, as it is now.
struct Passage
{
    QString chunkId;
    QString libraryId;
    QString fileId;
    QString relativePath;
    QString content;
    QString contentHash;
    QString fileContentHash;
    int startLine = 0;
    int endLine = 0;
};

[[nodiscard]] std::optional<Passage> livePassage(CoreDatabase &database, const QString &libraryId, const QString &chunkId);
// Every citable passage of one file, in order.
[[nodiscard]] QList<Passage> filePassages(CoreDatabase &database, const QString &libraryId, const QString &fileId);

struct Resolution
{
    bool ok = false;
    QString error; // why it is not a valid citation
    KnowledgeRepository::CitationDraft citation;
};

// Checks one citation: the chunk is live in this Library, (when
// `allowedChunkIds` is given) it is one of the passages the author was
// shown, and `quote` appears in it (whitespace-insensitive). The line range
// is narrowed to the quote.
[[nodiscard]] Resolution resolve(CoreDatabase &database, const QString &libraryId, const QString &chunkId, const QString &quote, const QSet<QString> &allowedChunkIds = {});

struct ReconcileReport
{
    int checked = 0;
    int unchanged = 0;
    int moved = 0;
    int reanchored = 0;
    int modified = 0;
    int deleted = 0;
    int unavailable = 0;
    int recovered = 0;     // citations that support their claim again
    QStringList staleClaimIds;     // claims that lost all support in this run
    QStringList recoveredClaimIds; // stale claims supported again
    QStringList proposalIds;       // reconciliation proposals made or kept

    [[nodiscard]] QVariantMap toVariant() const;
};

// Re-checks every citation of a Library against the current index:
//   unchanged chunk                         -> valid (path updated if the file moved in place)
//   same text, new chunk id in the same file -> reanchored (re-indexed or shifted lines)
//   quote still in the file, passage edited  -> reanchored to the passage that has it
//   same text in another file                -> moved (renamed or moved outside the app)
//   text gone, file still indexed            -> modified
//   file missing                             -> deleted
//   file present but not readable/indexed    -> unavailable
// Claims left without support become stale (never deleted); claims that
// regain support recover. Each affected page is flagged for review and gets
// one pending reconciliation proposal. Page text is never changed here, so
// manual edits are safe.
ReconcileReport reconcile(CoreDatabase &database, const QString &libraryId, const KnowledgeRepository::ActorRef &actor);
}
