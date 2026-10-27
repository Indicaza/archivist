#include "App/Domains/Documents/document_store.h"

#include "Core/Domains/Libraries/library_files.h"
#include "Core/core_runtime.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

DocumentStore::DocumentStore(QObject *parent)
    : QObject(parent)
{
}

void DocumentStore::setSaver(Saver saver)
{
    m_saver = std::move(saver);
}

void DocumentStore::setLoader(Loader loader)
{
    m_loader = std::move(loader);
}

void DocumentStore::noteFileSaved(const QString &libraryId, const QString &fileId, const QVariantMap &preview)
{
    const QString key = libraryId + QLatin1Char(':') + fileId;

    if (!m_saves.contains(key)) {
        return;
    }

    const Pending pending = m_saves.take(key);
    const bool wasDirty = m_buffers.isDirty(pending.documentId);
    const QString modifiedAt = preview.value(QStringLiteral("file")).toMap().value(QStringLiteral("modifiedAt")).toString();
    m_buffers.markSaved(pending.documentId, pending.version, pending.text, modifiedAt);
    m_disk.remove(pending.documentId);
    if (!m_buffers.isDirty(pending.documentId)) {
        forgetRecovered(pending.documentId);
    }
    noteDirty(pending.documentId, wasDirty);
    emit documentSaved(pending.documentId);
    bump();
}

void DocumentStore::noteFileSaveFailed(const QString &libraryId, const QString &fileId, const QString &message)
{
    const QString key = libraryId + QLatin1Char(':') + fileId;

    if (!m_saves.contains(key)) {
        return;
    }

    const Pending pending = m_saves.take(key);
    emit saveFailed(pending.documentId, message);
    // The usual reason is that the file moved on: find out, so the editor
    // can offer to keep these edits or take the file.
    reload(pending.documentId);
    bump();
}

QString DocumentStore::documentIdFor(const QString &collectionId, const QString &libraryId, const QString &fileId)
{
    return collectionId + QLatin1Char(':') + libraryId + QLatin1Char(':') + fileId;
}

QString DocumentStore::libraryIdOf(const QString &documentId)
{
    const QStringList parts = documentId.split(QLatin1Char(':'));
    return parts.size() >= 3 ? parts.at(parts.size() - 2) : QString();
}

QStringList DocumentStore::dirtyDocuments() const
{
    return m_buffers.dirtyDocuments();
}

void DocumentStore::bump()
{
    ++m_revision;
    emit revisionChanged();
}

void DocumentStore::noteDirty(const QString &documentId, bool wasDirty)
{
    const bool dirty = m_buffers.isDirty(documentId);

    if (dirty != wasDirty) {
        emit dirtyChanged(documentId, libraryIdOf(documentId), dirty);
        emit dirtyDocumentsChanged();
    }
}

void DocumentStore::retain(const QString &documentId, const QString &viewId, const QVariantMap &seed)
{
    if (documentId.isEmpty() || viewId.isEmpty()) {
        return;
    }

    if (m_buffers.contains(documentId)) {
        Documents::DocumentSeed existing;
        existing.documentId = documentId;
        m_buffers.open(existing, viewId);
        return;
    }

    m_waitingHolders[documentId].insert(viewId);

    if (!seed.isEmpty()) {
        m_seeds.insert(documentId, seed);
    }

    if (!m_loading.contains(documentId)) {
        load(documentId, m_seeds.value(documentId));
    }
}

void DocumentStore::load(const QString &documentId, const QVariantMap &seed)
{
    const QString libraryId = seed.value(QStringLiteral("libraryId")).toString();
    const QString fileId = seed.value(QStringLiteral("fileId")).toString();
    if (libraryId.isEmpty() || fileId.isEmpty() || (!m_loader && !CoreRuntime::active())) {
        m_loadErrors.insert(documentId, QStringLiteral("The document could not be located."));
        emit documentLoadFailed(documentId, m_loadErrors.value(documentId));
        bump();
        return;
    }

    m_loading.insert(documentId);
    m_loadErrors.remove(documentId);
    bump();

    const auto finished = [this, documentId, seed](bool ok, const QString &error, const QVariantMap &preview) {
            m_loading.remove(documentId);

            if (!ok) {
                m_loadErrors.insert(documentId, error);
                emit documentLoadFailed(documentId, error);
                bump();
                return;
            }

            const QVariantMap file = preview.value(QStringLiteral("file")).toMap();
            const QSet<QString> holders = m_waitingHolders.take(documentId);

            if (holders.isEmpty() && !m_buffers.contains(documentId)) {
                bump();
                return; // everyone left while it loaded
            }

            Documents::DocumentSeed documentSeed;
            documentSeed.documentId = documentId;
            documentSeed.collectionId = seed.value(QStringLiteral("collectionId")).toString();
            documentSeed.libraryId = seed.value(QStringLiteral("libraryId")).toString();
            documentSeed.fileId = seed.value(QStringLiteral("fileId")).toString();
            documentSeed.path = file.value(QStringLiteral("relativePath"), seed.value(QStringLiteral("path"))).toString();
            documentSeed.language = seed.value(QStringLiteral("language")).toString();
            documentSeed.diskText = preview.value(QStringLiteral("content")).toString();
            documentSeed.diskModifiedAt = file.value(QStringLiteral("modifiedAt")).toString();

            // Recovered unsaved text may already be here: it wins over disk.
            for (const QString &holder : holders) {
                m_buffers.open(documentSeed, holder);
            }

            if (holders.isEmpty()) {
                m_buffers.open(documentSeed, QString());
            }

            emit documentLoaded(documentId);
            bump();
    };

    readFile(libraryId, fileId, finished);
}

void DocumentStore::release(const QString &documentId, const QString &viewId)
{
    if (m_waitingHolders.contains(documentId)) {
        m_waitingHolders[documentId].remove(viewId);
        if (m_waitingHolders.value(documentId).isEmpty()) {
            m_waitingHolders.remove(documentId);
        }
    }

    if (m_buffers.release(documentId, viewId)) {
        m_seeds.remove(documentId);
    }

    bump();
}

void DocumentStore::releaseView(const QString &viewId)
{
    for (auto it = m_waitingHolders.begin(); it != m_waitingHolders.end();) {
        it->remove(viewId);
        it = it->isEmpty() ? m_waitingHolders.erase(it) : std::next(it);
    }

    m_buffers.releaseView(viewId);
    bump();
}

QVariantMap DocumentStore::snapshot(const QString &documentId) const
{
    const Documents::Buffer *buffer = m_buffers.find(documentId);
    QVariantMap result{
        {QStringLiteral("exists"), buffer != nullptr},
        {QStringLiteral("loading"), m_loading.contains(documentId)},
        {QStringLiteral("loaded"), buffer != nullptr},
        {QStringLiteral("error"), m_loadErrors.value(documentId)},
        {QStringLiteral("saving"), isSaving(documentId)},
    };

    if (!buffer) {
        return result;
    }

    result.insert(QStringLiteral("text"), buffer->text);
    result.insert(QStringLiteral("version"), double(buffer->version));
    result.insert(QStringLiteral("dirty"), buffer->dirty());
    result.insert(QStringLiteral("readOnly"), buffer->readOnly);
    result.insert(QStringLiteral("diskChanged"), buffer->diskChanged);
    result.insert(QStringLiteral("recovered"), m_recovered.contains(documentId));
    result.insert(QStringLiteral("language"), buffer->language);
    result.insert(QStringLiteral("path"), buffer->path);
    result.insert(QStringLiteral("libraryId"), buffer->libraryId);
    result.insert(QStringLiteral("fileId"), buffer->fileId);
    result.insert(QStringLiteral("modifiedAt"), buffer->diskModifiedAt);
    return result;
}

bool DocumentStore::isDirty(const QString &documentId) const
{
    return m_buffers.isDirty(documentId);
}

bool DocumentStore::isSaving(const QString &documentId) const
{
    for (const Pending &pending : m_saves) {
        if (pending.documentId == documentId) {
            return true;
        }
    }
    return false;
}

QStringList DocumentStore::holders(const QString &documentId) const
{
    const Documents::Buffer *buffer = m_buffers.find(documentId);
    QStringList result = buffer ? QStringList(buffer->holders.begin(), buffer->holders.end()) : QStringList();
    result.sort();
    return result;
}

QVariantMap DocumentStore::applyEdits(const QString &documentId, const QString &hostId, double baseVersion, const QString &changesJson)
{
    QList<Documents::TextChange> changes;
    const QJsonArray array = QJsonDocument::fromJson(changesJson.toUtf8()).array();

    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        changes.append({
            qsizetype(object.value(QStringLiteral("offset")).toDouble()),
            qsizetype(object.value(QStringLiteral("length")).toDouble()),
            object.value(QStringLiteral("text")).toString(),
        });
    }

    const bool wasDirty = m_buffers.isDirty(documentId);
    const Documents::EditResult result = m_buffers.applyEdit(documentId, qint64(baseVersion), changes);

    if (result.ok) {
        emit documentEdited(documentId, hostId, baseVersion, double(result.version), changesJson);
        noteDirty(documentId, wasDirty);
        bump();
    }

    return {
        {QStringLiteral("ok"), result.ok},
        {QStringLiteral("conflict"), result.conflict},
        {QStringLiteral("version"), double(result.version)},
        {QStringLiteral("error"), result.error},
    };
}

QVariantMap DocumentStore::replaceText(const QString &documentId, const QString &hostId, double baseVersion, const QString &text)
{
    const Documents::Buffer *before = m_buffers.find(documentId);
    const qsizetype oldLength = before ? before->text.size() : 0;
    const bool wasDirty = m_buffers.isDirty(documentId);
    const Documents::EditResult result = m_buffers.replaceText(documentId, qint64(baseVersion), text);

    if (result.ok && result.version != qint64(baseVersion)) {
        const QString changes = QString::fromUtf8(QJsonDocument(QJsonArray{QJsonObject{
            {QStringLiteral("offset"), 0},
            {QStringLiteral("length"), double(oldLength)},
            {QStringLiteral("text"), text},
        }}).toJson(QJsonDocument::Compact));
        emit documentEdited(documentId, hostId, baseVersion, double(result.version), changes);
        noteDirty(documentId, wasDirty);
        bump();
    }

    return {
        {QStringLiteral("ok"), result.ok},
        {QStringLiteral("conflict"), result.conflict},
        {QStringLiteral("version"), double(result.version)},
        {QStringLiteral("error"), result.error},
    };
}

void DocumentStore::save(const QString &documentId)
{
    const Documents::Buffer *buffer = m_buffers.find(documentId);

    if (!buffer || !m_saver || buffer->readOnly) {
        return;
    }

    const QString key = buffer->libraryId + QLatin1Char(':') + buffer->fileId;

    if (m_saves.contains(key)) {
        return;
    }

    if (!buffer->dirty()) {
        emit documentSaved(documentId);
        return;
    }

    m_saves.insert(key, {documentId, buffer->version, buffer->text});
    emit saveStarted(documentId);
    bump();
    m_saver(buffer->libraryId, buffer->fileId, buffer->text, buffer->diskModifiedAt);
}

void DocumentStore::saveAll()
{
    for (const QString &documentId : m_buffers.dirtyDocuments()) {
        save(documentId);
    }
}

void DocumentStore::discard(const QString &documentId)
{
    const bool wasDirty = m_buffers.isDirty(documentId);

    if (m_buffers.discard(documentId)) {
        forgetRecovered(documentId);
        emit documentReset(documentId);
        noteDirty(documentId, wasDirty);
        bump();
    }
}

void DocumentStore::readFile(const QString &libraryId, const QString &fileId, const LoadCallback &done)
{
    if (m_loader) {
        m_loader(libraryId, fileId, this, done);
        return;
    }

    CoreRuntime *core = CoreRuntime::active();
    if (!core) {
        done(false, QStringLiteral("The document could not be read."), {});
        return;
    }

    core->run(
        [libraryId, fileId](CoreDatabase &database) {
            return QVariant(LibraryFiles::readText(database, libraryId, fileId));
        },
        this,
        [done](const CoreResult &result) {
            done(result.ok, result.errorMessage, result.value.toMap());
        }
    );
}

void DocumentStore::reload(const QString &documentId)
{
    const Documents::Buffer *buffer = m_buffers.find(documentId);

    if (!buffer) {
        return;
    }

    readFile(buffer->libraryId, buffer->fileId, [this, documentId](bool ok, const QString &, const QVariantMap &preview) {
        if (!ok || !m_buffers.contains(documentId)) {
            return;
        }

        const QString text = preview.value(QStringLiteral("content")).toString();
        const QString modifiedAt = preview.value(QStringLiteral("file")).toMap().value(QStringLiteral("modifiedAt")).toString();
        m_disk.insert(documentId, {text, modifiedAt});

        if (m_buffers.reloadFromDisk(documentId, text, modifiedAt)) {
            emit documentReset(documentId);
        }

        bump();
    });
}

void DocumentStore::keepUnsavedOverDisk(const QString &documentId)
{
    if (!m_disk.contains(documentId)) {
        return;
    }

    const DiskState disk = m_disk.value(documentId);
    const bool wasDirty = m_buffers.isDirty(documentId);
    if (m_buffers.adoptDiskBase(documentId, disk.text, disk.modifiedAt)) {
        noteDirty(documentId, wasDirty);
        bump();
    }
}

void DocumentStore::takeDiskVersion(const QString &documentId)
{
    if (!m_disk.contains(documentId)) {
        reload(documentId);
        return;
    }

    const DiskState disk = m_disk.take(documentId);
    const bool wasDirty = m_buffers.isDirty(documentId);
    m_buffers.adoptDiskBase(documentId, disk.text, disk.modifiedAt);
    m_buffers.discard(documentId);
    forgetRecovered(documentId);
    emit documentReset(documentId);
    noteDirty(documentId, wasDirty);
    bump();
}

void DocumentStore::forgetRecovered(const QString &documentId)
{
    if (m_recovered.removeAll(documentId) > 0) {
        emit recoveredDocumentsChanged();
    }
}

QJsonArray DocumentStore::encodeUnsaved() const
{
    return m_buffers.encodeUnsaved();
}

int DocumentStore::restoreUnsaved(const QJsonArray &unsaved)
{
    const QStringList before = m_buffers.dirtyDocuments();
    const int restored = m_buffers.restoreUnsaved(unsaved);

    for (const QString &documentId : m_buffers.dirtyDocuments()) {
        if (!before.contains(documentId)) {
            if (!m_recovered.contains(documentId)) {
                m_recovered.append(documentId);
            }
            emit dirtyChanged(documentId, libraryIdOf(documentId), true);
            emit documentReset(documentId);
            // The file may have changed while Archivist was down.
            reload(documentId);
        }
    }

    if (restored > 0) {
        emit recoveredDocumentsChanged();
        emit dirtyDocumentsChanged();
        bump();
    }

    return restored;
}
