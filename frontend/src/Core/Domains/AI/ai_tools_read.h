#pragma once

#include "ai_tools.h"

namespace AITools::ReadTools
{
struct ToolCancelled
{
};

[[nodiscard]] bool isNumber(const QVariant &value);
void throwIfCancelled(const Context &context);
[[nodiscard]] QString requireLibraryId(const Context &context);

[[nodiscard]] QVariantMap listDirectory(const QVariantMap &input, Context &context);
[[nodiscard]] QVariantMap searchFilenames(const QVariantMap &input, Context &context);
[[nodiscard]] QVariantMap searchLibrary(const QVariantMap &input, Context &context);
[[nodiscard]] QVariantMap readFile(const QVariantMap &input, Context &context);
[[nodiscard]] QVariantMap readFileRange(const QVariantMap &input, Context &context);
[[nodiscard]] QVariantMap readFileRanges(const QVariantMap &input, Context &context);
} // namespace AITools::ReadTools
