#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// Canonical text of every open document, owned natively instead of by an
// editor page.
//
// Editor views (Monaco pages, one per Anchor or window) hold copies and keep
// them in sync through versioned edits. Because the buffer lives here, a view
// can be moved, rebuilt in another window or duplicated without losing or
// forking unsaved text, and a document's worktree (Library) is fixed by its
// id, never by which window has focus.
//
// Versions: every accepted edit bumps `version`. A view sends edits against
// the version it last saw; an edit against an older version is refused as a
// conflict and the view resyncs from `text`. One person types into one view at
// a time, so refusing and resyncing is enough; no operational transform.
namespace Documents
{
// One replacement, in UTF-16 code units (what Monaco and QString both use),
// relative to the text at the edit's base version.
struct TextChange
{
    qsizetype offset = 0;
    qsizetype length = 0; // characters replaced
    QString text;
};

struct DocumentSeed
{
    QString documentId; // collectionId:libraryId:fileId
    QString collectionId;
    QString libraryId;
    QString fileId;
    QString path;
    QString language;
    QString diskText;
    QString diskModifiedAt;
    bool readOnly = false;
};

struct Buffer
{
    QString documentId;
    QString collectionId;
    QString libraryId;
    QString fileId;
    QString path;
    QString language;
    QString text;
    QString savedText; // what is on disk as far as Archivist knows
    QString diskModifiedAt;
    qint64 version = 1;
    qint64 savedVersion = 1;
    bool readOnly = false;
    bool diskChanged = false; // disk moved on under unsaved edits
    QSet<QString> holders;    // view ids

    [[nodiscard]] bool dirty() const { return version != savedVersion && text != savedText; }
};

struct EditResult
{
    bool ok = false;
    bool conflict = false; // base version was stale; resync from text/version
    QString error;
    qint64 version = 0;
};

class DocumentBuffers
{
public:
    // Registers `viewId` as showing the document. An already open buffer
    // keeps its text (possibly unsaved); the seed is used only the first
    // time. Returns the buffer.
    const Buffer &open(const DocumentSeed &seed, const QString &viewId);
    // The view stopped showing the document. A clean buffer nobody holds is
    // dropped; a dirty one is kept until saved or discarded. Returns true
    // when the buffer was dropped.
    bool release(const QString &documentId, const QString &viewId);
    // Drops every hold `viewId` has (the view closed).
    void releaseView(const QString &viewId);

    EditResult applyEdit(const QString &documentId, qint64 baseVersion, QList<TextChange> changes);
    EditResult replaceText(const QString &documentId, qint64 baseVersion, const QString &text);

    // The text at `version` reached disk.
    bool markSaved(const QString &documentId, qint64 version, const QString &savedText, const QString &modifiedAt);
    // Disk changed outside Archivist. Clean buffers take the new text; dirty
    // ones keep theirs and are flagged.
    bool reloadFromDisk(const QString &documentId, const QString &text, const QString &modifiedAt);
    // Keeps the unsaved text but takes `text` as what is now on disk, so a
    // later save overwrites the outside change on purpose.
    bool adoptDiskBase(const QString &documentId, const QString &text, const QString &modifiedAt);
    // Throws away unsaved edits.
    bool discard(const QString &documentId);
    // Repoints a buffer after a rename or move inside its Library.
    bool rename(const QString &documentId, const QString &path);

    [[nodiscard]] const Buffer *find(const QString &documentId) const;
    [[nodiscard]] bool contains(const QString &documentId) const { return m_buffers.contains(documentId); }
    [[nodiscard]] bool isDirty(const QString &documentId) const;
    [[nodiscard]] QStringList dirtyDocuments() const;
    [[nodiscard]] QStringList dirtyDocumentsHeldOnlyBy(const QSet<QString> &viewIds) const;
    [[nodiscard]] QStringList documentIds() const;

    // Unsaved buffers, for crash recovery. Restoring marks them dirty again
    // against the disk state they were based on.
    [[nodiscard]] QJsonArray encodeUnsaved() const;
    int restoreUnsaved(const QJsonArray &unsaved);

private:
    QHash<QString, Buffer> m_buffers;
};
} // namespace Documents
