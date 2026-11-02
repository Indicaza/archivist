#pragma once

#include "ai_tools.h"

#include <QList>

// Internal contract shared by the AI tool modules. Not for use outside
// Core/Domains/AI/ai_tools_*.cpp: the rest of the app goes through ai_tools.h.
//
// One tool = one Definition. Each tool family builds its own list next to its
// handlers, and ai_tools_catalog.cpp combines the families:
//
//   ai_tools_read.cpp       Library read-only tools (discovery, file reads)
//   ai_tools_mutation.cpp   Library edit proposals (never write files)
//   ai_tools_workspace.cpp  workspace_* tools (through Workspace::Gateway)
//
// Adding a tool: append a Definition to its family's list (or add a family
// function here and list it in ai_tools_catalog.cpp), then extend
// tests/core_ai_tool_contracts_test.cpp's snapshot.
namespace AITools
{
// Permission levels (ai_tool_executions.permission_level).
inline const QString readOnlyPermission = QStringLiteral("read-only");
inline const QString safeLocalMutationPermission = QStringLiteral("safe-local-mutation");

// Execution modes (tool.requested / tool.started executionMode).
inline const QString directMode = QStringLiteral("direct");
inline const QString proposalMode = QStringLiteral("proposal");
inline const QString workspaceMode = QStringLiteral("workspace");

struct Definition
{
    QString id;
    QString name;
    QString description;
    QString permission;
    QString executionMode;
    QVariantMap schema;
    // Throws InvalidInput (ai_tools_input.h).
    std::function<QVariantMap(const QVariant &)> parse;
    // Throws ToolError, ReadTools::ToolCancelled, CoreError or std::exception.
    std::function<QVariantMap(const QVariantMap &, Context &)> run;
    std::function<QVariantMap(const QVariantMap &)> summarizeInput;
    std::function<QVariantMap(const QVariantMap &)> summarizeOutput;
};

// Tool families, each defined beside its handlers.
[[nodiscard]] QList<Definition> libraryReadTools();
[[nodiscard]] QList<Definition> libraryMutationTools();
[[nodiscard]] QList<Definition> workspaceTools();

// Every tool, sorted by id (ai_tools_catalog.cpp).
[[nodiscard]] const QList<Definition> &definitions();
[[nodiscard]] const Definition *findTool(const QString &toolId);
} // namespace AITools
