#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
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
    Q_PROPERTY(QVariantMap latestScan READ latestScan NOTIFY latestScanChanged)
    Q_PROPERTY(QString selectedFileId READ selectedFileId NOTIFY selectedFileIdChanged)
    Q_PROPERTY(QVariantMap selectedFile READ selectedFile NOTIFY selectedFileChanged)
    Q_PROPERTY(QVariantMap filePreview READ filePreview NOTIFY filePreviewChanged)
    Q_PROPERTY(bool loadingLibraries READ loadingLibraries NOTIFY loadingLibrariesChanged)
    Q_PROPERTY(bool loadingFiles READ loadingFiles NOTIFY loadingFilesChanged)
    Q_PROPERTY(bool loadingFilePreview READ loadingFilePreview NOTIFY loadingFilePreviewChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool movingFile READ movingFile NOTIFY movingFileChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString filePreviewError READ filePreviewError NOTIFY filePreviewErrorChanged)

public:
    explicit LibraryStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList libraries() const;
    [[nodiscard]] QString selectedLibraryId() const;
    [[nodiscard]] QVariantMap selectedLibrary() const;
    [[nodiscard]] QVariantList files() const;
    [[nodiscard]] QVariantMap latestScan() const;
    [[nodiscard]] QString selectedFileId() const;
    [[nodiscard]] QVariantMap selectedFile() const;
    [[nodiscard]] QVariantMap filePreview() const;
    [[nodiscard]] bool loadingLibraries() const;
    [[nodiscard]] bool loadingFiles() const;
    [[nodiscard]] bool loadingFilePreview() const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] bool movingFile() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString filePreviewError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectLibrary(const QString &libraryId);
    Q_INVOKABLE void refreshSelectedFiles();
    Q_INVOKABLE void scanSelectedLibrary();
    Q_INVOKABLE void moveFile(const QString &fileId, const QString &targetDirectory);
    Q_INVOKABLE void previewFile(const QString &fileId);
    Q_INVOKABLE void clearFilePreview();

signals:
    void librariesChanged();
    void selectedLibraryIdChanged();
    void selectedLibraryChanged();
    void filesChanged();
    void latestScanChanged();
    void selectedFileIdChanged();
    void selectedFileChanged();
    void filePreviewChanged();
    void loadingLibrariesChanged();
    void loadingFilesChanged();
    void loadingFilePreviewChanged();
    void scanningChanged();
    void movingFileChanged();
    void fileMoved(const QString &fileId, const QString &relativePath);
    void errorMessageChanged();
    void filePreviewErrorChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void setLibraries(const QVariantList &libraries);
    void setSelectedLibraryId(const QString &libraryId);
    void setFiles(const QVariantList &files);
    void setLatestScan(const QVariantMap &latestScan);
    void setSelectedFileId(const QString &fileId);
    void setFilePreview(const QVariantMap &preview);
    void setLoadingLibraries(bool loading);
    void setLoadingFiles(bool loading);
    void setLoadingFilePreview(bool loading);
    void setScanning(bool scanning);
    void setMovingFile(bool moving);
    void setErrorMessage(const QString &message);
    void setFilePreviewError(const QString &message);
    [[nodiscard]] bool containsLibrary(const QString &libraryId) const;
    [[nodiscard]] bool containsFile(const QString &fileId) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_libraries;
    QString m_selectedLibraryId;
    QVariantList m_files;
    QVariantMap m_latestScan;
    QString m_selectedFileId;
    QVariantMap m_filePreview;
    bool m_loadingLibraries = false;
    bool m_loadingFiles = false;
    bool m_loadingFilePreview = false;
    bool m_scanning = false;
    bool m_movingFile = false;
    QString m_errorMessage;
    QString m_filePreviewError;
};
