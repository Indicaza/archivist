#pragma once

#include <QFileSystemWatcher>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class LibraryStore final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged)
    Q_PROPERTY(QString selectedLibraryId READ selectedLibraryId NOTIFY selectedLibraryIdChanged)
    Q_PROPERTY(QVariantMap selectedLibrary READ selectedLibrary NOTIFY selectedLibraryChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QVariantList directories READ directories NOTIFY directoriesChanged)
    Q_PROPERTY(QVariantMap latestScan READ latestScan NOTIFY latestScanChanged)
    Q_PROPERTY(QString selectedFileId READ selectedFileId NOTIFY selectedFileIdChanged)
    Q_PROPERTY(QVariantMap selectedFile READ selectedFile NOTIFY selectedFileChanged)
    Q_PROPERTY(QVariantMap filePreview READ filePreview NOTIFY filePreviewChanged)
    Q_PROPERTY(bool loadingLibraries READ loadingLibraries NOTIFY loadingLibrariesChanged)
    Q_PROPERTY(bool loadingFiles READ loadingFiles NOTIFY loadingFilesChanged)
    Q_PROPERTY(bool loadingFilePreview READ loadingFilePreview NOTIFY loadingFilePreviewChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool movingFile READ movingFile NOTIFY movingFileChanged)
    Q_PROPERTY(bool mutatingEntry READ mutatingEntry NOTIFY mutatingEntryChanged)
    Q_PROPERTY(bool savingFile READ savingFile NOTIFY savingFileChanged)
    Q_PROPERTY(bool creatingLibrary READ creatingLibrary NOTIFY creatingLibraryChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString filePreviewError READ filePreviewError NOTIFY filePreviewErrorChanged)
    Q_PROPERTY(QString fileSaveError READ fileSaveError NOTIFY fileSaveErrorChanged)

public:
    explicit LibraryStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList libraries() const;
    [[nodiscard]] QString selectedLibraryId() const;
    [[nodiscard]] QVariantMap selectedLibrary() const;
    [[nodiscard]] QVariantList files() const;
    [[nodiscard]] QVariantList directories() const;
    [[nodiscard]] QVariantMap latestScan() const;
    [[nodiscard]] QString selectedFileId() const;
    [[nodiscard]] QVariantMap selectedFile() const;
    [[nodiscard]] QVariantMap filePreview() const;
    [[nodiscard]] bool loadingLibraries() const;
    [[nodiscard]] bool loadingFiles() const;
    [[nodiscard]] bool loadingFilePreview() const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] bool movingFile() const;
    [[nodiscard]] bool mutatingEntry() const;
    [[nodiscard]] bool savingFile() const;
    [[nodiscard]] bool creatingLibrary() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString filePreviewError() const;
    [[nodiscard]] QString fileSaveError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void createLibrary(const QUrl &folderUrl);
    Q_INVOKABLE void selectLibrary(const QString &libraryId);
    Q_INVOKABLE void selectLibraryAndScan(const QString &libraryId);
    Q_INVOKABLE void refreshSelectedFiles();
    Q_INVOKABLE void scanSelectedLibrary();
    Q_INVOKABLE void moveFile(const QString &fileId, const QString &targetDirectory);
    Q_INVOKABLE void createEntry(
        const QString &parentDirectory,
        const QString &name,
        bool directory
    );
    Q_INVOKABLE void renameFile(const QString &fileId, const QString &name);
    Q_INVOKABLE void duplicateFile(const QString &fileId, const QString &name);
    Q_INVOKABLE void revealEntry(const QString &relativePath);
    Q_INVOKABLE void previewFile(const QString &fileId);
    Q_INVOKABLE void saveFileContent(
        const QString &fileId,
        const QString &content,
        const QString &expectedModifiedAt
    );
    Q_INVOKABLE void clearFilePreview();

signals:
    void librariesChanged();
    void selectedLibraryIdChanged();
    void selectedLibraryChanged();
    void filesChanged();
    void directoriesChanged();
    void latestScanChanged();
    void selectedFileIdChanged();
    void selectedFileChanged();
    void filePreviewChanged();
    void loadingLibrariesChanged();
    void loadingFilesChanged();
    void loadingFilePreviewChanged();
    void scanningChanged();
    void movingFileChanged();
    void mutatingEntryChanged();
    void savingFileChanged();
    void creatingLibraryChanged();
    void libraryCreated(const QVariantMap &library);
    void fileMoved(const QString &fileId, const QString &relativePath);
    void entryCreated(
        const QString &kind,
        const QString &relativePath,
        const QString &fileId
    );
    void fileRenamed(
        const QString &fileId,
        const QString &relativePath,
        const QString &name
    );
    void fileDuplicated(
        const QString &fileId,
        const QString &relativePath,
        const QString &name
    );
    void entryRevealed(const QString &relativePath);
    void fileSaved(
        const QString &libraryId,
        const QString &fileId,
        const QVariantMap &preview
    );
    void fileSaveFailed(
        const QString &libraryId,
        const QString &fileId,
        const QString &message
    );
    void errorMessageChanged();
    void filePreviewErrorChanged();
    void fileSaveErrorChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void startLibrarySelection(const QString &libraryId);
    void rebuildFileWatchers();
    void scheduleExternalFileReload();
    void scheduleLibraryRescan();
    [[nodiscard]] QString selectedLibraryRootPath() const;
    [[nodiscard]] QString selectedFileAbsolutePath() const;
    void setLibraries(const QVariantList &libraries);
    void setSelectedLibraryId(const QString &libraryId);
    void setFiles(const QVariantList &files);
    void setDirectories(const QVariantList &directories);
    void setLatestScan(const QVariantMap &latestScan);
    void setSelectedFileId(const QString &fileId);
    void setFilePreview(const QVariantMap &preview);
    void setLoadingLibraries(bool loading);
    void setLoadingFiles(bool loading);
    void setLoadingFilePreview(bool loading);
    void setScanning(bool scanning);
    void setMovingFile(bool moving);
    void setMutatingEntry(bool mutating);
    void setSavingFile(bool saving);
    void setCreatingLibrary(bool creating);
    void setErrorMessage(const QString &message);
    void setFilePreviewError(const QString &message);
    void setFileSaveError(const QString &message);
    void upsertLibrary(const QVariantMap &library);
    [[nodiscard]] bool containsLibrary(const QString &libraryId) const;
    [[nodiscard]] bool containsFile(const QString &fileId) const;

    QNetworkAccessManager m_network;
    QFileSystemWatcher m_fileWatcher;
    QTimer m_externalFileReloadTimer;
    QTimer m_libraryRescanTimer;
    QTimer m_watcherSuppressionTimer;
    QUrl m_baseUrl;
    QVariantList m_libraries;
    QString m_selectedLibraryId;
    QString m_pendingScanLibraryId;
    QString m_pendingLibrarySelectionId;
    QString m_queuedLibrarySelectionId;
    quint64 m_fileRequestRevision = 0;
    QVariantList m_files;
    QVariantList m_directories;
    QVariantMap m_latestScan;
    QString m_selectedFileId;
    QVariantMap m_filePreview;
    bool m_loadingLibraries = false;
    bool m_loadingFiles = false;
    bool m_loadingFilePreview = false;
    bool m_scanning = false;
    bool m_movingFile = false;
    bool m_mutatingEntry = false;
    bool m_savingFile = false;
    bool m_creatingLibrary = false;
    QString m_errorMessage;
    QString m_filePreviewError;
    QString m_fileSaveError;
    QString m_fileToReloadAfterScanId;
};
