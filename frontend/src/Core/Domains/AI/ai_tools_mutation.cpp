// Library edit proposal tools: create_file, patch_file, create_directory,
// rename_file and move_file. They snapshot the Library state and record a
// reviewable AIEditProposals proposal; none of them writes to the Library.
#include "ai_tools_definition.h"
#include "ai_tools_input.h"
#include "ai_tools_read.h"

#include "Core/Domains/Libraries/library_edit_inspection.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"
#include "edit_proposals.h"

#include <QCryptographicHash>

#include <limits>

using namespace AITools::Input;
using AITools::Context;
using AITools::ToolError;
using AITools::ReadTools::requireLibraryId;
using AITools::ReadTools::throwIfCancelled;

namespace
{
QString sha256Hex(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap proposeAndAnnounce(
    Context &context,
    const QString &summary,
    const QVariantMap &draft,
    const QString &failureMessage,
    const QString &expectedHash = {}
)
{
    const QVariantMap proposal = AIEditProposals::propose(*context.database, context.runId, summary, {draft});
    const QVariantList operations = proposal.value(QStringLiteral("operations")).toList();

    if (operations.isEmpty()) {
        throw CoreError(failureMessage);
    }

    const QVariantMap operation = operations.last().toMap();
    QVariantList types;

    for (const QVariant &item : operations) {
        types.append(item.toMap().value(QStringLiteral("type")));
    }

    QVariantMap payload{
        {QStringLiteral("proposalId"), proposal.value(QStringLiteral("id"))},
        {QStringLiteral("executionId"), context.executionId},
        {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
        {QStringLiteral("operationCount"), operations.size()},
        {QStringLiteral("operationTypes"), types},
        {QStringLiteral("affectedPaths"), CoreValues::toVariantList(AIEditProposals::affectedPaths(operations))},
        {QStringLiteral("writesPerformed"), false},
    };

    if (!expectedHash.isEmpty()) {
        payload.insert(QStringLiteral("expectedHash"), expectedHash);
    }

    QVariantMap result{
        {QStringLiteral("proposalId"), proposal.value(QStringLiteral("id"))},
        {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
    };

    if (context.emitEvent) {
        // Emitted after the caller builds its output, like the Node tools.
        result.insert(QStringLiteral("__payload"), payload);
    }

    return result;
}

QVariantMap finishProposal(Context &context, QVariantMap proposed, QVariantMap output)
{
    const QVariantMap payload = proposed.take(QStringLiteral("__payload")).toMap();
    output.insert(QStringLiteral("proposalId"), proposed.value(QStringLiteral("proposalId")));
    output.insert(QStringLiteral("operationId"), proposed.value(QStringLiteral("operationId")));
    output.insert(QStringLiteral("status"), QStringLiteral("proposed"));
    output.insert(QStringLiteral("writesPerformed"), false);

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("edit.proposed"), payload);
    }

    return output;
}

QVariantMap createFileTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto target = LibraryEditInspection::createTarget(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QString content = input.value(QStringLiteral("content")).toString();
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Create %1").arg(target.relativePath),
        {{QStringLiteral("type"), QStringLiteral("create_file")}, {QStringLiteral("destinationPath"), target.relativePath}, {QStringLiteral("afterContent"), content}},
        QStringLiteral("The create-file proposal did not retain its operation.")
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("create_file")},
        {QStringLiteral("relativePath"), target.relativePath},
        {QStringLiteral("expectedState"), target.expectedState},
        {QStringLiteral("contentSha256"), sha256Hex(content)},
    });
}

QVariantMap patchFileTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto source = LibraryEditInspection::textFile(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QString content = input.value(QStringLiteral("content")).toString();

    if (source.content == content) {
        throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("patch_file requires proposed content to differ from the current file."));
    }

    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Patch %1").arg(source.relativePath),
        {
            {QStringLiteral("type"), QStringLiteral("patch_file")},
            {QStringLiteral("sourcePath"), source.relativePath},
            {QStringLiteral("expectedHash"), source.sha256},
            {QStringLiteral("beforeContent"), source.content},
            {QStringLiteral("afterContent"), content},
        },
        QStringLiteral("The patch-file proposal did not retain its operation."),
        source.sha256
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("patch_file")},
        {QStringLiteral("relativePath"), source.relativePath},
        {QStringLiteral("expectedHash"), source.sha256},
        {QStringLiteral("proposedContentSha256"), sha256Hex(content)},
        {QStringLiteral("beforeSizeBytes"), source.sizeBytes},
        {QStringLiteral("afterSizeBytes"), content.toUtf8().size()},
        {QStringLiteral("sourceModifiedAt"), source.modifiedAt},
    });
}

QVariantMap createDirectoryTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto target =
        LibraryEditInspection::directoryCreateTarget(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Create directory %1").arg(target.relativePath),
        {{QStringLiteral("type"), QStringLiteral("create_directory")}, {QStringLiteral("destinationPath"), target.relativePath}},
        QStringLiteral("The create-directory proposal did not retain its operation.")
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("create_directory")},
        {QStringLiteral("relativePath"), target.relativePath},
        {QStringLiteral("expectedState"), target.expectedState},
    });
}

QVariantMap relocateTool(const QVariantMap &input, Context &context, const QString &type)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto source = LibraryEditInspection::textFile(*context.database, libraryId, input.value(QStringLiteral("sourcePath")).toString());
    const auto destination =
        LibraryEditInspection::createTarget(*context.database, libraryId, input.value(QStringLiteral("destinationPath")).toString());
    throwIfCancelled(context);
    const bool rename = type == QStringLiteral("rename_file");
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("%1 %2 to %3").arg(rename ? QStringLiteral("Rename") : QStringLiteral("Move"), source.relativePath, destination.relativePath),
        {
            {QStringLiteral("type"), type},
            {QStringLiteral("sourcePath"), source.relativePath},
            {QStringLiteral("destinationPath"), destination.relativePath},
            {QStringLiteral("expectedHash"), source.sha256},
        },
        rename ? QStringLiteral("The rename-file proposal did not retain its operation.")
               : QStringLiteral("The move-file proposal did not retain its operation."),
        source.sha256
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), type},
        {QStringLiteral("sourcePath"), source.relativePath},
        {QStringLiteral("destinationPath"), destination.relativePath},
        {QStringLiteral("expectedHash"), source.sha256},
        {QStringLiteral("sourceSizeBytes"), source.sizeBytes},
        {QStringLiteral("sourceModifiedAt"), source.modifiedAt},
    });
}

QVariantMap proposalOutputSummary(const QVariantMap &output, const QStringList &keys)
{
    QVariantMap summary = pick(output, keys);
    summary.insert(QStringLiteral("proposalId"), output.value(QStringLiteral("proposalId")));
    summary.insert(QStringLiteral("writesPerformed"), output.value(QStringLiteral("writesPerformed")));
    return summary;
}
} // namespace

namespace AITools
{
QList<Definition> libraryMutationTools()
{
    QList<Definition> tools;
    const QString mutation = safeLocalMutationPermission;
    const QString proposal = proposalMode;

    const auto pathAndContent = [](const QVariant &input) {
        const QVariantMap map = strictObject(input, {QStringLiteral("path"), QStringLiteral("content")});
        const QString path = stringField(map, QStringLiteral("path"), 1, 1024);
        return QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("content"), stringField(map, QStringLiteral("content"), 0, std::numeric_limits<int>::max())},
        };
    };
    const auto pathAndContentSummary = [](const QVariantMap &input) {
        return QVariantMap{
            {QStringLiteral("relativePath"), input.value(QStringLiteral("path"))},
            {QStringLiteral("contentCharacterCount"), input.value(QStringLiteral("content")).toString().size()},
        };
    };
    const auto relocate = [](const QVariant &input) {
        const QVariantMap map = strictObject(input, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")});
        const QString source = stringField(map, QStringLiteral("sourcePath"), 1, 1024);
        return QVariantMap{
            {QStringLiteral("sourcePath"), source},
            {QStringLiteral("destinationPath"), stringField(map, QStringLiteral("destinationPath"), 1, 1024)},
        };
    };
    const auto relocateSummary = [](const QVariantMap &input) {
        return pick(input, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")});
    };

    tools.append({
        QStringLiteral("create_file"),
        QStringLiteral("Propose Library File Creation"),
        QStringLiteral("Create a reviewable proposal for a new UTF-8 text file in the active Library. This tool never writes the file; execution requires later user approval."),
        mutation, proposal,
        objectSchema(
            {
                {QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative destination path for the proposed new file."))},
                {QStringLiteral("content"), stringSchema(0, 0, QStringLiteral("Complete UTF-8 text content proposed for the new file."))},
            },
            {QStringLiteral("path"), QStringLiteral("content")}
        ),
        pathAndContent,
        createFileTool,
        pathAndContentSummary,
        [](const QVariantMap &output) {
            return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedState")});
        },
    });

    tools.append({
        QStringLiteral("patch_file"),
        QStringLiteral("Propose Library File Patch"),
        QStringLiteral("Create a reviewable proposal to replace the complete UTF-8 text content of an existing Library file. Archivist snapshots the exact current content and SHA-256 first; this tool never writes the file."),
        mutation, proposal,
        objectSchema(
            {
                {QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to change."))},
                {QStringLiteral("content"), stringSchema(0, 0, QStringLiteral("Complete replacement UTF-8 text content proposed for the file. Preserve any content that should remain unchanged."))},
            },
            {QStringLiteral("path"), QStringLiteral("content")}
        ),
        pathAndContent,
        patchFileTool,
        pathAndContentSummary,
        [](const QVariantMap &output) {
            return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedHash")});
        },
    });

    tools.append({
        QStringLiteral("create_directory"),
        QStringLiteral("Propose Library Directory Creation"),
        QStringLiteral("Create a reviewable proposal for a new directory in the active Library. The parent directory must already exist. This tool never creates the directory; execution requires later user approval."),
        mutation, proposal,
        objectSchema(
            {{QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative path for the proposed new directory. Its parent directory must already exist."))}},
            {QStringLiteral("path")}
        ),
        [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("path")});
            return QVariantMap{{QStringLiteral("path"), stringField(map, QStringLiteral("path"), 1, 1024)}};
        },
        createDirectoryTool,
        [](const QVariantMap &input) { return QVariantMap{{QStringLiteral("relativePath"), input.value(QStringLiteral("path"))}}; },
        [](const QVariantMap &output) {
            return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedState")});
        },
    });

    tools.append({
        QStringLiteral("rename_file"),
        QStringLiteral("Propose Library File Rename"),
        QStringLiteral("Create a reviewable proposal to rename an existing supported UTF-8 Library text file within its current directory. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never renames the file."),
        mutation, proposal,
        objectSchema(
            {
                {QStringLiteral("sourcePath"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to rename."))},
                {QStringLiteral("destinationPath"), stringSchema(1, 1024, QStringLiteral("Library-relative destination path in the same directory, using the new file name."))},
            },
            {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")}
        ),
        relocate,
        [](const QVariantMap &input, Context &context) { return relocateTool(input, context, QStringLiteral("rename_file")); },
        relocateSummary,
        [](const QVariantMap &output) {
            return proposalOutputSummary(output, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath"), QStringLiteral("expectedHash")});
        },
    });

    tools.append({
        QStringLiteral("move_file"),
        QStringLiteral("Propose Library File Move"),
        QStringLiteral("Create a reviewable proposal to move an existing supported UTF-8 Library text file to another existing Library directory while preserving its file name. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never moves the file."),
        mutation, proposal,
        objectSchema(
            {
                {QStringLiteral("sourcePath"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to move."))},
                {QStringLiteral("destinationPath"), stringSchema(1, 1024, QStringLiteral("Library-relative path in another existing directory. Preserve the source file name."))},
            },
            {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")}
        ),
        relocate,
        [](const QVariantMap &input, Context &context) { return relocateTool(input, context, QStringLiteral("move_file")); },
        relocateSummary,
        [](const QVariantMap &output) {
            return proposalOutputSummary(output, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath"), QStringLiteral("expectedHash")});
        },
    });

    return tools;
}
} // namespace AITools
