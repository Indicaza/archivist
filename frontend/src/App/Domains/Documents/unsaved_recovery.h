#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class DocumentStore;

// Keeps unsaved editor text on disk so a crash, a kill or a power cut never
// loses it. The file is rewritten (atomically) shortly after edits settle
// and removed once nothing is unsaved. On the next start, restore() puts the
// text back into the document buffers, marked as unsaved.
class UnsavedRecovery final : public QObject
{
    Q_OBJECT

public:
    UnsavedRecovery(DocumentStore *documents, QString filePath, QObject *parent = nullptr);

    // Reads the recovery file into the buffers. Returns how many documents
    // came back.
    int restore();
    // Writes now (used on quit and by tests).
    void flush();

    [[nodiscard]] QString filePath() const { return m_filePath; }

private:
    void schedule();

    DocumentStore *m_documents = nullptr;
    QString m_filePath;
    QTimer m_timer;
    QByteArray m_written;
};
