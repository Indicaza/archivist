#pragma once

#include <QString>
#include <QVariant>
#include <stdexcept>

// The IDE host's AppError: an HTTP status plus the message the web IDE shows.
class IdeHttpError final : public std::runtime_error
{
public:
    IdeHttpError(int status, const QString &message, QVariant details = {})
        : std::runtime_error(message.toStdString())
        , m_status(status)
        , m_message(message)
        , m_details(std::move(details))
    {
    }

    [[nodiscard]] int status() const noexcept { return m_status; }
    [[nodiscard]] const QString &message() const noexcept { return m_message; }
    [[nodiscard]] const QVariant &details() const noexcept { return m_details; }

private:
    int m_status;
    QString m_message;
    QVariant m_details;
};
