#include "Core/Domains/Documents/document_buffers.h"

#include <QJsonObject>

#include <algorithm>

namespace Documents
{
const Buffer &DocumentBuffers::open(const DocumentSeed &seed, const QString &viewId)
{
    auto it = m_buffers.find(seed.documentId);

    if (it == m_buffers.end()) {
        Buffer buffer;
        buffer.documentId = seed.documentId;
        buffer.collectionId = seed.collectionId;
        buffer.libraryId = seed.libraryId;
        buffer.fileId = seed.fileId;
        buffer.path = seed.path;
        buffer.language = seed.language;
        buffer.text = seed.diskText;
        buffer.savedText = seed.diskText;
        buffer.diskModifiedAt = seed.diskModifiedAt;
        buffer.readOnly = seed.readOnly;
        it = m_buffers.insert(seed.documentId, buffer);
    }

    if (!viewId.isEmpty()) {
        it->holders.insert(viewId);
    }

    return it.value();
}

bool DocumentBuffers::release(const QString &documentId, const QString &viewId)
{
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        return false;
    }

    it->holders.remove(viewId);

    if (it->holders.isEmpty() && !it->dirty()) {
        m_buffers.erase(it);
        return true;
    }

    return false;
}

void DocumentBuffers::releaseView(const QString &viewId)
{
    const QStringList ids = m_buffers.keys();

    for (const QString &documentId : ids) {
        if (m_buffers.value(documentId).holders.contains(viewId)) {
            release(documentId, viewId);
        }
    }
}

EditResult DocumentBuffers::applyEdit(const QString &documentId, qint64 baseVersion, QList<TextChange> changes)
{
    EditResult result;
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        result.error = QStringLiteral("The document is not open.");
        return result;
    }

    result.version = it->version;

    if (it->readOnly) {
        result.error = QStringLiteral("The document is read-only.");
        return result;
    }

    if (baseVersion != it->version) {
        result.conflict = true;
        result.error = QStringLiteral("The editor is behind; resync.");
        return result;
    }

    // Apply from the end so earlier offsets stay valid.
    std::sort(changes.begin(), changes.end(), [](const TextChange &a, const TextChange &b) {
        return a.offset > b.offset;
    });

    QString text = it->text;
    qsizetype floor = text.size() + 1;

    for (const TextChange &change : changes) {
        if (change.offset < 0 || change.length < 0 || change.offset + change.length > text.size()
            || change.offset + change.length > floor) {
            result.conflict = true;
            result.error = QStringLiteral("The edit does not fit the document; resync.");
            return result;
        }
        text.replace(change.offset, change.length, change.text);
        floor = change.offset;
    }

    it->text = text;
    it->version += 1;
    result.ok = true;
    result.version = it->version;
    return result;
}

EditResult DocumentBuffers::replaceText(const QString &documentId, qint64 baseVersion, const QString &text)
{
    EditResult result;
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        result.error = QStringLiteral("The document is not open.");
        return result;
    }

    result.version = it->version;

    if (it->readOnly) {
        result.error = QStringLiteral("The document is read-only.");
        return result;
    }

    if (baseVersion != it->version) {
        result.conflict = true;
        result.error = QStringLiteral("The editor is behind; resync.");
        return result;
    }

    if (text != it->text) {
        it->text = text;
        it->version += 1;
    }

    result.ok = true;
    result.version = it->version;
    return result;
}

bool DocumentBuffers::markSaved(const QString &documentId, qint64 version, const QString &savedText, const QString &modifiedAt)
{
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end() || version > it->version) {
        return false;
    }

    it->savedText = savedText;
    it->savedVersion = version;
    it->diskModifiedAt = modifiedAt;
    it->diskChanged = false;

    if (it->holders.isEmpty() && !it->dirty()) {
        m_buffers.erase(it);
    }

    return true;
}

bool DocumentBuffers::reloadFromDisk(const QString &documentId, const QString &text, const QString &modifiedAt)
{
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        return false;
    }

    if (it->dirty()) {
        it->diskChanged = text != it->savedText;
        return false;
    }

    it->savedText = text;
    it->diskModifiedAt = modifiedAt;

    if (it->text != text) {
        it->text = text;
        it->version += 1;
    }

    it->savedVersion = it->version;
    it->diskChanged = false;
    return true;
}

bool DocumentBuffers::discard(const QString &documentId)
{
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        return false;
    }

    if (it->text != it->savedText) {
        it->text = it->savedText;
        it->version += 1;
    }

    it->savedVersion = it->version;
    it->diskChanged = false;

    if (it->holders.isEmpty()) {
        m_buffers.erase(it);
    }

    return true;
}

bool DocumentBuffers::rename(const QString &documentId, const QString &path)
{
    auto it = m_buffers.find(documentId);

    if (it == m_buffers.end()) {
        return false;
    }

    it->path = path;
    return true;
}

const Buffer *DocumentBuffers::find(const QString &documentId) const
{
    const auto it = m_buffers.constFind(documentId);
    return it == m_buffers.constEnd() ? nullptr : &it.value();
}

bool DocumentBuffers::isDirty(const QString &documentId) const
{
    const Buffer *buffer = find(documentId);
    return buffer && buffer->dirty();
}

QStringList DocumentBuffers::dirtyDocuments() const
{
    QStringList result;

    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it) {
        if (it->dirty()) {
            result.append(it.key());
        }
    }

    result.sort();
    return result;
}

QStringList DocumentBuffers::dirtyDocumentsHeldOnlyBy(const QSet<QString> &viewIds) const
{
    QStringList result;

    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it) {
        if (it->dirty() && !it->holders.isEmpty() && viewIds.contains(it->holders)) {
            result.append(it.key());
        }
    }

    result.sort();
    return result;
}

QStringList DocumentBuffers::documentIds() const
{
    QStringList result = m_buffers.keys();
    result.sort();
    return result;
}

QJsonArray DocumentBuffers::encodeUnsaved() const
{
    QJsonArray result;

    for (const QString &documentId : dirtyDocuments()) {
        const Buffer &buffer = m_buffers[documentId];
        result.append(QJsonObject{
            {QStringLiteral("documentId"), buffer.documentId},
            {QStringLiteral("collectionId"), buffer.collectionId},
            {QStringLiteral("libraryId"), buffer.libraryId},
            {QStringLiteral("fileId"), buffer.fileId},
            {QStringLiteral("path"), buffer.path},
            {QStringLiteral("language"), buffer.language},
            {QStringLiteral("text"), buffer.text},
            {QStringLiteral("savedText"), buffer.savedText},
            {QStringLiteral("diskModifiedAt"), buffer.diskModifiedAt},
        });
    }

    return result;
}

int DocumentBuffers::restoreUnsaved(const QJsonArray &unsaved)
{
    int restored = 0;

    for (const QJsonValue &value : unsaved) {
        const QJsonObject object = value.toObject();
        const QString documentId = object.value(QStringLiteral("documentId")).toString();

        if (documentId.isEmpty() || isDirty(documentId)) {
            continue;
        }

        Buffer buffer;
        buffer.documentId = documentId;
        buffer.collectionId = object.value(QStringLiteral("collectionId")).toString();
        buffer.libraryId = object.value(QStringLiteral("libraryId")).toString();
        buffer.fileId = object.value(QStringLiteral("fileId")).toString();
        buffer.path = object.value(QStringLiteral("path")).toString();
        buffer.language = object.value(QStringLiteral("language")).toString();
        buffer.text = object.value(QStringLiteral("text")).toString();
        buffer.savedText = object.value(QStringLiteral("savedText")).toString();
        buffer.diskModifiedAt = object.value(QStringLiteral("diskModifiedAt")).toString();
        buffer.version = 2;
        buffer.savedVersion = 1;

        if (const Buffer *existing = find(documentId)) {
            buffer.holders = existing->holders;
        }

        if (buffer.text == buffer.savedText) {
            continue;
        }

        m_buffers.insert(documentId, buffer);
        ++restored;
    }

    return restored;
}
} // namespace Documents
