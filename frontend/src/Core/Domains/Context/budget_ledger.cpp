#include "budget_ledger.h"

#include <QVariantList>
#include <algorithm>

namespace Context
{
BudgetLedger::BudgetLedger(int totalTokens, int outputReserve)
    : m_total(std::max(0, totalTokens))
    , m_outputReserve(std::max(0, outputReserve))
{
}

void BudgetLedger::reserve(const QString &name, int tokens)
{
    for (auto &entry : m_reserves) {
        if (entry.first == name) {
            entry.second = std::max(0, tokens);
            return;
        }
    }

    m_reserves.append({name, std::max(0, tokens)});
}

int BudgetLedger::reserved(const QString &name) const
{
    for (const auto &entry : m_reserves) {
        if (entry.first == name) {
            return entry.second;
        }
    }

    return 0;
}

void BudgetLedger::charge(Kind kind, int tokens)
{
    m_used[kind] += std::max(0, tokens);
    m_items[kind] += 1;
}

QString BudgetLedger::refusal(Kind kind, int tokens, int kindCap) const
{
    if (kindCap >= 0 && m_used.value(kind) + tokens > kindCap) {
        return Omission::categoryBudget;
    }

    if (used() + tokens > inputLimit()) {
        return Omission::totalBudget;
    }

    return {};
}

QString BudgetLedger::tryCharge(Kind kind, int tokens, int kindCap)
{
    const QString reason = refusal(kind, tokens, kindCap);

    if (reason.isEmpty()) {
        charge(kind, tokens);
    }

    return reason;
}

int BudgetLedger::drawReserve(const QString &name, Kind kind, int tokens)
{
    const int available = std::max(0, reserved(name) - m_drawn.value(name));
    const int granted = std::clamp(tokens, 0, available);
    m_drawn[name] += granted;
    m_reserveUsed[kind] += granted;
    m_items[kind] += 1;
    return granted;
}

int BudgetLedger::inputLimit() const
{
    int reserves = 0;

    for (const auto &entry : m_reserves) {
        reserves += entry.second;
    }

    return std::max(0, m_total - m_outputReserve - reserves);
}

int BudgetLedger::used() const
{
    int total = 0;

    for (auto it = m_used.cbegin(); it != m_used.cend(); ++it) {
        total += it.value();
    }

    return total;
}

QVariantMap BudgetLedger::toVariant(const QHash<Kind, int> &caps) const
{
    static const QList<Kind> order{
        Kind::Instructions, Kind::ToolDefinitions, Kind::CurrentRequest, Kind::ConversationHistory,
        Kind::ConversationRecall, Kind::Attachment, Kind::LibraryEvidence, Kind::WikiKnowledge,
        Kind::Memory, Kind::ToolResult,
    };
    QVariantList lines;

    for (const Kind kind : order) {
        if (!m_used.contains(kind) && !m_items.contains(kind) && !m_reserveUsed.contains(kind) && !caps.contains(kind)) {
            continue;
        }

        lines.append(QVariantMap{
            {QStringLiteral("kind"), kindName(kind)},
            {QStringLiteral("tokens"), m_used.value(kind) + m_reserveUsed.value(kind)},
            {QStringLiteral("fromReserve"), m_reserveUsed.value(kind)},
            {QStringLiteral("items"), m_items.value(kind)},
            {QStringLiteral("cap"), caps.contains(kind) && caps.value(kind) >= 0 ? QVariant(caps.value(kind)) : QVariant::fromValue(nullptr)},
        });
    }

    QVariantMap reserves;

    for (const auto &entry : m_reserves) {
        reserves.insert(entry.first, QVariantMap{
            {QStringLiteral("tokens"), entry.second},
            {QStringLiteral("drawn"), m_drawn.value(entry.first)},
        });
    }

    return {
        {QStringLiteral("totalTokens"), m_total},
        {QStringLiteral("outputReserve"), m_outputReserve},
        {QStringLiteral("reserves"), reserves},
        {QStringLiteral("inputLimit"), inputLimit()},
        {QStringLiteral("used"), used()},
        {QStringLiteral("remaining"), remaining()},
        {QStringLiteral("overBudget"), overBudget()},
        {QStringLiteral("lines"), lines},
    };
}
}
