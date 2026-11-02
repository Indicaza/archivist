// The tool registry: every family's Definitions in one list sorted by id, and
// the provider-facing discovery rules (which tools an Availability offers).
#include "ai_tools_definition.h"

#include <QSet>

#include <algorithm>

namespace AITools
{
const QList<Definition> &definitions()
{
    static const QList<Definition> list = []() {
        QList<Definition> tools;
        tools.append(libraryReadTools());
        tools.append(libraryMutationTools());
        tools.append(workspaceTools());

        std::sort(tools.begin(), tools.end(), [](const Definition &first, const Definition &second) {
            return first.id < second.id;
        });
        return tools;
    }();

    return list;
}

const Definition *findTool(const QString &toolId)
{
    for (const Definition &definition : definitions()) {
        if (definition.id == toolId) {
            return &definition;
        }
    }

    return nullptr;
}

QVariantList providerTools(const Availability &availability)
{
    static const QSet<QString> discovery{QStringLiteral("list_directory"), QStringLiteral("search_filenames"), QStringLiteral("search_library")};
    const bool verificationOnly = !availability.discovery && !availability.fullFileRead;
    QVariantList result;

    for (const Definition &tool : definitions()) {
        bool include = false;

        if (tool.executionMode == workspaceMode) {
            include = availability.workspace;
        } else if (tool.permission == safeLocalMutationPermission && tool.executionMode == proposalMode) {
            include = availability.mutationProposals;
        } else if (tool.permission == readOnlyPermission) {
            include = verificationOnly
                ? availability.verification && tool.id == QStringLiteral("read_file_ranges")
                : (availability.discovery || !discovery.contains(tool.id))
                    && (availability.fullFileRead || tool.id != QStringLiteral("read_file"));
        }

        if (include) {
            result.append(QVariantMap{
                {QStringLiteral("name"), tool.id},
                {QStringLiteral("description"), tool.description},
                {QStringLiteral("parameters"), tool.schema},
            });
        }
    }

    return result;
}

bool isMutationProposalTool(const QString &toolId)
{
    const Definition *tool = findTool(toolId);
    return tool && tool->executionMode == proposalMode;
}

bool isWorkspaceTool(const QString &toolId)
{
    const Definition *tool = findTool(toolId);
    return tool && tool->executionMode == workspaceMode;
}

QStringList toolIds()
{
    QStringList ids;

    for (const Definition &tool : definitions()) {
        ids.append(tool.id);
    }

    return ids;
}
} // namespace AITools
