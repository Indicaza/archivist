#include "web_socket_connection.h"

#include <QCryptographicHash>
#include <QStringDecoder>
#include <QTcpSocket>
#include <QtEndian>

namespace
{
constexpr quint8 continuationFrame = 0x0;
constexpr quint8 textFrame = 0x1;
constexpr quint8 binaryFrame = 0x2;
constexpr quint8 closeFrame = 0x8;
constexpr quint8 pingFrame = 0x9;
constexpr quint8 pongFrame = 0xA;

bool validUtf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder.decode(bytes);
    Q_UNUSED(decoded);
    return !decoder.hasError();
}
}

WebSocketConnection::WebSocketConnection(QTcpSocket *socket, qint64 maximumPayload, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_maximumPayload(maximumPayload)
{
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, &WebSocketConnection::readAvailable);
    connect(socket, &QTcpSocket::disconnected, this, [this]() { finish(1006, {}); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this]() {
        if (m_socket && m_socket->state() != QAbstractSocket::ConnectedState) {
            finish(1006, {});
        }
    });
}

WebSocketConnection::~WebSocketConnection()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

bool WebSocketConnection::isOpen() const
{
    return m_open && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QByteArray WebSocketConnection::acceptKey(const QByteArray &clientKey)
{
    return QCryptographicHash::hash(clientKey.trimmed() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1)
        .toBase64();
}

void WebSocketConnection::sendText(const QByteArray &utf8)
{
    if (isOpen()) {
        sendFrame(textFrame, utf8);
    }
}

void WebSocketConnection::sendText(const QString &text)
{
    sendText(text.toUtf8());
}

void WebSocketConnection::close(quint16 code, const QString &reason)
{
    if (!isOpen()) {
        return;
    }

    QByteArray payload(2, '\0');
    qToBigEndian(code, payload.data());
    payload += reason.toUtf8().left(123);
    sendFrame(closeFrame, payload);
    m_open = false;
    m_socket->disconnectFromHost();
}

void WebSocketConnection::receive(const QByteArray &bytes)
{
    m_buffer += bytes;
    processBuffer();
}

void WebSocketConnection::readAvailable()
{
    if (!m_socket) {
        return;
    }

    m_buffer += m_socket->readAll();
    processBuffer();
}

void WebSocketConnection::processBuffer()
{
    while (m_open && !m_finished) {
        if (m_buffer.size() < 2) {
            return;
        }

        const auto *bytes = reinterpret_cast<const uchar *>(m_buffer.constData());
        const bool final = bytes[0] & 0x80;
        const quint8 reserved = bytes[0] & 0x70;
        const quint8 opcode = bytes[0] & 0x0F;
        const bool masked = bytes[1] & 0x80;
        quint64 length = bytes[1] & 0x7F;
        qsizetype offset = 2;

        if (length == 126) {
            if (m_buffer.size() < 4) {
                return;
            }

            length = qFromBigEndian<quint16>(bytes + 2);
            offset = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10) {
                return;
            }

            length = qFromBigEndian<quint64>(bytes + 2);
            offset = 10;
        }

        if (reserved != 0 || !masked) {
            fail(1002, QStringLiteral("Invalid WebSocket frame."));
            return;
        }

        if (static_cast<qint64>(length) < 0
            || static_cast<qint64>(length) > m_maximumPayload
            || static_cast<qint64>(m_message.size() + length) > m_maximumPayload) {
            fail(1009, QStringLiteral("Message too big."));
            return;
        }

        if (m_buffer.size() < offset + 4 + static_cast<qsizetype>(length)) {
            return;
        }

        const QByteArray mask = m_buffer.mid(offset, 4);
        QByteArray payload = m_buffer.mid(offset + 4, static_cast<qsizetype>(length));
        m_buffer.remove(0, offset + 4 + static_cast<qsizetype>(length));

        for (qsizetype index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<char>(payload[index] ^ mask[index % 4]);
        }

        handleFrame(opcode, final, payload);
    }
}

void WebSocketConnection::handleFrame(quint8 opcode, bool final, const QByteArray &payload)
{
    if (opcode >= closeFrame) {
        if (!final || payload.size() > 125) {
            fail(1002, QStringLiteral("Invalid control frame."));
            return;
        }

        if (opcode == pingFrame) {
            sendFrame(pongFrame, payload);
        } else if (opcode == closeFrame) {
            const quint16 code = payload.size() >= 2
                ? qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(payload.constData()))
                : quint16(1005);
            const QString reason = QString::fromUtf8(payload.mid(2));

            if (m_open && m_socket) {
                sendFrame(closeFrame, payload.left(2));
                m_open = false;
                m_socket->disconnectFromHost();
            }

            finish(code, reason);
        } else if (opcode != pongFrame) {
            fail(1002, QStringLiteral("Unknown control frame."));
        }

        return;
    }

    if (opcode == continuationFrame) {
        if (m_messageOpcode == 0) {
            fail(1002, QStringLiteral("Unexpected continuation frame."));
            return;
        }

        m_message += payload;
    } else if (opcode == textFrame || opcode == binaryFrame) {
        if (m_messageOpcode != 0) {
            fail(1002, QStringLiteral("Expected a continuation frame."));
            return;
        }

        m_messageOpcode = opcode;
        m_message = payload;
    } else {
        fail(1002, QStringLiteral("Unknown frame."));
        return;
    }

    if (!final) {
        return;
    }

    const quint8 messageOpcode = m_messageOpcode;
    const QByteArray message = std::exchange(m_message, {});
    m_messageOpcode = 0;

    if (messageOpcode == binaryFrame) {
        emit binaryMessageReceived();
        return;
    }

    if (!validUtf8(message)) {
        fail(1007, QStringLiteral("Invalid UTF-8."));
        return;
    }

    emit textMessageReceived(message);
}

void WebSocketConnection::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray frame;
    frame.reserve(payload.size() + 10);
    frame.append(static_cast<char>(0x80 | opcode));

    if (payload.size() < 126) {
        frame.append(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        frame.append(static_cast<char>(126));
        char length[2];
        qToBigEndian(static_cast<quint16>(payload.size()), length);
        frame.append(length, 2);
    } else {
        frame.append(static_cast<char>(127));
        char length[8];
        qToBigEndian(static_cast<quint64>(payload.size()), length);
        frame.append(length, 8);
    }

    frame.append(payload);
    m_socket->write(frame);
}

void WebSocketConnection::fail(quint16 code, const QString &reason)
{
    close(code, reason);
    finish(code, reason);
}

void WebSocketConnection::finish(quint16 code, const QString &reason)
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    m_open = false;
    emit closed(code, reason);
}
