#include "library_store.h"

#include "Core/Domains/AppState/app_state_repository.h"
#include "Core/Domains/Libraries/library_file_operations.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/core_runtime.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QSet>

namespace
{
// The file catalog plus one extra key, the same shape the Node routes returned.
QVariant withCatalog(CoreDatabase &database, const QString &libraryId, const QString &key, const QVariant &value)
{
    QVariantMap response = LibraryFiles::catalogWithDirectories(database, libraryId);
    response.insert(key, value);
    return response;
}

QString coreUnavailableMessage()
{
    return QStringLiteral("Archivist's data layer is not running. Restart the app.");
}

bool usesDirectImageRenderer(const QVariantMap &file)
{
    const QString fileName = file.value(QStringLiteral("name")).toString();
    const QString mimeType = QMimeDatabase()
        .mimeTypeForFile(fileName, QMimeDatabase::MatchExtension)
        .name();

    return mimeType.startsWith(QStringLiteral("image/"))
        && mimeType != QStringLiteral("image/svg+xml");
}

bool usesDocumentRenderer(const QVariantMap &file)
{
    static const QSet<QString> extensions{
        QStringLiteral("pdf"),
        QStringLiteral("doc"),
        QStringLiteral("docx"),
        QStringLiteral("odt"),
        QStringLiteral("rtf"),
        QStringLiteral("xls"),
        QStringLiteral("xlsx"),
        QStringLiteral("ods"),
        QStringLiteral("ppt"),
        QStringLiteral("pptx"),
        QStringLiteral("odp"),
    };

    QString extension = file.value(QStringLiteral("extension")).toString().toLower();
    if (extension.startsWith(QLatin1Char('.'))) {
        extension.remove(0, 1);
    }

    return extensions.contains(extension);
}
}

LibraryStore::LibraryStore(QObject *parent)
    : QObject(parent)
{
    m_externalFileReloadTimer.setSingleShot(true);
    m_externalFileReloadTimer.setInterval(220);
    m_libraryRescanTimer.setSingleShot(true);
    m_libraryRescanTimer.setInterval(900);
    m_gitStatusRefreshTimer.setSingleShot(true);
    m_gitStatusRefreshTimer.setInterval(320);
    m_watcherSuppressionTimer.setSingleShot(true);
    m_watcherSuppressionTimer.setInterval(1200);

    connect(
        &m_fileWatcher,
        &QFileSystemWatcher::fileChanged,
        this,
        [this](const QString &) {
            if (m_watcherSuppressionTimer.isActive()) {
                return;
            }

            scheduleExternalFileReload();
            scheduleLibraryRescan();
            scheduleGitStatusRefresh();
            QTimer::singleShot(
                0,
                this,
                &LibraryStore::rebuildFileWatchers
            );
        }
    );

    connect(
        &m_fileWatcher,
        &QFileSystemWatcher::directoryChanged,
        this,
        [this](const QString &) {
            if (m_watcherSuppressionTimer.isActive()) {
                return;
            }

            scheduleExternalFileReload();
            scheduleLibraryRescan();
            scheduleGitStatusRefresh();
        }
    );

    connect(
        &m_externalFileReloadTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (
                m_selectedFileId.isEmpty()
                || m_loadingFilePreview
                || m_savingFile
            ) {
                return;
            }

            previewFileFromLibrary(
                m_activeFileLibraryId,
                m_selectedFileId,
                m_selectedFile
            );
        }
    );

    connect(
        &m_libraryRescanTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_selectedLibraryId.isEmpty()) {
                return;
            }

            if (m_scanning) {
                m_libraryRescanTimer.start();
                return;
            }

            scanSelectedLibrary();
        }
    );

    connect(
        &m_gitStatusRefreshTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_scanning) {
                m_gitStatusRefreshTimer.start();
                return;
            }

            refreshSelectedGitStatus();
        }
    );
}

QVariantList LibraryStore::libraries() const
{
    return m_libraries;
}

QString LibraryStore::selectedLibraryId() const
{
    return m_selectedLibraryId;
}

QVariantMap LibraryStore::selectedLibrary() const
{
    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (library.value(QStringLiteral("id")).toString() == m_selectedLibraryId) {
            return library;
        }
    }

    return {};
}

QVariantList LibraryStore::files() const
{
    return m_files;
}

QVariantList LibraryStore::directories() const
{
    return m_directories;
}

QVariantMap LibraryStore::latestScan() const
{
    return m_latestScan;
}

QVariantMap LibraryStore::gitStatus() const
{
    return m_gitStatus;
}

bool LibraryStore::loadingGitStatus() const
{
    return m_loadingGitStatus;
}

QString LibraryStore::activeFileLibraryId() const
{
    return m_activeFileLibraryId;
}

QVariantMap LibraryStore::activeFileLibrary() const
{
    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (
            library.value(QStringLiteral("id")).toString()
            == m_activeFileLibraryId
        ) {
            return library;
        }
    }

    return {};
}

QString LibraryStore::activeFileRootPath() const
{
    return activeFileLibrary()
        .value(QStringLiteral("rootPath"))
        .toString();
}

QString LibraryStore::selectedFileId() const
{
    return m_selectedFileId;
}

QVariantMap LibraryStore::selectedFile() const
{
    return m_selectedFile;
}

QVariantMap LibraryStore::filePreview() const
{
    return m_filePreview;
}

bool LibraryStore::loadingLibraries() const
{
    return m_loadingLibraries;
}

bool LibraryStore::loadingFiles() const
{
    return m_loadingFiles;
}

bool LibraryStore::loadingFilePreview() const
{
    return m_loadingFilePreview;
}

bool LibraryStore::scanning() const
{
    return m_scanning;
}

bool LibraryStore::movingFile() const
{
    return m_movingFile;
}

bool LibraryStore::mutatingEntry() const
{
    return m_mutatingEntry;
}

bool LibraryStore::savingFile() const
{
    return m_savingFile;
}

bool LibraryStore::creatingLibrary() const
{
    return m_creatingLibrary;
}

QString LibraryStore::errorMessage() const
{
    return m_errorMessage;
}

QString LibraryStore::filePreviewError() const
{
    return m_filePreviewError;
}

QString LibraryStore::fileSaveError() const
{
    return m_fileSaveError;
}

QString LibraryStore::selectedLibraryRootPath() const
{
    return selectedLibrary()
        .value(QStringLiteral("rootPath"))
        .toString();
}

QString LibraryStore::selectedFileAbsolutePath() const
{
    const QString rootPath = activeFileRootPath();
    const QVariantMap file = selectedFile();

    if (rootPath.isEmpty() || file.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(
        QDir(rootPath).filePath(
            file.value(QStringLiteral("relativePath")).toString()
        )
    );
}

void LibraryStore::rebuildFileWatchers()
{
    const QStringList watchedFiles = m_fileWatcher.files();
    const QStringList watchedDirectories =
        m_fileWatcher.directories();

    if (!watchedFiles.isEmpty()) {
        m_fileWatcher.removePaths(watchedFiles);
    }

    if (!watchedDirectories.isEmpty()) {
        m_fileWatcher.removePaths(watchedDirectories);
    }

    const QString rootPath = selectedLibraryRootPath();

    if (rootPath.isEmpty()) {
        return;
    }

    QSet<QString> directories;
    const QFileInfo rootInfo(rootPath);

    if (rootInfo.exists() && rootInfo.isDir()) {
        directories.insert(rootInfo.absoluteFilePath());
    }

    for (const QVariant &value : m_files) {
        const QVariantMap file = value.toMap();
        const QString relativePath =
            file.value(QStringLiteral("relativePath")).toString();
        const QFileInfo fileInfo(
            QDir(rootPath).filePath(relativePath)
        );

        if (fileInfo.absoluteDir().exists()) {
            directories.insert(
                fileInfo.absolutePath()
            );
        }
    }

    if (!directories.isEmpty()) {
        m_fileWatcher.addPaths(directories.values());
    }

    const QString filePath = selectedFileAbsolutePath();
    const QFileInfo selectedFileInfo(filePath);

    if (
        !filePath.isEmpty()
        && selectedFileInfo.exists()
        && selectedFileInfo.isFile()
    ) {
        m_fileWatcher.addPath(filePath);
    }
}

void LibraryStore::scheduleExternalFileReload()
{
    if (m_selectedFileId.isEmpty()) {
        return;
    }

    m_externalFileReloadTimer.start();
}

void LibraryStore::scheduleLibraryRescan()
{
    if (m_selectedLibraryId.isEmpty()) {
        return;
    }

    m_libraryRescanTimer.start();
}

void LibraryStore::scheduleGitStatusRefresh()
{
    if (m_selectedLibraryId.isEmpty()) {
        return;
    }

    m_gitStatusRefreshTimer.start();
}

void LibraryStore::refresh()
{
    if (m_loadingLibraries) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(QStringLiteral("Archivist's data layer is not running. Restart the app."));
        return;
    }

    setErrorMessage({});
    setLoadingLibraries(true);

    core->read(
        [](CoreDatabase &database) { return QVariant(LibraryRepository::activeLibraries(database)); },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                setLoadingLibraries(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            setLibraries(result.value.toList());
            fetchAppState();
        }
    );
}

void LibraryStore::createLibrary(const QUrl &folderUrl)
{
    if (m_creatingLibrary || !folderUrl.isValid()) {
        return;
    }

    const QString rootPath = folderUrl.isLocalFile()
        ? folderUrl.toLocalFile()
        : folderUrl.toString();

    if (rootPath.trimmed().isEmpty()) {
        setErrorMessage(QStringLiteral("Select a folder to create a Library."));
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(QStringLiteral("Archivist's data layer is not running. Restart the app."));
        return;
    }

    setErrorMessage({});
    setCreatingLibrary(true);

    const QVariantMap input{{QStringLiteral("rootPath"), rootPath}};

    core->write(
        [input](CoreDatabase &database) { return QVariant(LibraryRepository::createLibrary(database, input)); },
        this,
        [this](const CoreResult &result) {
            setCreatingLibrary(false);

            if (!result.ok) {
                if (result.errorDetails.value(QStringLiteral("code")).toString() == QStringLiteral("LIBRARY_EXISTS")) {
                    const QString existingLibraryId =
                        result.errorDetails.value(QStringLiteral("libraryId")).toString();

                    for (const QVariant &value : m_libraries) {
                        const QVariantMap library = value.toMap();

                        if (library.value(QStringLiteral("id")).toString() == existingLibraryId) {
                            setErrorMessage({});
                            emit libraryCreated(library);
                            return;
                        }
                    }
                }

                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap library = result.value.toMap().value(QStringLiteral("library")).toMap();

            if (library.isEmpty()) {
                setErrorMessage(QStringLiteral("Archivist created the Library but returned no Library data."));
                return;
            }

            upsertLibrary(library);
            emit libraryCreated(library);
        }
    );
}

void LibraryStore::fetchAppState()
{
    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoadingLibraries(false);
        setErrorMessage(QStringLiteral("Archivist's data layer is not running. Restart the app."));
        return;
    }

    core->read(
        [](CoreDatabase &database) {
            return QVariant(AppStateRepository::appState(database));
        },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                setLoadingLibraries(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            QString nextLibraryId = result.value.toMap()
                .value(QStringLiteral("selectedLibraryId")).toString();

            if (!containsLibrary(nextLibraryId)) {
                nextLibraryId.clear();
            }

            setLoadingLibraries(false);

            if (nextLibraryId.isEmpty() && !m_libraries.isEmpty()) {
                selectLibrary(m_libraries.first().toMap().value(QStringLiteral("id")).toString());
                return;
            }

            setSelectedLibraryId(nextLibraryId);
            refreshSelectedFiles();
        }
    );
}

void LibraryStore::selectLibrary(const QString &libraryId)
{
    if (libraryId.isEmpty() || !containsLibrary(libraryId)) {
        return;
    }

    if (!m_pendingLibrarySelectionId.isEmpty()) {
        if (libraryId == m_pendingLibrarySelectionId) {
            m_queuedLibrarySelectionId.clear();
        } else {
            m_queuedLibrarySelectionId = libraryId;
        }
        return;
    }

    if (libraryId == m_selectedLibraryId) {
        if (m_pendingScanLibraryId == libraryId) {
            m_pendingScanLibraryId.clear();
            scanSelectedLibrary();
            return;
        }

        refreshSelectedFiles();
        return;
    }

    startLibrarySelection(libraryId);
}

void LibraryStore::startLibrarySelection(const QString &libraryId)
{
    m_pendingLibrarySelectionId = libraryId;
    m_queuedLibrarySelectionId.clear();
    ++m_fileRequestRevision;
    ++m_gitStatusRequestRevision;
    m_gitStatusRefreshTimer.stop();

    setErrorMessage({});
    setFiles({});
    setLatestScan({});
    setGitStatus({});
    setLoadingGitStatus(false);
    setLoadingFiles(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        m_pendingLibrarySelectionId.clear();
        setLoadingFiles(false);
        setErrorMessage(QStringLiteral("Archivist's data layer is not running. Restart the app."));
        return;
    }

    core->write(
        [libraryId](CoreDatabase &database) {
            return QVariant(AppStateRepository::setSelectedLibrary(database, libraryId));
        },
        this,
        [this, libraryId](const CoreResult &result) {
            if (m_pendingLibrarySelectionId != libraryId) {
                return;
            }

            m_pendingLibrarySelectionId.clear();

            if (!result.ok) {
                m_queuedLibrarySelectionId.clear();
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            const QString selectedLibraryId = result.value.toMap()
                .value(QStringLiteral("selectedLibraryId")).toString();

            if (selectedLibraryId != libraryId) {
                m_queuedLibrarySelectionId.clear();
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                setErrorMessage(
                    QStringLiteral("Archivist selected an unexpected Library.")
                );
                return;
            }

            setSelectedLibraryId(selectedLibraryId);

            if (
                !m_queuedLibrarySelectionId.isEmpty()
                && m_queuedLibrarySelectionId != selectedLibraryId
            ) {
                const QString queuedLibraryId = m_queuedLibrarySelectionId;
                m_queuedLibrarySelectionId.clear();
                startLibrarySelection(queuedLibraryId);
                return;
            }

            m_queuedLibrarySelectionId.clear();

            if (m_pendingScanLibraryId == selectedLibraryId) {
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                scanSelectedLibrary();
                return;
            }

            refreshSelectedFiles();
        }
    );
}

void LibraryStore::selectLibraryAndScan(const QString &libraryId)
{
    if (libraryId.isEmpty() || !containsLibrary(libraryId)) {
        return;
    }

    m_pendingScanLibraryId = libraryId;
    selectLibrary(libraryId);
}

void LibraryStore::refreshSelectedFiles()
{
    if (m_selectedLibraryId.isEmpty()) {
        ++m_fileRequestRevision;
        ++m_gitStatusRequestRevision;
        setFiles({});
        setDirectories({});
        setLatestScan({});
        setGitStatus({});
        setLoadingFiles(false);
        setLoadingGitStatus(false);
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_fileRequestRevision;

    setErrorMessage({});
    setLoadingFiles(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoadingFiles(false);
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    core->run(
        [requestedLibraryId](CoreDatabase &database) {
            return QVariant(LibraryFiles::catalogWithDirectories(database, requestedLibraryId));
        },
        this,
        [this, requestedLibraryId, requestRevision](const CoreResult &result) {
            if (
                requestRevision != m_fileRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            if (!result.ok) {
                setLoadingFiles(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap catalog = result.value.toMap();
            setFiles(catalog.value(QStringLiteral("files")).toList());
            setDirectories(catalog.value(QStringLiteral("directories")).toList());
            setLatestScan(catalog.value(QStringLiteral("latestScan")).toMap());
            setLoadingFiles(false);
            refreshSelectedGitStatus();
        }
    );
}

void LibraryStore::refreshSelectedGitStatus()
{
    if (m_selectedLibraryId.isEmpty()) {
        ++m_gitStatusRequestRevision;
        setGitStatus({});
        setLoadingGitStatus(false);
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_gitStatusRequestRevision;

    setLoadingGitStatus(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoadingGitStatus(false);
        setGitStatus({});
        return;
    }

    core->run(
        [requestedLibraryId](CoreDatabase &database) {
            return QVariant(LibraryFiles::gitStatus(database, requestedLibraryId));
        },
        this,
        [this, requestedLibraryId, requestRevision](const CoreResult &result) {
            if (
                requestRevision != m_gitStatusRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            setLoadingGitStatus(false);
            setGitStatus(result.ok ? result.value.toMap() : QVariantMap{});
        }
    );
}

void LibraryStore::scanSelectedLibrary()
{
    if (m_selectedLibraryId.isEmpty() || m_scanning) {
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_fileRequestRevision;

    ++m_gitStatusRequestRevision;
    m_gitStatusRefreshTimer.stop();
    setLoadingGitStatus(true);

    m_fileToReloadAfterScanId =
        m_activeFileLibraryId == requestedLibraryId
            ? m_selectedFileId
            : QString{};
    setErrorMessage({});
    setLoadingFiles(true);
    setScanning(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setScanning(false);
        setLoadingFiles(false);
        setLoadingGitStatus(false);
        setErrorMessage(coreUnavailableMessage());
        m_fileToReloadAfterScanId.clear();
        return;
    }

    core->run(
        [requestedLibraryId](CoreDatabase &database) {
            QVariantMap scan = LibraryFiles::scan(database, requestedLibraryId);
            scan.insert(
                QStringLiteral("directories"),
                QVariant(LibraryFiles::directories(database, requestedLibraryId))
            );

            try {
                scan.insert(QStringLiteral("gitStatus"), LibraryFiles::gitStatus(database, requestedLibraryId));
            } catch (const std::exception &) {
                scan.insert(QStringLiteral("gitStatus"), QVariantMap{});
            }

            return QVariant(scan);
        },
        this,
        [this, requestedLibraryId, requestRevision](const CoreResult &result) {
            setScanning(false);

            if (
                requestRevision != m_fileRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            if (!result.ok) {
                setLoadingFiles(false);
                setLoadingGitStatus(false);
                setErrorMessage(result.errorMessage);
                m_fileToReloadAfterScanId.clear();
                return;
            }

            const QVariantMap scanResult = result.value.toMap();
            setGitStatus(scanResult.value(QStringLiteral("gitStatus")).toMap());
            setLoadingGitStatus(false);
            setFiles(scanResult.value(QStringLiteral("files")).toList());
            setDirectories(scanResult.value(QStringLiteral("directories")).toList());

            QVariantMap scan = scanResult.value(QStringLiteral("latestScan")).toMap();
            if (scan.isEmpty()) {
                scan = scanResult.value(QStringLiteral("scan")).toMap();
            }
            setLatestScan(scan);
            setLoadingFiles(false);
            rebuildFileWatchers();
            m_gitStatusRefreshTimer.stop();

            const QString fileIdToReload =
                m_fileToReloadAfterScanId;
            m_fileToReloadAfterScanId.clear();

            if (
                !fileIdToReload.isEmpty()
                && m_activeFileLibraryId == requestedLibraryId
                && containsFile(fileIdToReload)
            ) {
                previewFile(fileIdToReload);
            }
        }
    );
}

void LibraryStore::moveFile(const QString &fileId, const QString &targetDirectory)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || !containsFile(fileId)
        || m_movingFile
    ) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMovingFile(true);

    core->run(
        [libraryId, fileId, targetDirectory](CoreDatabase &database) {
            const QVariantMap file = LibraryFileOperations::moveFile(database, libraryId, fileId, targetDirectory);
            return withCatalog(database, libraryId, QStringLiteral("file"), file);
        },
        this,
        [this, libraryId, fileId](const CoreResult &result) {
            setMovingFile(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (m_selectedLibraryId != libraryId) {
                return;
            }

            const QVariantMap response = result.value.toMap();
            applyCatalog(response);
            refreshSelectedGitStatus();
            emit fileMoved(
                fileId,
                response.value(QStringLiteral("file")).toMap().value(QStringLiteral("relativePath")).toString()
            );
        }
    );
}

void LibraryStore::createEntry(
    const QString &parentDirectory,
    const QString &name,
    bool directory
)
{
    if (
        m_selectedLibraryId.isEmpty()
        || name.trimmed().isEmpty()
        || m_mutatingEntry
    ) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    core->run(
        [libraryId, parentDirectory, name, directory](CoreDatabase &database) {
            const QVariantMap entry = LibraryFileOperations::createEntry(database, libraryId, parentDirectory, name, directory);
            return withCatalog(database, libraryId, QStringLiteral("entry"), entry);
        },
        this,
        [this, libraryId](const CoreResult &result) {
            setMutatingEntry(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (m_selectedLibraryId != libraryId) {
                return;
            }

            const QVariantMap response = result.value.toMap();
            const QVariantMap entry = response.value(QStringLiteral("entry")).toMap();
            applyCatalog(response);
            refreshSelectedGitStatus();
            emit entryCreated(
                entry.value(QStringLiteral("kind")).toString(),
                entry.value(QStringLiteral("relativePath")).toString(),
                entry.value(QStringLiteral("file")).toMap().value(QStringLiteral("id")).toString()
            );
        }
    );
}

void LibraryStore::renameFile(const QString &fileId, const QString &name)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || name.trimmed().isEmpty()
        || !containsFile(fileId)
        || m_mutatingEntry
    ) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    core->run(
        [libraryId, fileId, name](CoreDatabase &database) {
            const QVariantMap file = LibraryFileOperations::renameFile(database, libraryId, fileId, name);
            return withCatalog(database, libraryId, QStringLiteral("file"), file);
        },
        this,
        [this, libraryId, fileId](const CoreResult &result) {
            setMutatingEntry(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (m_selectedLibraryId != libraryId) {
                return;
            }

            const QVariantMap response = result.value.toMap();
            const QVariantMap file = response.value(QStringLiteral("file")).toMap();
            applyCatalog(response);
            refreshSelectedGitStatus();
            emit fileRenamed(
                fileId,
                file.value(QStringLiteral("relativePath")).toString(),
                file.value(QStringLiteral("name")).toString()
            );
        }
    );
}

void LibraryStore::duplicateFile(const QString &fileId, const QString &name)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || name.trimmed().isEmpty()
        || !containsFile(fileId)
        || m_mutatingEntry
    ) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    core->run(
        [libraryId, fileId, name](CoreDatabase &database) {
            const QVariantMap file = LibraryFileOperations::duplicateFile(database, libraryId, fileId, name);
            return withCatalog(database, libraryId, QStringLiteral("file"), file);
        },
        this,
        [this, libraryId](const CoreResult &result) {
            setMutatingEntry(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (m_selectedLibraryId != libraryId) {
                return;
            }

            const QVariantMap response = result.value.toMap();
            const QVariantMap file = response.value(QStringLiteral("file")).toMap();
            applyCatalog(response);
            refreshSelectedGitStatus();
            emit fileDuplicated(
                file.value(QStringLiteral("id")).toString(),
                file.value(QStringLiteral("relativePath")).toString(),
                file.value(QStringLiteral("name")).toString()
            );
        }
    );
}

void LibraryStore::revealEntry(const QString &relativePath)
{
    revealEntryFromLibrary(m_selectedLibraryId, relativePath);
}

void LibraryStore::revealEntryFromLibrary(
    const QString &libraryId,
    const QString &relativePath
)
{
    if (
        libraryId.isEmpty()
        || relativePath.isEmpty()
        || !containsLibrary(libraryId)
        || m_mutatingEntry
    ) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage());
        return;
    }

    setErrorMessage({});
    setMutatingEntry(true);

    core->run(
        [libraryId, relativePath](CoreDatabase &database) {
            LibraryFileOperations::revealEntry(database, libraryId, relativePath);
            return QVariant();
        },
        this,
        [this, libraryId, relativePath](const CoreResult &result) {
            setMutatingEntry(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            if (m_selectedLibraryId == libraryId) {
                emit entryRevealed(relativePath);
            }
        }
    );
}

void LibraryStore::copyEntryPath(
    const QString &libraryId,
    const QString &relativePath,
    bool absolute
)
{
    const QString normalizedRelativePath = QDir::fromNativeSeparators(
        relativePath.trimmed()
    );

    if (
        libraryId.isEmpty()
        || normalizedRelativePath.isEmpty()
    ) {
        return;
    }

    QString rootPath;

    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (
            library.value(QStringLiteral("id")).toString()
            == libraryId
        ) {
            rootPath = library
                .value(QStringLiteral("rootPath"))
                .toString();
            break;
        }
    }

    if (rootPath.isEmpty()) {
        return;
    }

    const QString normalizedRootPath = QDir::cleanPath(
        QFileInfo(rootPath).absoluteFilePath()
    );
    const QString absolutePath = QDir::cleanPath(
        QFileInfo(normalizedRelativePath).isAbsolute()
            ? normalizedRelativePath
            : QDir(normalizedRootPath).absoluteFilePath(
                normalizedRelativePath
            )
    );

    QString copiedPath;

    if (absolute) {
        copiedPath = QDir::toNativeSeparators(absolutePath);
    } else {
        const QString normalizedRoot = QDir::fromNativeSeparators(
            normalizedRootPath
        );
        const QString normalizedAbsolute = QDir::fromNativeSeparators(
            absolutePath
        );
        const QString rootPrefix = normalizedRoot.endsWith('/')
            ? normalizedRoot
            : normalizedRoot + '/';

        if (normalizedAbsolute == normalizedRoot) {
            copiedPath = QStringLiteral(".");
        } else if (normalizedAbsolute.startsWith(rootPrefix)) {
            copiedPath = normalizedAbsolute.mid(rootPrefix.size());
        } else {
            return;
        }
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(copiedPath, QClipboard::Clipboard);
    }
}

void LibraryStore::previewFile(const QString &fileId)
{
    if (m_selectedLibraryId.isEmpty() || fileId.isEmpty() || !containsFile(fileId)) {
        return;
    }

    QVariantMap file;

    for (const QVariant &value : m_files) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == fileId) {
            file = candidate;
            break;
        }
    }

    previewFileFromLibrary(m_selectedLibraryId, fileId, file);
}

void LibraryStore::previewFileFromLibrary(
    const QString &libraryId,
    const QString &fileId,
    const QVariantMap &file
)
{
    if (
        libraryId.isEmpty()
        || fileId.isEmpty()
        || !containsLibrary(libraryId)
    ) {
        return;
    }

    ++m_previewPathRequestRevision;
    QVariantMap resolvedFile = file;

    if (resolvedFile.isEmpty() && libraryId == m_selectedLibraryId) {
        for (const QVariant &value : m_files) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == fileId) {
                resolvedFile = candidate;
                break;
            }
        }
    }

    if (resolvedFile.isEmpty()) {
        resolvedFile.insert(QStringLiteral("id"), fileId);
    }

    const bool reloadingSelectedFile =
        m_activeFileLibraryId == libraryId
        && m_selectedFileId == fileId
        && !m_filePreview.isEmpty();

    setActiveFileLibraryId(libraryId);
    setSelectedFileId(fileId);
    setSelectedFile(resolvedFile);

    if (!reloadingSelectedFile) {
        setFilePreview({});
    }

    setFilePreviewError({});
    setFileSaveError({});
    rebuildFileWatchers();

    const QString status = resolvedFile
        .value(QStringLiteral("status"))
        .toString();

    if (!status.isEmpty() && status != QStringLiteral("available")) {
        setLoadingFilePreview(false);
        setFilePreviewError(
            QStringLiteral("This file is not currently available. Rescan the Library and try again.")
        );
        return;
    }

    if (usesDirectImageRenderer(resolvedFile) || usesDocumentRenderer(resolvedFile)) {
        setLoadingFilePreview(false);
        return;
    }

    setLoadingFilePreview(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoadingFilePreview(false);
        setFilePreviewError(coreUnavailableMessage());
        return;
    }

    core->run(
        [libraryId, fileId](CoreDatabase &database) {
            return QVariant(LibraryFiles::readText(database, libraryId, fileId));
        },
        this,
        [this, libraryId, fileId](const CoreResult &result) {
            if (
                m_activeFileLibraryId != libraryId
                || m_selectedFileId != fileId
            ) {
                return;
            }

            setLoadingFilePreview(false);

            if (!result.ok) {
                setFilePreviewError(result.errorMessage);
                return;
            }

            const QVariantMap preview = result.value.toMap();
            const QVariantMap previewFile = preview
                .value(QStringLiteral("file"))
                .toMap();

            if (!previewFile.isEmpty()) {
                setSelectedFile(previewFile);
            }

            setFilePreview(preview);
        }
    );
}

void LibraryStore::previewFilePathFromLibrary(
    const QString &libraryId,
    const QString &relativePath
)
{
    QString normalizedPath = QDir::fromNativeSeparators(
        QDir::cleanPath(relativePath)
    );

    if (
        libraryId.isEmpty()
        || !containsLibrary(libraryId)
        || normalizedPath.isEmpty()
        || normalizedPath == QStringLiteral(".")
        || normalizedPath == QStringLiteral("..")
        || normalizedPath.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(normalizedPath)
    ) {
        return;
    }

    const quint64 requestRevision = ++m_previewPathRequestRevision;
    setFilePreviewError({});
    setLoadingFilePreview(true);

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoadingFilePreview(false);
        setFilePreviewError(coreUnavailableMessage());
        return;
    }

    core->read(
        [libraryId](CoreDatabase &database) {
            return QVariant(LibraryFileRepository::files(database, libraryId));
        },
        this,
        [this, libraryId, normalizedPath, requestRevision](const CoreResult &result) {
            if (requestRevision != m_previewPathRequestRevision) {
                return;
            }

            if (!result.ok) {
                setLoadingFilePreview(false);
                setFilePreviewError(result.errorMessage);
                return;
            }

            const QVariantList files = result.value.toList();

            for (const QVariant &value : files) {
                const QVariantMap candidate = value.toMap();
                const QString candidatePath = QDir::fromNativeSeparators(
                    QDir::cleanPath(
                        candidate
                            .value(QStringLiteral("relativePath"))
                            .toString()
                    )
                );

                if (candidatePath != normalizedPath) {
                    continue;
                }

                previewFileFromLibrary(
                    libraryId,
                    candidate.value(QStringLiteral("id")).toString(),
                    candidate
                );
                return;
            }

            setLoadingFilePreview(false);
            setFilePreviewError(
                QStringLiteral("The requested file is not cataloged in this Library.")
            );
        }
    );
}

void LibraryStore::saveFileContent(
    const QString &fileId,
    const QString &content,
    const QString &expectedModifiedAt
)
{
    if (
        m_savingFile
        || m_activeFileLibraryId.isEmpty()
        || fileId.isEmpty()
        || expectedModifiedAt.isEmpty()
        || fileId != m_selectedFileId
    ) {
        return;
    }

    const QString libraryId = m_activeFileLibraryId;
    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setFileSaveError(coreUnavailableMessage());
        emit fileSaveFailed(libraryId, fileId, coreUnavailableMessage());
        return;
    }

    setFileSaveError({});
    setSavingFile(true);
    m_watcherSuppressionTimer.start();

    core->run(
        [libraryId, fileId, content, expectedModifiedAt](CoreDatabase &database) {
            return QVariant(LibraryFileOperations::writeText(database, libraryId, fileId, content, expectedModifiedAt));
        },
        this,
        [this, libraryId, fileId](const CoreResult &result) {
            setSavingFile(false);

            if (!result.ok) {
                setFileSaveError(result.errorMessage);
                emit fileSaveFailed(libraryId, fileId, result.errorMessage);
                return;
            }

            const QVariantMap preview = result.value.toMap();
            const QVariantMap savedFile = preview.value(QStringLiteral("file")).toMap();

            if (m_selectedLibraryId == libraryId) {
                QVariantList nextFiles = m_files;

                for (qsizetype index = 0; index < nextFiles.size(); ++index) {
                    if (nextFiles.at(index).toMap().value(QStringLiteral("id")).toString() == fileId) {
                        nextFiles[index] = savedFile;
                        break;
                    }
                }

                setFiles(nextFiles);
            }

            if (m_activeFileLibraryId == libraryId && m_selectedFileId == fileId) {
                setSelectedFile(savedFile);
                setFilePreview(preview);
                setFilePreviewError({});
            }

            setFileSaveError({});
            m_watcherSuppressionTimer.start();
            rebuildFileWatchers();
            refreshSelectedGitStatus();
            emit fileSaved(libraryId, fileId, preview);
        }
    );
}

void LibraryStore::clearFilePreview()
{
    setActiveFileLibraryId({});
    setSelectedFileId({});
    setSelectedFile({});
    setFilePreview({});
    setLoadingFilePreview(false);
    setFilePreviewError({});
    setFileSaveError({});
}

void LibraryStore::setLibraries(const QVariantList &libraries)
{
    if (m_libraries == libraries) {
        return;
    }

    m_libraries = libraries;
    emit librariesChanged();
    emit selectedLibraryChanged();
    emit activeFileLibraryChanged();
}

void LibraryStore::setSelectedLibraryId(const QString &libraryId)
{
    if (m_selectedLibraryId == libraryId) {
        return;
    }

    m_selectedLibraryId = libraryId;
    setGitStatus({});
    emit selectedLibraryIdChanged();
    emit selectedLibraryChanged();
    rebuildFileWatchers();
    scheduleGitStatusRefresh();
}

void LibraryStore::applyCatalog(const QVariantMap &catalog)
{
    setFiles(catalog.value(QStringLiteral("files")).toList());
    setDirectories(catalog.value(QStringLiteral("directories")).toList());
}

void LibraryStore::setFiles(const QVariantList &files)
{
    QVariantList currentFiles;
    currentFiles.reserve(files.size());

    for (const QVariant &value : files) {
        const QVariantMap file = value.toMap();

        if (
            file.value(QStringLiteral("status")).toString()
            == QStringLiteral("missing")
        ) {
            continue;
        }

        currentFiles.append(file);
    }

    if (m_files == currentFiles) {
        return;
    }

    m_files = currentFiles;
    emit filesChanged();
    rebuildFileWatchers();

    if (
        !m_selectedFileId.isEmpty()
        && m_activeFileLibraryId == m_selectedLibraryId
    ) {
        QVariantMap refreshedFile;

        for (const QVariant &value : m_files) {
            const QVariantMap candidate = value.toMap();
            if (
                candidate.value(QStringLiteral("id")).toString()
                == m_selectedFileId
            ) {
                refreshedFile = candidate;
                break;
            }
        }

        if (!refreshedFile.isEmpty()) {
            setSelectedFile(refreshedFile);
        } else {
            QVariantMap missingFile = m_selectedFile;
            missingFile.insert(
                QStringLiteral("status"),
                QStringLiteral("missing")
            );
            setSelectedFile(missingFile);
            setFilePreview({});
            setFilePreviewError(
                QStringLiteral("This file is no longer available in the Library.")
            );
        }
    }
}

void LibraryStore::setDirectories(const QVariantList &directories)
{
    if (m_directories == directories) {
        return;
    }

    m_directories = directories;
    emit directoriesChanged();
}

void LibraryStore::setLatestScan(const QVariantMap &latestScan)
{
    if (m_latestScan == latestScan) {
        return;
    }

    m_latestScan = latestScan;
    emit latestScanChanged();
}

void LibraryStore::setGitStatus(const QVariantMap &gitStatus)
{
    if (m_gitStatus == gitStatus) {
        return;
    }

    m_gitStatus = gitStatus;
    emit gitStatusChanged();
}

void LibraryStore::setLoadingGitStatus(bool loading)
{
    if (m_loadingGitStatus == loading) {
        return;
    }

    m_loadingGitStatus = loading;
    emit loadingGitStatusChanged();
}

void LibraryStore::setActiveFileLibraryId(const QString &libraryId)
{
    if (m_activeFileLibraryId == libraryId) {
        return;
    }

    m_activeFileLibraryId = libraryId;
    emit activeFileLibraryChanged();
    rebuildFileWatchers();
}

void LibraryStore::setSelectedFile(const QVariantMap &file)
{
    if (m_selectedFile == file) {
        return;
    }

    m_selectedFile = file;
    emit selectedFileChanged();
    rebuildFileWatchers();
}

void LibraryStore::setSelectedFileId(const QString &fileId)
{
    if (m_selectedFileId == fileId) {
        return;
    }

    m_selectedFileId = fileId;
    emit selectedFileIdChanged();
    rebuildFileWatchers();
}

void LibraryStore::setFilePreview(const QVariantMap &preview)
{
    if (m_filePreview == preview) {
        return;
    }

    m_filePreview = preview;
    emit filePreviewChanged();
}

void LibraryStore::setLoadingLibraries(bool loading)
{
    if (m_loadingLibraries == loading) {
        return;
    }

    m_loadingLibraries = loading;
    emit loadingLibrariesChanged();
}

void LibraryStore::setLoadingFiles(bool loading)
{
    if (m_loadingFiles == loading) {
        return;
    }

    m_loadingFiles = loading;
    emit loadingFilesChanged();
}

void LibraryStore::setLoadingFilePreview(bool loading)
{
    if (m_loadingFilePreview == loading) {
        return;
    }

    m_loadingFilePreview = loading;
    emit loadingFilePreviewChanged();
}

void LibraryStore::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }

    m_scanning = scanning;
    emit scanningChanged();
}

void LibraryStore::setMovingFile(bool moving)
{
    if (m_movingFile == moving) {
        return;
    }

    m_movingFile = moving;
    emit movingFileChanged();
}

void LibraryStore::setMutatingEntry(bool mutating)
{
    if (m_mutatingEntry == mutating) {
        return;
    }

    m_mutatingEntry = mutating;
    emit mutatingEntryChanged();
}

void LibraryStore::setSavingFile(bool saving)
{
    if (m_savingFile == saving) {
        return;
    }

    m_savingFile = saving;
    emit savingFileChanged();
}

void LibraryStore::setCreatingLibrary(bool creating)
{
    if (m_creatingLibrary == creating) {
        return;
    }

    m_creatingLibrary = creating;
    emit creatingLibraryChanged();
}

void LibraryStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void LibraryStore::setFilePreviewError(const QString &message)
{
    if (m_filePreviewError == message) {
        return;
    }

    m_filePreviewError = message;
    emit filePreviewErrorChanged();
}

void LibraryStore::setFileSaveError(const QString &message)
{
    if (m_fileSaveError == message) {
        return;
    }

    m_fileSaveError = message;
    emit fileSaveErrorChanged();
}

void LibraryStore::upsertLibrary(const QVariantMap &library)
{
    const QString libraryId = library.value(QStringLiteral("id")).toString();

    if (libraryId.isEmpty()) {
        return;
    }

    QVariantList nextLibraries = m_libraries;
    bool replaced = false;

    for (qsizetype index = 0; index < nextLibraries.size(); ++index) {
        if (
            nextLibraries.at(index).toMap().value(QStringLiteral("id")).toString()
            == libraryId
        ) {
            nextLibraries[index] = library;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        nextLibraries.prepend(library);
    }

    setLibraries(nextLibraries);
}

bool LibraryStore::containsLibrary(const QString &libraryId) const
{
    if (libraryId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_libraries) {
        if (value.toMap().value(QStringLiteral("id")).toString() == libraryId) {
            return true;
        }
    }

    return false;
}

bool LibraryStore::containsFile(const QString &fileId) const
{
    if (fileId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_files) {
        if (value.toMap().value(QStringLiteral("id")).toString() == fileId) {
            return true;
        }
    }

    return false;
}
