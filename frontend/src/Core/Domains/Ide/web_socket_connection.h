#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

class QTcpSocket;

// The server side of one RFC 6455 WebSocket on an upgraded loopback
// connection. It replaces the `ws` package the Node IDE host used.
class WebSocketConnection final : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of `socket`. `maximumPayload` caps one message.
    WebSocketConnection(QTcpSocket *socket, qint64 maximumPayload, QObject *parent = nullptr);
    ~WebSocketConnection() override;

    [[nodiscard]] bool isOpen() const;
    void sendText(const QByteArray &utf8);
    void sendText(const QString &text);
    void close(quint16 code = 1000, const QString &reason = {});

    // Bytes that arrived in the same packet as the upgrade request.
    void receive(const QByteArray &bytes);

    // Sec-WebSocket-Accept for a client's Sec-WebSocket-Key.
    [[nodiscard]] static QByteArray acceptKey(const QByteArray &clientKey);

signals:
    void textMessageReceived(const QByteArray &utf8);
    void binaryMessageReceived();
    // Emitted once, when the connection is gone.
    void closed(quint16 code, const QString &reason);

private:
    void readAvailable();
    void processBuffer();
    void handleFrame(quint8 opcode, bool final, const QByteArray &payload);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void fail(quint16 code, const QString &reason);
    void finish(quint16 code, const QString &reason);

    QPointer<QTcpSocket> m_socket;
    qint64 m_maximumPayload = 0;
    QByteArray m_buffer;
    QByteArray m_message;
    quint8 m_messageOpcode = 0;
    bool m_open = true;
    bool m_finished = false;
};
