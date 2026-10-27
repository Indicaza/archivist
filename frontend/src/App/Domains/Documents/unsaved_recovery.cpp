#include "App/Domains/Documents/unsaved_recovery.h"

#include "App/Domains/Documents/document_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr int kFormatVersion = 1;
constexpr int kSettleMs = 1200;
}

UnsavedRecovery::UnsavedRecovery(DocumentStore *documents, QString filePath, QObject *parent)
    : QObject(parent)
    , m_documents(documents)
    , m_filePath(std::move(filePath))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSettleMs);
    connect(&m_timer, &QTimer::timeout, this, &UnsavedRecovery::flush);

    if (m_documents) {
        connect(m_documents, &DocumentStore::revisionChanged, this, &UnsavedRecovery::schedule);
    }
}

void UnsavedRecovery::schedule()
{
    m_timer.start();
}

int UnsavedRecovery::restore()
{
    QFile file(m_filePath);

    if (!m_documents || !file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray bytes = file.readAll();
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();

    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        return 0;
    }

    const int restored = m_documents->restoreUnsaved(root.value(QStringLiteral("documents")).toArray());
    m_written = bytes;
    return restored;
}

void UnsavedRecovery::flush()
{
    m_timer.stop();

    if (!m_documents) {
        return;
    }

    const QJsonArray unsaved = m_documents->encodeUnsaved();

    if (unsaved.isEmpty()) {
        if (!m_written.isEmpty() || QFile::exists(m_filePath)) {
            QFile::remove(m_filePath);
            m_written.clear();
        }
        return;
    }

    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("documents"), unsaved},
    }).toJson(QJsonDocument::Compact);

    if (bytes == m_written) {
        return;
    }

    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(bytes);

    if (file.commit()) {
        m_written = bytes;
    }
}
