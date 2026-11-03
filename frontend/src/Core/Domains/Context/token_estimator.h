#pragma once

#include <QString>
#include <algorithm>
#include <cmath>

namespace Context
{
// Counts tokens for budgeting. The assembler only ever asks this interface,
// so a model-specific tokenizer can replace the estimate without touching
// selection logic.
class TokenEstimator
{
public:
    virtual ~TokenEstimator() = default;
    [[nodiscard]] virtual int count(const QString &text) const = 0;
    [[nodiscard]] virtual QString name() const = 0;
};

// The estimate every Context Compiler has used so far: one token per four
// UTF-16 code units, at least one for non-empty text. Same numbers as
// ContextCompilers::estimateTokens.
class CharacterEstimator final : public TokenEstimator
{
public:
    [[nodiscard]] int count(const QString &text) const override
    {
        if (text.isEmpty()) {
            return 0;
        }

        return std::max<int>(1, static_cast<int>(std::ceil(text.size() / 4.0)));
    }

    [[nodiscard]] QString name() const override { return QStringLiteral("chars-div-4"); }
};
}
