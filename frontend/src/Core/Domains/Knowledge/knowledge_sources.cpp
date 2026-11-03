#include "knowledge_sources.h"

#include "knowledge_proposals.h"
#include "knowledge_text.h"
#include "knowledge_types.h"

#include "Core/Support/transactions.h"

#include <QHash>
#include <QSet>

using namespace Knowledge;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

// A chunk that can be cited: indexed text of an available file.
const QString passageSelect = QStringLiteral(
    "SELECT c.id AS chunkId, c.library_id AS libraryId, c.library_file_id AS fileId, f.relative_path AS relativePath, "
    "c.content, c.content_hash AS contentHash, d.content_hash AS fileContentHash, c.start_line AS startLine, c.end_line AS endLine "
    "FROM library_chunks c "
    "INNER JOIN library_files f ON f.id = c.library_file_id "
    "INNER JOIN library_documents d ON d.library_file_id = c.library_file_id "
    "WHERE d.status = 'indexed' AND f.status = 'available' "
);

KnowledgeSources::Passage passageFrom(const QVariantMap &row)
{
    KnowledgeSources::Passage passage;
    passage.chunkId = row.value(QStringLiteral("chunkId")).toString();
    passage.libraryId = row.value(QStringLiteral("libraryId")).toString();
    passage.fileId = row.value(QStringLiteral("fileId")).toString();
    passage.relativePath = row.value(QStringLiteral("relativePath")).toString();
    passage.content = row.value(QStringLiteral("content")).toString();
    passage.contentHash = row.value(QStringLiteral("contentHash")).toString();
    passage.fileContentHash = row.value(QStringLiteral("fileContentHash")).toString();
    passage.startLine = row.value(QStringLiteral("startLine")).toInt();
    passage.endLine = row.value(QStringLiteral("endLine")).toInt();
    return passage;
}

struct Anchor
{
    QString status;
    QString detail;
    std::optional<KnowledgeSources::Passage> passage; // where the citation points now
    bool rehash = false; // the passage around the quote changed: adopt the new chunk hash
};

Anchor locate(CoreDatabase &database, const QString &libraryId, const QVariantMap &citation)
{
    const QString chunkId = citation.value(QStringLiteral("chunkId")).toString();
    const QString chunkHash = citation.value(QStringLiteral("chunkHash")).toString();
    const QString fileId = citation.value(QStringLiteral("fileId")).toString();
    const QString path = citation.value(QStringLiteral("relativePath")).toString();
    const QString quote = citation.value(QStringLiteral("quote")).toString();

    // 1. The same chunk, unchanged.
    if (const auto same = KnowledgeSources::livePassage(database, libraryId, chunkId); same && same->contentHash == chunkHash) {
        if (same->relativePath != path) {
            return {CitationStatus::moved, QStringLiteral("Moved from %1.").arg(path), same};
        }

        return {CitationStatus::valid, {}, same};
    }

    // 2. The same text in the same file under a new chunk id (re-indexed,
    //    or lines shifted by an edit elsewhere in the file).
    const auto sameFile = database.prepare(passageSelect + QStringLiteral("AND c.library_id = ? AND c.library_file_id = ? ORDER BY c.ordinal")).bind(1, libraryId).bind(2, fileId).all();

    for (const QVariantMap &row : sameFile) {
        if (row.value(QStringLiteral("contentHash")).toString() == chunkHash) {
            const auto passage = passageFrom(row);
            return {passage.relativePath != path ? CitationStatus::moved : CitationStatus::reanchored,
                    QStringLiteral("Re-anchored after re-indexing."), passage};
        }
    }

    // 3. The quoted text survived an edit to the passage around it.
    for (const QVariantMap &row : sameFile) {
        if (KnowledgeText::locateQuote(row.value(QStringLiteral("content")).toString(), quote, 1).found) {
            Anchor anchor{CitationStatus::reanchored, QStringLiteral("The quoted text is still in the file; the passage around it changed."), passageFrom(row)};
            anchor.rehash = true;
            return anchor;
        }
    }

    // 4. The same text in another file of the Library (renamed or moved
    //    outside the app, which gives the file a new id).
    if (const auto elsewhere = database
                                   .prepare(passageSelect + QStringLiteral("AND c.library_id = ? AND c.content_hash = ? ORDER BY f.relative_path COLLATE NOCASE, c.ordinal LIMIT 1"))
                                   .bind(1, libraryId)
                                   .bind(2, chunkHash)
                                   .one()) {
        const auto passage = passageFrom(*elsewhere);
        return {CitationStatus::moved, QStringLiteral("Found in %1 (was %2).").arg(passage.relativePath, path), passage};
    }

    // 5. Gone. Why?
    const auto file = database
                          .prepare(QStringLiteral(
                              "SELECT f.status AS fileStatus, d.status AS documentStatus FROM library_files f "
                              "LEFT JOIN library_documents d ON d.library_file_id = f.id WHERE f.id = ? AND f.library_id = ?"
                          ))
                          .bind(1, fileId)
                          .bind(2, libraryId)
                          .one();

    if (!file || file->value(QStringLiteral("fileStatus")).toString() == QStringLiteral("missing")) {
        return {CitationStatus::deleted, QStringLiteral("%1 was deleted or moved out of the Library.").arg(path), std::nullopt};
    }

    if (file->value(QStringLiteral("fileStatus")).toString() != QStringLiteral("available")
        || file->value(QStringLiteral("documentStatus")).toString() != QStringLiteral("indexed")) {
        return {CitationStatus::unavailable, QStringLiteral("%1 cannot be read or is not indexed right now.").arg(path), std::nullopt};
    }

    return {CitationStatus::modified, QStringLiteral("%1 no longer contains the quoted text.").arg(path), std::nullopt};
}
}

namespace KnowledgeSources
{
std::optional<Passage> livePassage(CoreDatabase &database, const QString &libraryId, const QString &chunkId)
{
    if (chunkId.isEmpty()) {
        return std::nullopt;
    }

    const auto row = database.prepare(passageSelect + QStringLiteral("AND c.id = ? AND c.library_id = ?")).bind(1, chunkId).bind(2, libraryId).one();
    return row ? std::optional<Passage>(passageFrom(*row)) : std::nullopt;
}

QList<Passage> filePassages(CoreDatabase &database, const QString &libraryId, const QString &fileId)
{
    QList<Passage> passages;

    for (const QVariantMap &row : database.prepare(passageSelect + QStringLiteral("AND c.library_id = ? AND c.library_file_id = ? ORDER BY c.ordinal"))
                                      .bind(1, libraryId)
                                      .bind(2, fileId)
                                      .all()) {
        passages.append(passageFrom(row));
    }

    return passages;
}

Resolution resolve(CoreDatabase &database, const QString &libraryId, const QString &chunkId, const QString &quote, const QSet<QString> &allowedChunkIds)
{
    Resolution resolution;

    if (!allowedChunkIds.isEmpty() && !allowedChunkIds.contains(chunkId)) {
        resolution.error = QStringLiteral("Cites passage %1, which was not among the sources provided.").arg(chunkId);
        return resolution;
    }

    const auto passage = livePassage(database, libraryId, chunkId);

    if (!passage) {
        resolution.error = QStringLiteral("Cites passage %1, which is not indexed text in this Library.").arg(chunkId);
        return resolution;
    }

    const QString trimmedQuote = KnowledgeText::collapsedWhitespace(quote);

    if (trimmedQuote.size() < 4) {
        resolution.error = QStringLiteral("Cites %1 without quoting it.").arg(passage->relativePath);
        return resolution;
    }

    const auto match = KnowledgeText::locateQuote(passage->content, trimmedQuote, passage->startLine);

    if (!match.found) {
        resolution.error = QStringLiteral("The quoted text is not in %1:%2-%3.").arg(passage->relativePath).arg(passage->startLine).arg(passage->endLine);
        return resolution;
    }

    resolution.ok = true;
    resolution.citation.fileId = passage->fileId;
    resolution.citation.relativePath = passage->relativePath;
    resolution.citation.chunkId = passage->chunkId;
    resolution.citation.chunkHash = passage->contentHash;
    resolution.citation.quote = trimmedQuote;
    resolution.citation.startLine = match.startLine;
    resolution.citation.endLine = match.endLine;
    resolution.citation.fileContentHash = passage->fileContentHash;
    return resolution;
}

QVariantMap ReconcileReport::toVariant() const
{
    return {
        {QStringLiteral("checked"), checked},
        {QStringLiteral("unchanged"), unchanged},
        {QStringLiteral("moved"), moved},
        {QStringLiteral("reanchored"), reanchored},
        {QStringLiteral("modified"), modified},
        {QStringLiteral("deleted"), deleted},
        {QStringLiteral("unavailable"), unavailable},
        {QStringLiteral("recovered"), recovered},
        {QStringLiteral("staleClaimIds"), staleClaimIds},
        {QStringLiteral("recoveredClaimIds"), recoveredClaimIds},
        {QStringLiteral("proposalIds"), proposalIds},
    };
}

ReconcileReport reconcile(CoreDatabase &database, const QString &libraryId, const KnowledgeRepository::ActorRef &actor)
{
    return Transactions::join(database, [&]() {
        ReconcileReport report;
        const auto citations = database
                                   .prepare(QStringLiteral(
                                       "SELECT c.id, c.claim_id AS claimId, c.library_file_id AS fileId, c.relative_path AS relativePath, "
                                       "c.chunk_id AS chunkId, c.chunk_hash AS chunkHash, c.quote, c.status, k.page_id AS pageId "
                                       "FROM knowledge_citations c INNER JOIN knowledge_claims k ON k.id = c.claim_id "
                                       "INNER JOIN knowledge_pages p ON p.id = k.page_id "
                                       "WHERE c.library_id = ? AND p.status = 'active' ORDER BY c.id"
                                   ))
                                   .bind(1, libraryId)
                                   .all();
        QHash<QString, QString> claimPage;

        for (const QVariantMap &citation : citations) {
            ++report.checked;
            const QString citationId = citation.value(QStringLiteral("id")).toString();
            const QString before = citation.value(QStringLiteral("status")).toString();
            const Anchor anchor = locate(database, libraryId, citation);
            claimPage.insert(citation.value(QStringLiteral("claimId")).toString(), citation.value(QStringLiteral("pageId")).toString());

            const bool unchanged = anchor.status == CitationStatus::valid && anchor.passage
                && anchor.passage->chunkId == citation.value(QStringLiteral("chunkId")).toString();

            if (unchanged) {
                ++report.unchanged;

                // Keep an earlier "moved"/"reanchored" note; it is still true.
                if (CitationStatus::supports(before)) {
                    database.prepare(QStringLiteral("UPDATE knowledge_citations SET checked_at = %1 WHERE id = ?").arg(nowSql)).bind(1, citationId).run();
                    continue;
                }

                ++report.recovered;
            } else if (anchor.status == CitationStatus::moved) {
                ++report.moved;
            } else if (anchor.status == CitationStatus::reanchored) {
                ++report.reanchored;
            } else if (anchor.status == CitationStatus::modified) {
                ++report.modified;
            } else if (anchor.status == CitationStatus::deleted) {
                ++report.deleted;
            } else if (anchor.status == CitationStatus::unavailable) {
                ++report.unavailable;
            }

            if (!unchanged && CitationStatus::supports(anchor.status) && !CitationStatus::supports(before)) {
                ++report.recovered;
            }

            if (anchor.passage) {
                const auto match = KnowledgeText::locateQuote(anchor.passage->content, citation.value(QStringLiteral("quote")).toString(), anchor.passage->startLine);
                const int startLine = match.found ? match.startLine : anchor.passage->startLine;
                const int endLine = match.found ? match.endLine : anchor.passage->endLine;
                // The stable reference follows the text: new chunk, file and
                // lines. The original quote and claim are untouched.
                database
                    .prepare(QStringLiteral(
                                 "UPDATE OR IGNORE knowledge_citations SET status = ?, status_detail = ?, chunk_id = ?, library_file_id = ?, "
                                 "relative_path = ?, start_line = ?, end_line = ?, file_content_hash = ?, chunk_hash = ?, checked_at = %1 WHERE id = ?"
                    )
                                 .arg(nowSql))
                    .bind(1, anchor.status)
                    .bind(2, anchor.detail.isEmpty() ? QVariant() : QVariant(anchor.detail))
                    .bind(3, anchor.passage->chunkId)
                    .bind(4, anchor.passage->fileId)
                    .bind(5, anchor.passage->relativePath)
                    .bind(6, startLine)
                    .bind(7, endLine)
                    .bind(8, anchor.passage->fileContentHash)
                    .bind(9, anchor.rehash ? anchor.passage->contentHash : citation.value(QStringLiteral("chunkHash")).toString())
                    .bind(10, citationId)
                    .run();

                if (database.changes() == 0) {
                    // The claim already cites that passage: keep this record as it was, re-anchored in name only.
                    database.prepare(QStringLiteral("UPDATE knowledge_citations SET status = ?, status_detail = ?, checked_at = %1 WHERE id = ?").arg(nowSql))
                        .bind(1, anchor.status)
                        .bind(2, anchor.detail)
                        .bind(3, citationId)
                        .run();
                }
            } else {
                database.prepare(QStringLiteral("UPDATE knowledge_citations SET status = ?, status_detail = ?, checked_at = %1 WHERE id = ?").arg(nowSql))
                    .bind(1, anchor.status)
                    .bind(2, anchor.detail)
                    .bind(3, citationId)
                    .run();
            }
        }

        // Claims follow their citations.
        QHash<QString, QStringList> staleByPage;
        QSet<QString> touchedPages;

        for (auto it = claimPage.cbegin(); it != claimPage.cend(); ++it) {
            const auto claim = KnowledgeRepository::claim(database, it.key());
            const QString before = claim ? claim->value(QStringLiteral("verificationStatus")).toString() : QString();
            const QString after = KnowledgeRepository::refreshClaimSupport(database, it.key());
            touchedPages.insert(it.value());

            if (after == Verification::stale && before != Verification::stale) {
                report.staleClaimIds.append(it.key());
            } else if (before == Verification::stale && after != Verification::stale) {
                report.recoveredClaimIds.append(it.key());
            }

            if (after == Verification::stale) {
                staleByPage[it.value()].append(it.key());
            }
        }

        report.staleClaimIds.sort();
        report.recoveredClaimIds.sort();
        QStringList pages(touchedPages.cbegin(), touchedPages.cend());
        pages.sort();

        for (const QString &pageId : pages) {
            KnowledgeRepository::touchReconciled(database, pageId);
            QStringList stale = staleByPage.value(pageId);
            stale.sort();

            if (stale.isEmpty()) {
                // Everything is supported again: an open reconciliation is moot.
                database
                    .prepare(QStringLiteral(
                                 "UPDATE knowledge_proposals SET status = 'superseded', review_note = 'Its sources support the page again.', updated_at = %1 "
                                 "WHERE target_page_id = ? AND status = 'pending' AND proposal_type = 'reconcile'"
                    )
                                 .arg(nowSql))
                    .bind(1, pageId)
                    .run();
                continue;
            }

            const auto page = KnowledgeRepository::pageRow(database, pageId);
            KnowledgeRepository::setReviewStatus(database, pageId, ReviewStatus::needsReview);
            // Described from the citations' current state, not from what
            // changed in this pass, so the same situation always yields the
            // same proposal (and the same fingerprint).
            QStringList changes;
            QVariantList staleClaims;

            for (const QString &claimId : stale) {
                const auto claim = KnowledgeRepository::claim(database, claimId);

                for (const QVariant &value : KnowledgeRepository::citations(database, claimId)) {
                    const QVariantMap cited = value.toMap();
                    const QString detail = cited.value(QStringLiteral("statusDetail")).toString();
                    changes.append(QStringLiteral("%1: %2").arg(
                        cited.value(QStringLiteral("status")).toString(),
                        detail.isEmpty() ? cited.value(QStringLiteral("relativePath")).toString() : detail));
                }
                staleClaims.append(QVariantMap{
                    {QStringLiteral("id"), claimId},
                    {QStringLiteral("statement"), claim->value(QStringLiteral("statement"))},
                    {QStringLiteral("reason"), claim->value(QStringLiteral("statusReason"))},
                });
            }

            changes.sort();
            changes.removeDuplicates();
            KnowledgeProposals::ProposalDraft draft;
            draft.libraryId = libraryId;
            draft.proposalType = ProposalType::reconcile;
            draft.targetPageId = pageId;
            draft.baseRevision = page->value(QStringLiteral("revision")).toInt();
            draft.title = QStringLiteral("Review “%1”: %2 claim(s) lost their sources").arg(page->value(QStringLiteral("title")).toString()).arg(stale.size());
            draft.explanation = QStringLiteral(
                "Library files this page cites were changed, moved or deleted, and these claims are no longer supported by any original source. "
                "Approve to retract them (they stay in the page history), or reject to keep them marked stale. "
                "To rewrite the page from the current sources, refresh it instead."
            );
            draft.payload = {
                {QStringLiteral("staleClaimIds"), stale},
                {QStringLiteral("retractClaimIds"), stale},
                {QStringLiteral("staleClaims"), staleClaims},
                {QStringLiteral("citationChanges"), changes},
            };
            draft.diff = changes.join(QLatin1Char('\n'));
            draft.actor = actor;

            // The person already answered exactly this question: do not ask again.
            const bool answered = database
                                      .prepare(QStringLiteral(
                                          "SELECT 1 FROM knowledge_proposals WHERE library_id = ? AND fingerprint = ? AND status IN ('rejected', 'applied')"
                                      ))
                                      .bind(1, libraryId)
                                      .bind(2, KnowledgeProposals::fingerprint(draft))
                                      .scalar()
                                      .isValid();

            if (answered) {
                KnowledgeRepository::setReviewStatus(database, pageId, ReviewStatus::reviewed);
                continue;
            }

            report.proposalIds.append(KnowledgeProposals::submit(database, draft).proposalId);
        }

        return report;
    });
}
}
