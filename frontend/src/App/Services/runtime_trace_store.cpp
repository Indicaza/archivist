#include "runtime_trace_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QVariant>
#include <QtEnvironmentVariables>

namespace {
QString configuredTracePath()
{
    const QString configured = qEnvironmentVariable(
        "ARCHIVIST_RUNTIME_TRACE_PATH"
    ).trimmed();

    if (!configured.isEmpty()) {
        return configured;
    }

    return QDir(QDir::currentPath()).filePath(
        "backend/data/runtime/logs/runtime-trace.jsonl"
    );
}
}

RuntimeTraceStore::RuntimeTraceStore(QObject *parent)
    : QObject(parent),
      m_enabled(
          qEnvironmentVariableIsSet("ARCHIVIST_RUNTIME_TRACE")
          || qEnvironmentVariableIsSet("ARCHIVIST_CHAT_TRACE")
      ),
      m_traceFilePath(configuredTracePath())
{
    m_elapsedTimer.start();
    m_flushTimer.setInterval(80);
    m_flushTimer.setSingleShot(true);

    connect(
        &m_flushTimer,
        &QTimer::timeout,
        this,
        &RuntimeTraceStore::flush
    );

    if (m_enabled) {
        initializeTraceFile();
    }
}

RuntimeTraceStore::~RuntimeTraceStore()
{
    flush();
}

bool RuntimeTraceStore::enabled() const
{
    return m_enabled;
}

QString RuntimeTraceStore::traceFilePath() const
{
    return m_traceFilePath;
}

void RuntimeTraceStore::initializeTraceFile()
{
    const QFileInfo info(m_traceFilePath);
    QDir().mkpath(info.absolutePath());

    QFile file(m_traceFilePath);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
}

void RuntimeTraceStore::emitEvent(
    const QString &domain,
    const QString &eventName,
    const QVariantMap &details
)
{
    if (!m_enabled) {
        return;
    }

    QVariantMap payload = details;
    payload.insert("sequence", QVariant::fromValue(++m_sequence));
    payload.insert("timestampMs", QDateTime::currentMSecsSinceEpoch());
    payload.insert("elapsedMs", m_elapsedTimer.elapsed());
    payload.insert("domain", domain.isEmpty() ? "runtime" : domain);
    payload.insert("event", eventName.isEmpty() ? "event" : eventName);

    QByteArray line = QJsonDocument::fromVariant(payload).toJson(
        QJsonDocument::Compact
    );
    line.append('\n');
    m_pending.append(line);

    if (m_pending.size() >= MaxPendingBytes) {
        flush();
        return;
    }

    if (!m_flushTimer.isActive()) {
        m_flushTimer.start();
    }
}

void RuntimeTraceStore::flush()
{
    if (!m_enabled || m_pending.isEmpty()) {
        return;
    }

    QFile file(m_traceFilePath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly
        | QIODevice::Append
        | QIODevice::Text;

    if (
        file.exists()
        && file.size() + m_pending.size() > MaxTraceBytes
    ) {
        mode = QIODevice::WriteOnly
            | QIODevice::Truncate
            | QIODevice::Text;
    }

    if (!file.open(mode)) {
        return;
    }

    file.write(m_pending);
    file.flush();
    m_pending.clear();
}
