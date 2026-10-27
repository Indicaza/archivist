#pragma once

#include "Core/Domains/Documents/document_buffers.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// QML face of the native document buffers. Editor pages (one Monaco page per
// Anchor or window) open documents here, report their edits and apply edits
// made elsewhere, so every view of a document shows the same text and
// unsaved edits never live only inside a page that might be rebuilt.
//
// Holders are view ids from the workspace. A dirty buffer outlives its last
// view until it is saved or discarded. Saves go through LibraryStore, which
// routes them to the document's own Library (worktree), via the saver.
class DocumentStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList dirtyDocuments READ dirtyDocuments NOTIFY dirtyDocumentsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit DocumentStore(QObject *parent = nullptr);

    // Writes a document to its own Library (worktree). The app wires this to
    // LibraryStore::saveDocumentContent and reports back through
    // noteFileSaved / noteFileSaveFailed.
    using Saver = std::function<void(const QString &libraryId, const QString &fileId, const QString &text, const QString &expectedModifiedAt)>;
    // Reads a document; defaults to LibraryFiles::readText on the core thread.
    using LoadCallback = std::function<void(bool ok, const QString &error, const QVariantMap &preview)>;
    using Loader = std::function<void(const QString &libraryId, const QString &fileId, QObject *context, const LoadCallback &done)>;
    void setSaver(Saver saver);
    void setLoader(Loader loader);
    void noteFileSaved(const QString &libraryId, const QString &fileId, const QVariantMap &preview);
    void noteFileSaveFailed(const QString &libraryId, const QString &fileId, const QString &message);

    [[nodiscard]] QStringList dirtyDocuments() const;
    [[nodiscard]] int revision() const { return m_revision; }

    // seed: {collectionId, libraryId, fileId, path, language}
    Q_INVOKABLE void retain(const QString &documentId, const QString &viewId, const QVariantMap &seed);
    Q_INVOKABLE void release(const QString &documentId, const QString &viewId);
    Q_INVOKABLE void releaseView(const QString &viewId);

    // {exists, loading, loaded, error, text, version, dirty, saving,
    //  readOnly, diskChanged, language, path, libraryId, fileId, modifiedAt}
    [[nodiscard]] Q_INVOKABLE QVariantMap snapshot(const QString &documentId) const;
    [[nodiscard]] Q_INVOKABLE bool isDirty(const QString &documentId) const;
    [[nodiscard]] Q_INVOKABLE bool isSaving(const QString &documentId) const;
    [[nodiscard]] Q_INVOKABLE QStringList holders(const QString &documentId) const;

    // changesJson: [{offset, length, text}] against `baseVersion`.
    // Returns {ok, conflict, version, error}.
    Q_INVOKABLE QVariantMap applyEdits(
        const QString &documentId,
        const QString &hostId,
        double baseVersion,
        const QString &changesJson
    );
    Q_INVOKABLE QVariantMap replaceText(
        const QString &documentId,
        const QString &hostId,
        double baseVersion,
        const QString &text
    );

    Q_INVOKABLE void save(const QString &documentId);
    Q_INVOKABLE void saveAll();
    Q_INVOKABLE void discard(const QString &documentId);
    // Re-reads the file when the buffer is clean (after a change on disk).
    Q_INVOKABLE void reload(const QString &documentId);

    // Unsaved text for crash recovery.
    [[nodiscard]] QJsonArray encodeUnsaved() const;
    int restoreUnsaved(const QJsonArray &unsaved);

    // documentId is collectionId:libraryId:fileId.
    [[nodiscard]] static QString documentIdFor(const QString &collectionId, const QString &libraryId, const QString &fileId);
    [[nodiscard]] static QString libraryIdOf(const QString &documentId);

signals:
    void documentLoaded(const QString &documentId);
    void documentLoadFailed(const QString &documentId, const QString &message);
    // An edit from `hostId`; other hosts apply `changesJson` when they are at
    // `baseVersion`, otherwise they resync from snapshot().
    void documentEdited(
        const QString &documentId,
        const QString &hostId,
        double baseVersion,
        double version,
        const QString &changesJson
    );
    // The whole text changed (reload, discard, recovery): resync.
    void documentReset(const QString &documentId);
    void dirtyChanged(const QString &documentId, const QString &libraryId, bool dirty);
    void dirtyDocumentsChanged();
    void saveStarted(const QString &documentId);
    void documentSaved(const QString &documentId);
    void saveFailed(const QString &documentId, const QString &message);
    void revisionChanged();

private:
    struct Pending
    {
        QString documentId;
        qint64 version = 0;
        QString text;
    };

    void load(const QString &documentId, const QVariantMap &seed);
    void noteDirty(const QString &documentId, bool wasDirty);
    void bump();

    Documents::DocumentBuffers m_buffers;
    QHash<QString, QSet<QString>> m_waitingHolders; // document -> views before load
    QHash<QString, QVariantMap> m_seeds;
    QSet<QString> m_loading;
    QHash<QString, QString> m_loadErrors;
    QHash<QString, Pending> m_saves; // libraryId:fileId -> pending save
    Saver m_saver;
    Loader m_loader;
    int m_revision = 0;
};
