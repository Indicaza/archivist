#include "edit_proposals.h"

#include "Core/Domains/Libraries/library_edit_inspection.h"
#include "Core/Domains/Libraries/library_file_policy.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"
#include "ai_run_repository.h"

#include <QRegularExpression>
#include <QSet>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");
constexpr int maximumOperationsPerProposal = 64;
constexpr int maximumSummaryCharacters = 2000;

const QString proposalSelect = QStringLiteral(
    "SELECT id, run_id, chat_id, library_id, agent_id, provider, model, skill_id, summary, status, created_at, "
    "reviewed_at, completed_at, error_code, error_message FROM ai_edit_proposals "
);

const QString operationSelect = QStringLiteral(
    "SELECT id, proposal_id, ordinal, operation_type, status, source_path, destination_path, expected_hash, "
    "before_content, after_content, after_hash, created_at, completed_at, error_code, error_message FROM ai_edit_operations "
);

const QString operationSummarySelect = QStringLiteral(
    "SELECT id, proposal_id, ordinal, operation_type, status, source_path, destination_path, expected_hash, "
    "NULL AS before_content, NULL AS after_content, after_hash, created_at, completed_at, error_code, error_message "
    "FROM ai_edit_operations "
);

QVariantMap mapOperation(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("proposalId"), row.value(QStringLiteral("proposal_id"))},
        {QStringLiteral("ordinal"), row.value(QStringLiteral("ordinal"))},
        {QStringLiteral("type"), row.value(QStringLiteral("operation_type"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("sourcePath"), nullable(row.value(QStringLiteral("source_path")))},
        {QStringLiteral("destinationPath"), nullable(row.value(QStringLiteral("destination_path")))},
        {QStringLiteral("expectedHash"), nullable(row.value(QStringLiteral("expected_hash")))},
        {QStringLiteral("beforeContent"), nullable(row.value(QStringLiteral("before_content")))},
        {QStringLiteral("afterContent"), nullable(row.value(QStringLiteral("after_content")))},
        {QStringLiteral("afterHash"), nullable(row.value(QStringLiteral("after_hash")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("errorCode"), nullable(row.value(QStringLiteral("error_code")))},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
    };
}

QVariantList operations(CoreDatabase &database, const QString &proposalId, bool summaries = false)
{
    QVariantList result;
    const auto rows = database
        .prepare((summaries ? operationSummarySelect : operationSelect) + QStringLiteral("WHERE proposal_id = ? ORDER BY ordinal ASC"))
        .bind(1, proposalId)
        .all();

    for (const QVariantMap &row : rows) {
        result.append(mapOperation(row));
    }

    return result;
}

QVariantMap mapProposal(CoreDatabase &database, const QVariantMap &row, bool summaries = false)
{
    const QString id = row.value(QStringLiteral("id")).toString();
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("runId"), row.value(QStringLiteral("run_id"))},
        {QStringLiteral("chatId"), row.value(QStringLiteral("chat_id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("agentId"), row.value(QStringLiteral("agent_id"))},
        {QStringLiteral("provider"), row.value(QStringLiteral("provider"))},
        {QStringLiteral("model"), row.value(QStringLiteral("model"))},
        {QStringLiteral("skillId"), nullable(row.value(QStringLiteral("skill_id")))},
        {QStringLiteral("summary"), row.value(QStringLiteral("summary"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("operations"), operations(database, id, summaries)},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("reviewedAt"), nullable(row.value(QStringLiteral("reviewed_at")))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("errorCode"), nullable(row.value(QStringLiteral("error_code")))},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
    };
}

QVariantList operationsWithStatus(const QVariantMap &proposal, const QString &status)
{
    QVariantList result;

    for (const QVariant &operation : proposal.value(QStringLiteral("operations")).toList()) {
        if (operation.toMap().value(QStringLiteral("status")).toString() == status) {
            result.append(operation);
        }
    }

    return result;
}

void insertOperations(CoreDatabase &database, const QString &proposalId, int firstOrdinal, const QVariantList &drafts)
{
    int ordinal = firstOrdinal;

    for (const QVariant &value : drafts) {
        const QVariantMap draft = value.toMap();
        const auto field = [&](const char *key) {
            const QVariant item = draft.value(QLatin1String(key));
            return isNullish(item) ? QVariant() : item;
        };

        database
            .prepare(QStringLiteral(
                "INSERT INTO ai_edit_operations (id, proposal_id, ordinal, operation_type, status, source_path, "
                "destination_path, expected_hash, before_content, after_content) VALUES (?, ?, ?, ?, 'proposed', ?, ?, ?, ?, ?)"
            ))
            .bind(1, newId())
            .bind(2, proposalId)
            .bind(3, ordinal++)
            .bind(4, draft.value(QStringLiteral("type")))
            .bind(5, field("sourcePath"))
            .bind(6, field("destinationPath"))
            .bind(7, field("expectedHash"))
            .bind(8, field("beforeContent"))
            .bind(9, field("afterContent"))
            .run();
    }
}

void assertNoOtherExecuting(CoreDatabase &database, const QString &libraryId, const QString &proposalId)
{
    const auto row = database
        .prepare(QStringLiteral("SELECT id FROM ai_edit_proposals WHERE library_id = ? AND status = 'executing' AND id != ? LIMIT 1"))
        .bind(1, libraryId)
        .bind(2, proposalId)
        .one();

    if (row) {
        throw CoreError(
            QStringLiteral("This Library already has an AI edit transaction in progress."),
            {{QStringLiteral("proposalId"), row->value(QStringLiteral("id"))}}
        );
    }
}

QVariantMap requireProposed(CoreDatabase &database, const QString &proposalId)
{
    const QVariantMap proposal = AIEditProposals::requireProposal(database, proposalId);

    if (proposal.value(QStringLiteral("status")).toString() != QStringLiteral("proposed")) {
        throw CoreError(QStringLiteral("This AI edit proposal has already been reviewed."));
    }

    return proposal;
}

QVariantMap requireExecuting(CoreDatabase &database, const QString &proposalId, const QString &message)
{
    const QVariantMap proposal = AIEditProposals::requireProposal(database, proposalId);

    if (proposal.value(QStringLiteral("status")).toString() != QStringLiteral("executing")) {
        throw CoreError(message);
    }

    return proposal;
}

void markApprovedOperationsFailed(CoreDatabase &database, const QString &proposalId, const QString &errorCode, const QString &errorMessage)
{
    database
        .prepare(QStringLiteral(
            "UPDATE ai_edit_operations SET status = 'failed', completed_at = %1, error_code = ?, error_message = ? "
            "WHERE proposal_id = ? AND status = 'approved'"
        ).arg(nowSql))
        .bind(1, errorCode)
        .bind(2, errorMessage)
        .bind(3, proposalId)
        .run();
}

// ---- proposeAIEdit validation ----

QString nullableText(const QVariantMap &draft, const char *key, bool *present)
{
    const QVariant value = draft.value(QLatin1String(key));
    *present = !isNullish(value);
    return *present ? value.toString() : QString();
}

QString normalizeEditPath(const QString &value, const QString &label)
{
    return LibraryEditInspection::normalizeRelativePath(value, QStringLiteral("%1 must be a safe Library-relative path.").arg(label));
}

QString posixDirname(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QStringLiteral(".") : path.left(slash);
}

QString posixBasename(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

void checkContentSize(const QString &content, const QString &label)
{
    const qint64 sizeBytes = content.toUtf8().size();

    if (sizeBytes > LibraryFilePolicy::maxTextPreviewBytes) {
        throw CoreError(
            QStringLiteral("%1 exceeds the current safe text-edit limit.").arg(label),
            {{QStringLiteral("sizeBytes"), sizeBytes}, {QStringLiteral("maximumBytes"), LibraryFilePolicy::maxTextPreviewBytes}}
        );
    }
}

QVariantMap normalizeOperation(const QVariantMap &draft)
{
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"), QRegularExpression::CaseInsensitiveOption);
    const QString type = draft.value(QStringLiteral("type")).toString();
    bool hasHash = false;
    bool hasBefore = false;
    bool hasAfter = false;
    bool hasSource = false;
    bool hasDestination = false;
    QString expectedHash = nullableText(draft, "expectedHash", &hasHash);
    const QString beforeContent = nullableText(draft, "beforeContent", &hasBefore);
    const QString afterContent = nullableText(draft, "afterContent", &hasAfter);
    const QString sourcePath = nullableText(draft, "sourcePath", &hasSource);
    const QString destinationPath = nullableText(draft, "destinationPath", &hasDestination);

    if (hasHash) {
        expectedHash = JsCompat::trimmed(expectedHash).toLower();

        if (!sha256.match(expectedHash).hasMatch()) {
            throw CoreError(QStringLiteral("Expected file hashes must be SHA-256 hex values."));
        }
    }

    if (hasBefore) {
        checkContentSize(beforeContent, QStringLiteral("Before content"));
    }

    if (hasAfter) {
        checkContentSize(afterContent, QStringLiteral("After content"));
    }

    const auto rejectUnexpected = [&](const QString &field, bool present) {
        if (present) {
            throw CoreError(QStringLiteral("%1 does not accept %2.").arg(type, field));
        }
    };

    if (type == QStringLiteral("create_file")) {
        rejectUnexpected(QStringLiteral("sourcePath"), hasSource);
        rejectUnexpected(QStringLiteral("expectedHash"), hasHash);
        rejectUnexpected(QStringLiteral("beforeContent"), hasBefore);

        if (!hasAfter) {
            throw CoreError(QStringLiteral("create_file requires afterContent."));
        }

        return {
            {QStringLiteral("type"), type},
            {QStringLiteral("destinationPath"), normalizeEditPath(destinationPath, QStringLiteral("Destination path"))},
            {QStringLiteral("afterContent"), afterContent},
        };
    }

    if (type == QStringLiteral("create_directory")) {
        rejectUnexpected(QStringLiteral("sourcePath"), hasSource);
        rejectUnexpected(QStringLiteral("expectedHash"), hasHash);
        rejectUnexpected(QStringLiteral("beforeContent"), hasBefore);
        rejectUnexpected(QStringLiteral("afterContent"), hasAfter);

        return {
            {QStringLiteral("type"), type},
            {QStringLiteral("destinationPath"), normalizeEditPath(destinationPath, QStringLiteral("Destination path"))},
        };
    }

    if (type == QStringLiteral("patch_file")) {
        rejectUnexpected(QStringLiteral("destinationPath"), hasDestination);

        if (!hasHash) {
            throw CoreError(QStringLiteral("patch_file requires an expectedHash."));
        }

        if (!hasBefore || !hasAfter) {
            throw CoreError(QStringLiteral("patch_file requires beforeContent and afterContent."));
        }

        const QString source = normalizeEditPath(sourcePath, QStringLiteral("Source path"));
        return {
            {QStringLiteral("type"), type},
            {QStringLiteral("sourcePath"), source},
            {QStringLiteral("destinationPath"), source},
            {QStringLiteral("expectedHash"), expectedHash},
            {QStringLiteral("beforeContent"), beforeContent},
            {QStringLiteral("afterContent"), afterContent},
        };
    }

    if (type == QStringLiteral("rename_file") || type == QStringLiteral("move_file")) {
        rejectUnexpected(QStringLiteral("beforeContent"), hasBefore);
        rejectUnexpected(QStringLiteral("afterContent"), hasAfter);

        if (!hasHash) {
            throw CoreError(QStringLiteral("%1 requires an expectedHash.").arg(type));
        }

        const QString source = normalizeEditPath(sourcePath, QStringLiteral("Source path"));
        const QString destination = normalizeEditPath(destinationPath, QStringLiteral("Destination path"));

        if (source == destination) {
            throw CoreError(QStringLiteral("%1 must change the file path.").arg(type));
        }

        if (type == QStringLiteral("rename_file") && posixDirname(source) != posixDirname(destination)) {
            throw CoreError(QStringLiteral("rename_file must keep the file in its current directory."));
        }

        if (type == QStringLiteral("move_file") && posixBasename(source) != posixBasename(destination)) {
            throw CoreError(QStringLiteral("move_file must preserve the file name."));
        }

        return {
            {QStringLiteral("type"), type},
            {QStringLiteral("sourcePath"), source},
            {QStringLiteral("destinationPath"), destination},
            {QStringLiteral("expectedHash"), expectedHash},
        };
    }

    throw CoreError(QStringLiteral("Unsupported AI edit operation."));
}

QStringList operationPaths(const QVariantMap &operation)
{
    QStringList paths;

    for (const char *key : {"sourcePath", "destinationPath"}) {
        const QVariant value = operation.value(QLatin1String(key));

        if (value.typeId() == QMetaType::QString && !paths.contains(value.toString())) {
            paths.append(value.toString());
        }
    }

    return paths;
}

void assertNoOperationConflicts(const QVariantList &existing, const QVariantList &incoming)
{
    QHash<QString, QString> occupied;

    for (const QVariant &value : existing) {
        const QVariantMap operation = value.toMap();

        for (const QString &path : operationPaths(operation)) {
            occupied.insert(path, operation.value(QStringLiteral("type")).toString());
        }
    }

    for (const QVariant &value : incoming) {
        const QVariantMap operation = value.toMap();
        const QString type = operation.value(QStringLiteral("type")).toString();

        for (const QString &path : operationPaths(operation)) {
            if (occupied.contains(path)) {
                throw CoreError(
                    QStringLiteral("The active edit proposal already contains an operation affecting \"%1\".").arg(path),
                    {
                        {QStringLiteral("affectedPath"), path},
                        {QStringLiteral("existingOperationType"), occupied.value(path)},
                        {QStringLiteral("incomingOperationType"), type},
                    }
                );
            }
        }

        for (const QString &path : operationPaths(operation)) {
            occupied.insert(path, type);
        }
    }
}
}

namespace AIEditProposals
{
std::optional<QVariantMap> proposalById(CoreDatabase &database, const QString &proposalId)
{
    const auto row = database.prepare(proposalSelect + QStringLiteral("WHERE id = ?")).bind(1, proposalId).one();
    return row ? std::optional<QVariantMap>(mapProposal(database, *row)) : std::nullopt;
}

QVariantMap requireProposal(CoreDatabase &database, const QString &proposalId)
{
    const auto proposal = proposalById(database, proposalId);

    if (!proposal) {
        throw CoreError(QStringLiteral("AI edit proposal not found."));
    }

    return *proposal;
}

QVariantList proposalsByRun(CoreDatabase &database, const QString &runId)
{
    QVariantList result;

    for (const QVariantMap &row :
         database.prepare(proposalSelect + QStringLiteral("WHERE run_id = ? ORDER BY created_at ASC, rowid ASC")).bind(1, runId).all()) {
        result.append(mapProposal(database, row));
    }

    return result;
}

QVariantList proposalsByChat(CoreDatabase &database, const QString &chatId)
{
    QVariantList result;

    for (const QVariantMap &row :
         database.prepare(proposalSelect + QStringLiteral("WHERE chat_id = ? ORDER BY created_at ASC, rowid ASC")).bind(1, chatId).all()) {
        result.append(mapProposal(database, row, true));
    }

    return result;
}

std::optional<QVariantMap> latestCompletedByLibrary(CoreDatabase &database, const QString &libraryId)
{
    const auto row = database
        .prepare(proposalSelect + QStringLiteral("WHERE library_id = ? AND status = 'completed' ORDER BY completed_at DESC, rowid DESC LIMIT 1"))
        .bind(1, libraryId)
        .one();
    return row ? std::optional<QVariantMap>(mapProposal(database, *row)) : std::nullopt;
}

QVariantMap propose(CoreDatabase &database, const QString &runId, const QString &rawSummary, const QVariantList &drafts, const QString &rawSkillId)
{
    const QVariantMap run = AIRunRepository::requireRun(database, runId);

    if (run.value(QStringLiteral("status")).toString() != QStringLiteral("running")) {
        throw CoreError(QStringLiteral("New edit proposals can only be created by an active AI Run."));
    }

    const QVariant libraryId = run.value(QStringLiteral("libraryId"));

    if (isNullish(libraryId) || libraryId.toString().isEmpty()) {
        throw CoreError(QStringLiteral("This AI Run is not attached to a Library."));
    }

    const QString summary = JsCompat::trimmed(rawSummary);

    if (summary.isEmpty() || summary.size() > maximumSummaryCharacters) {
        throw CoreError(QStringLiteral("Edit proposal summaries must contain 1-%1 characters.").arg(maximumSummaryCharacters));
    }

    if (drafts.isEmpty() || drafts.size() > maximumOperationsPerProposal) {
        throw CoreError(QStringLiteral("Edit proposals must contain 1-%1 operations.").arg(maximumOperationsPerProposal));
    }

    QVariantList normalized;

    for (const QVariant &draft : drafts) {
        normalized.append(normalizeOperation(draft.toMap()));
    }

    const QString skillId = JsCompat::trimmed(rawSkillId);
    QVariantList open;

    for (const QVariant &proposal : proposalsByRun(database, runId)) {
        if (proposal.toMap().value(QStringLiteral("status")).toString() == QStringLiteral("proposed")) {
            open.append(proposal);
        }
    }

    if (open.size() > 1) {
        throw CoreError(QStringLiteral("This AI Run has multiple active edit proposals and cannot safely add another operation."));
    }

    if (open.isEmpty()) {
        assertNoOperationConflicts({}, normalized);

        return Transactions::join(database, [&]() {
            const QString proposalId = newId();
            database
                .prepare(QStringLiteral(
                    "INSERT INTO ai_edit_proposals (id, run_id, chat_id, library_id, agent_id, provider, model, skill_id, "
                    "summary, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'proposed')"
                ))
                .bind(1, proposalId)
                .bind(2, runId)
                .bind(3, run.value(QStringLiteral("chatId")))
                .bind(4, libraryId)
                .bind(5, run.value(QStringLiteral("agentId")))
                .bind(6, run.value(QStringLiteral("provider")))
                .bind(7, run.value(QStringLiteral("model")))
                .bind(8, skillId.isEmpty() ? QVariant() : QVariant(skillId))
                .bind(9, summary)
                .run();
            insertOperations(database, proposalId, 1, normalized);
            return requireProposal(database, proposalId);
        });
    }

    const QVariantMap active = open.first().toMap();
    const QVariant activeSkill = active.value(QStringLiteral("skillId"));

    if ((isNullish(activeSkill) ? QString() : activeSkill.toString()) != skillId) {
        throw CoreError(QStringLiteral("The active edit proposal was created under different Skill provenance."));
    }

    const QVariantList existingOperations = active.value(QStringLiteral("operations")).toList();

    if (existingOperations.size() + normalized.size() > maximumOperationsPerProposal) {
        throw CoreError(QStringLiteral("Edit proposals must contain at most %1 operations.").arg(maximumOperationsPerProposal));
    }

    assertNoOperationConflicts(existingOperations, normalized);
    const QString activeSummary = active.value(QStringLiteral("summary")).toString();
    const QString mergedSummary = activeSummary == summary ? activeSummary : QStringLiteral("Multiple proposed Library changes");
    const QString proposalId = active.value(QStringLiteral("id")).toString();

    return Transactions::join(database, [&]() {
        const QVariantMap proposal = requireProposal(database, proposalId);

        if (proposal.value(QStringLiteral("status")).toString() != QStringLiteral("proposed")) {
            throw CoreError(QStringLiteral("AI edit proposal %1 is no longer open for changes.").arg(proposalId));
        }

        int highest = 0;

        for (const QVariant &operation : proposal.value(QStringLiteral("operations")).toList()) {
            highest = std::max(highest, operation.toMap().value(QStringLiteral("ordinal")).toInt());
        }

        insertOperations(database, proposalId, highest + 1, normalized);
        database
            .prepare(QStringLiteral("UPDATE ai_edit_proposals SET summary = ? WHERE id = ? AND status = 'proposed'"))
            .bind(1, mergedSummary)
            .bind(2, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap beginExecution(CoreDatabase &database, const QString &proposalId, const std::optional<QStringList> &selectedOperationIds)
{
    return Transactions::join(database, [&]() {
        const QVariantMap proposal = requireProposed(database, proposalId);
        assertNoOtherExecuting(database, proposal.value(QStringLiteral("libraryId")).toString(), proposalId);
        QStringList allIds;

        for (const QVariant &operation : proposal.value(QStringLiteral("operations")).toList()) {
            allIds.append(operation.toMap().value(QStringLiteral("id")).toString());
        }

        const QStringList requested = selectedOperationIds.value_or(allIds);
        const QSet<QString> unique(requested.cbegin(), requested.cend());

        if (requested.isEmpty()) {
            throw CoreError(QStringLiteral("At least one edit operation must be approved."));
        }

        if (unique.size() != requested.size()) {
            throw CoreError(QStringLiteral("Approved edit operation IDs must be unique."));
        }

        for (const QString &id : unique) {
            if (!allIds.contains(id)) {
                throw CoreError(QStringLiteral("Approved edit operations must belong to this proposal."));
            }
        }

        for (const QString &id : allIds) {
            database
                .prepare(QStringLiteral(
                    "UPDATE ai_edit_operations SET status = ?, error_code = NULL, error_message = NULL "
                    "WHERE id = ? AND proposal_id = ? AND status = 'proposed'"
                ))
                .bind(1, unique.contains(id) ? QStringLiteral("approved") : QStringLiteral("rejected"))
                .bind(2, id)
                .bind(3, proposalId)
                .run();
        }

        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'executing', reviewed_at = %1, error_code = NULL, error_message = NULL "
                "WHERE id = ? AND status = 'proposed'"
            ).arg(nowSql))
            .bind(1, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap reject(CoreDatabase &database, const QString &proposalId)
{
    return Transactions::join(database, [&]() {
        requireProposed(database, proposalId);
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_operations SET status = 'rejected', error_code = NULL, error_message = NULL "
                "WHERE proposal_id = ? AND status = 'proposed'"
            ))
            .bind(1, proposalId)
            .run();
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'rejected', reviewed_at = %1, completed_at = %1, error_code = NULL, "
                "error_message = NULL WHERE id = ? AND status = 'proposed'"
            ).arg(nowSql))
            .bind(1, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap markStale(CoreDatabase &database, const QString &proposalId, const QString &errorMessage)
{
    return Transactions::join(database, [&]() {
        requireExecuting(database, proposalId, QStringLiteral("Only executing AI edit proposals can become stale."));
        markApprovedOperationsFailed(database, proposalId, QStringLiteral("proposal_stale"), errorMessage);
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'stale', completed_at = %1, error_code = 'proposal_stale', "
                "error_message = ? WHERE id = ? AND status = 'executing'"
            ).arg(nowSql))
            .bind(1, errorMessage)
            .bind(2, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap markFailed(CoreDatabase &database, const QString &proposalId, const QString &errorCode, const QString &errorMessage)
{
    return Transactions::join(database, [&]() {
        requireExecuting(database, proposalId, QStringLiteral("Only executing AI edit proposals can fail execution."));
        markApprovedOperationsFailed(database, proposalId, errorCode, errorMessage);
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'failed', completed_at = %1, error_code = ?, error_message = ? "
                "WHERE id = ? AND status = 'executing'"
            ).arg(nowSql))
            .bind(1, errorCode)
            .bind(2, errorMessage)
            .bind(3, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap completeExecution(CoreDatabase &database, const QString &proposalId, const QHash<QString, QVariant> &afterHashes)
{
    return Transactions::join(database, [&]() {
        const QVariantMap proposal = requireExecuting(database, proposalId, QStringLiteral("Only executing AI edit proposals can complete."));
        const QVariantList approved = operationsWithStatus(proposal, QStringLiteral("approved"));

        if (approved.isEmpty()) {
            throw CoreError(QStringLiteral("This AI edit proposal has no approved operations to complete."));
        }

        for (const QVariant &value : approved) {
            const QString operationId = value.toMap().value(QStringLiteral("id")).toString();

            if (!afterHashes.contains(operationId)) {
                throw CoreError(QStringLiteral("Missing final hash record for AI edit operation %1.").arg(operationId));
            }

            const QVariant hash = afterHashes.value(operationId);
            database
                .prepare(QStringLiteral(
                    "UPDATE ai_edit_operations SET status = 'completed', after_hash = ?, completed_at = %1, error_code = NULL, "
                    "error_message = NULL WHERE id = ? AND proposal_id = ? AND status = 'approved'"
                ).arg(nowSql))
                .bind(1, isNullish(hash) ? QVariant() : hash)
                .bind(2, operationId)
                .bind(3, proposalId)
                .run();
        }

        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'completed', completed_at = %1, error_code = NULL, error_message = NULL "
                "WHERE id = ? AND status = 'executing'"
            ).arg(nowSql))
            .bind(1, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap beginUndo(CoreDatabase &database, const QString &proposalId)
{
    return Transactions::join(database, [&]() {
        const QVariantMap proposal = requireProposal(database, proposalId);

        if (proposal.value(QStringLiteral("status")).toString() != QStringLiteral("completed")) {
            throw CoreError(QStringLiteral("Only completed AI edit transactions can be undone."));
        }

        const QString libraryId = proposal.value(QStringLiteral("libraryId")).toString();
        assertNoOtherExecuting(database, libraryId, proposalId);
        const auto latest = latestCompletedByLibrary(database, libraryId);

        if (!latest || latest->value(QStringLiteral("id")).toString() != proposalId) {
            throw CoreError(QStringLiteral("Only the latest completed AI edit transaction can be undone."));
        }

        if (operationsWithStatus(proposal, QStringLiteral("completed")).isEmpty()) {
            throw CoreError(QStringLiteral("This AI edit transaction has no completed operations to undo."));
        }

        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'executing', error_code = NULL, error_message = NULL "
                "WHERE id = ? AND status = 'completed'"
            ))
            .bind(1, proposalId)
            .run();

        if (database.changes() != 1) {
            throw CoreError(QStringLiteral("This AI edit transaction is no longer available for undo."));
        }

        return requireProposal(database, proposalId);
    });
}

QVariantMap restoreAfterUndoFailure(CoreDatabase &database, const QString &proposalId)
{
    return Transactions::join(database, [&]() {
        requireExecuting(database, proposalId, QStringLiteral("Only an executing AI edit undo can be restored."));
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'completed', error_code = NULL, error_message = NULL "
                "WHERE id = ? AND status = 'executing'"
            ))
            .bind(1, proposalId)
            .run();

        if (database.changes() != 1) {
            throw CoreError(QStringLiteral("The AI edit undo state could not be restored."));
        }

        return requireProposal(database, proposalId);
    });
}

QVariantMap markUndoFailed(CoreDatabase &database, const QString &proposalId, const QString &errorCode, const QString &errorMessage)
{
    return Transactions::join(database, [&]() {
        requireExecuting(database, proposalId, QStringLiteral("Only an executing AI edit undo can fail."));
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_operations SET status = 'failed', error_code = ?, error_message = ? "
                "WHERE proposal_id = ? AND status = 'completed'"
            ))
            .bind(1, errorCode)
            .bind(2, errorMessage)
            .bind(3, proposalId)
            .run();
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'failed', error_code = ?, error_message = ? WHERE id = ? AND status = 'executing'"
            ))
            .bind(1, errorCode)
            .bind(2, errorMessage)
            .bind(3, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QVariantMap completeUndo(CoreDatabase &database, const QString &proposalId)
{
    return Transactions::join(database, [&]() {
        const QVariantMap proposal =
            requireExecuting(database, proposalId, QStringLiteral("Only an executing AI edit transaction can complete undo."));

        if (operationsWithStatus(proposal, QStringLiteral("completed")).isEmpty()) {
            throw CoreError(QStringLiteral("This AI edit transaction has no completed operations to undo."));
        }

        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_operations SET status = 'undone', error_code = NULL, error_message = NULL "
                "WHERE proposal_id = ? AND status = 'completed'"
            ))
            .bind(1, proposalId)
            .run();
        database
            .prepare(QStringLiteral(
                "UPDATE ai_edit_proposals SET status = 'undone', error_code = NULL, error_message = NULL "
                "WHERE id = ? AND status = 'executing'"
            ))
            .bind(1, proposalId)
            .run();
        return requireProposal(database, proposalId);
    });
}

QStringList affectedPaths(const QVariantList &operations)
{
    QStringList paths;

    for (const QVariant &operation : operations) {
        for (const QString &path : operationPaths(operation.toMap())) {
            if (!paths.contains(path)) {
                paths.append(path);
            }
        }
    }

    return paths;
}
}
