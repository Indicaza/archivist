#pragma once

#include "context_contracts.h"

#include <QList>
#include <QVariantMap>

namespace Context
{
// One account of the whole provider request: the output reserve and every
// token sent, by kind. Optional items go through tryCharge(), which refuses
// anything that would cross the input limit or the kind's cap. Required items
// (instructions, tools, the current request) always go through charge(); if
// they alone exceed the limit the ledger says so in overBudget() rather than
// dropping the user's message.
class BudgetLedger
{
public:
    BudgetLedger(int totalTokens, int outputReserve);

    // Holds tokens back for content that arrives later (tool results).
    void reserve(const QString &name, int tokens);
    [[nodiscard]] int reserved(const QString &name) const;

    // Unconditional charge for required content.
    void charge(Kind kind, int tokens);

    // Why `tokens` of `kind` cannot be charged, or an empty string if they can.
    [[nodiscard]] QString refusal(Kind kind, int tokens, int kindCap) const;
    // Charges when refusal() is empty; returns the refusal otherwise.
    QString tryCharge(Kind kind, int tokens, int kindCap);

    // Spends part of a reservation (tool output as it arrives). Returns how
    // many of `tokens` fit; the rest must not be sent.
    int drawReserve(const QString &name, Kind kind, int tokens);

    [[nodiscard]] int totalTokens() const { return m_total; }
    [[nodiscard]] int outputReserve() const { return m_outputReserve; }
    // Tokens the request may use: total minus output and named reserves.
    [[nodiscard]] int inputLimit() const;
    [[nodiscard]] int used() const;
    [[nodiscard]] int used(Kind kind) const { return m_used.value(kind); }
    [[nodiscard]] int remaining() const { return inputLimit() - used(); }
    [[nodiscard]] bool overBudget() const { return used() > inputLimit(); }

    // {totalTokens, outputReserve, reserves{}, inputLimit, used, remaining,
    //  overBudget, lines[{kind, tokens, items, cap}]}
    [[nodiscard]] QVariantMap toVariant(const QHash<Kind, int> &caps = {}) const;

private:
    int m_total;
    int m_outputReserve;
    QList<QPair<QString, int>> m_reserves;
    QHash<QString, int> m_drawn;
    QHash<Kind, int> m_used;
    QHash<Kind, int> m_items;
    QHash<Kind, int> m_reserveUsed;
};
}
