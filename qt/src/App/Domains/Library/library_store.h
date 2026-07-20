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
    Q_PROPERTY(bool loadingLibraries READ loadingLibraries NOTIFY loadingLibrariesChanged)
    Q_PROPERTY(bool loadingFiles READ loadingFiles NOTIFY loadingFilesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit LibraryStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList libraries() const;
    [[nodiscard]] QString selectedLibraryId() const;
    [[nodiscard]] QVariantMap selectedLibrary() const;
    [[nodiscard]] QVariantList files() const;
    [[nodiscard]] QVariantMap latestScan() const;
    [[nodiscard]] bool loadingLibraries() const;
    [[nodiscard]] bool loadingFiles() const;
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectLibrary(const QString &libraryId);
    Q_INVOKABLE void refreshSelectedFiles();
    Q_INVOKABLE void scanSelectedLibrary();

signals:
    void librariesChanged();
    void selectedLibraryIdChanged();
    void selectedLibraryChanged();
    void filesChanged();
    void latestScanChanged();
    void loadingLibrariesChanged();
    void loadingFilesChanged();
    void scanningChanged();
    void errorMessageChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void setLibraries(const QVariantList &libraries);
    void setSelectedLibraryId(const QString &libraryId);
    void setFiles(const QVariantList &files);
    void setLatestScan(const QVariantMap &latestScan);
    void setLoadingLibraries(bool loading);
    void setLoadingFiles(bool loading);
    void setScanning(bool scanning);
    void setErrorMessage(const QString &message);
    [[nodiscard]] bool containsLibrary(const QString &libraryId) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_libraries;
    QString m_selectedLibraryId;
    QVariantList m_files;
    QVariantMap m_latestScan;
    bool m_loadingLibraries = false;
    bool m_loadingFiles = false;
    bool m_scanning = false;
    QString m_errorMessage;
};
