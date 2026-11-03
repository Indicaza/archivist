#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QVariantMap>

namespace Retrieval
{
class SemanticIndexWorker;

// The app's handle on semantic indexing (QML singleton "SemanticIndex").
//
// Indexing runs on its own "ArchivistEmbeddings" thread with its own database
// connection and network manager, so neither the UI nor chat streams wait on
// it. Every call here returns at once; status arrives through statusChanged()
// and status(libraryId).
//
// Semantic indexing sends Library text to OpenAI, so it is off for every
// Library until the user enables it for that Library.
class SemanticIndexService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // Bumped on every status change so QML bindings re-read status().
    Q_PROPERTY(int revision READ revision NOTIFY statusChanged)
    Q_PROPERTY(QString model READ model CONSTANT)

public:
    explicit SemanticIndexService(QObject *parent = nullptr);
    ~SemanticIndexService() override;

    // Starts the indexing thread, re-queues interrupted jobs and syncs the
    // enabled Libraries.
    bool start(const QString &databasePath);
    void stop();

    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QString model() const;

    // The latest known status (see EmbeddingIndex::libraryStatus), plus
    // keyConfigured. Empty until the first refresh for that Library arrives;
    // asking schedules one.
    Q_INVOKABLE QVariantMap status(const QString &libraryId);
    Q_INVOKABLE void refresh(const QString &libraryId);

    Q_INVOKABLE void enable(const QString &libraryId);
    Q_INVOKABLE void disable(const QString &libraryId);
    Q_INVOKABLE void pause(const QString &libraryId);
    Q_INVOKABLE void resume(const QString &libraryId);
    Q_INVOKABLE void cancel(const QString &libraryId);
    // Picks up file changes (after a scan, a save, an AI edit).
    Q_INVOKABLE void sync(const QString &libraryId);

signals:
    void readyChanged();
    void statusChanged(const QString &libraryId);

private:
    void onStatus(const QString &libraryId, const QVariantMap &status);

    QThread m_thread;
    QPointer<SemanticIndexWorker> m_worker;
    QHash<QString, QVariantMap> m_status;
    QString m_databasePath;
    bool m_ready = false;
    int m_revision = 0;
};
}
