#pragma once

#include "Core/Database/core_database.h"

#include <utility>

namespace Transactions
{
// Runs `work` inside the caller's open transaction, or opens one. Matches
// better-sqlite3, where a nested db.transaction() joins the outer one.
template<typename Work>
auto join(CoreDatabase &database, Work &&work) -> decltype(work())
{
    if (database.inTransaction()) {
        return work();
    }

    return database.transaction(std::forward<Work>(work));
}
}
