#pragma once

#include <QString>
#include <QVariantMap>
#include <stdexcept>

// Thrown by the native core when a database or domain operation fails.
// Callers on the UI side receive the message through CoreResult instead.
class CoreError final : public std::runtime_error
{
public:
    explicit CoreError(const QString &message, const QVariantMap &details = {})
        : std::runtime_error(message.toStdString())
        , m_message(message)
        , m_details(details)
    {
    }

    [[nodiscard]] const QString &message() const noexcept
    {
        return m_message;
    }

    // Machine-readable context, like the old API's error.details
    // (for example {code: "LIBRARY_EXISTS", libraryId}).
    [[nodiscard]] const QVariantMap &details() const noexcept
    {
        return m_details;
    }

private:
    QString m_message;
    QVariantMap m_details;
};
