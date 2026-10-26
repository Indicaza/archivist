#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class RuntimeTraceStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(QString traceFilePath READ traceFilePath CONSTANT)

public:
    explicit RuntimeTraceStore(QObject *parent = nullptr);
    ~RuntimeTraceStore() override;

    bool enabled() const;
    QString traceFilePath() const;

    Q_INVOKABLE void emitEvent(
        const QString &domain,
        const QString &eventName,
        const QVariantMap &details = QVariantMap()
    );
    Q_INVOKABLE void flush();

private:
    void initializeTraceFile();

    static constexpr qint64 MaxTraceBytes = 4 * 1024 * 1024;
    static constexpr int MaxPendingBytes = 256 * 1024;

    bool m_enabled = false;
    QString m_traceFilePath;
    quint64 m_sequence = 0;
    QByteArray m_pending;
    QTimer m_flushTimer;
    QElapsedTimer m_elapsedTimer;
};
