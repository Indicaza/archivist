#pragma once

#include <QString>
#include <QVariantMap>

// Config validation for the Context Compilers registered in
// backend/src/core/cognition/conscious/context/ContextCompilerRegistry.ts.
// Only the config schemas are ported here; compiling context still runs in
// the AI runtime.
namespace ContextCompilerCatalog
{
[[nodiscard]] bool has(const QString &id, int version);

// Throws CoreError with the same message the Node registry produced when the
// compiler is unknown or the config does not match its schema.
void validateConfig(const QString &id, int version, const QVariantMap &config);
}
