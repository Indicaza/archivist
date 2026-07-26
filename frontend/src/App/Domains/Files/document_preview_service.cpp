#include "document_preview_service.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace
{
bool pathIsInsideRoot(const QString &rootPath, const QString &candidatePath)
{
    const QString relative = QDir(rootPath).relativeFilePath(candidatePath);
    return relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative);
}

bool isOfficeExtension(const QString &extension)
{
    static const QSet<QString> extensions{
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
    return extensions.contains(extension);
}

QString findLibreOffice()
{
    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
        return soffice;
    }

    const QString libreoffice =
        QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    if (!libreoffice.isEmpty()) {
        return libreoffice;
    }

#ifdef Q_OS_MACOS
    const QString bundleExecutable =
        QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS/soffice");
    if (QFileInfo::exists(bundleExecutable)) {
        return bundleExecutable;
    }
#endif

    return {};
}
}

DocumentPreviewService::DocumentPreviewService(QObject *parent)
    : QObject(parent)
{
}

DocumentPreviewService::~DocumentPreviewService()
{
    clear();
}

QUrl DocumentPreviewService::previewUrl() const
{
    return m_previewUrl;
}

QString DocumentPreviewService::state() const
{
    return m_state;
}

QString DocumentPreviewService::errorMessage() const
{
    return m_errorMessage;
}

QString DocumentPreviewService::converterLabel() const
{
    return m_converterLabel;
}

void DocumentPreviewService::openDocument(
    const QString &libraryRootPath,
    const QString &relativePath
)
{
    ++m_requestRevision;
    const quint64 revision = m_requestRevision;

    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    QObject::disconnect(&m_process, nullptr, this, nullptr);
    cleanupJobDirectory();
    m_cachePath.clear();

    setPreviewUrl({});
    setErrorMessage({});
    setConverterLabel({});
    setState(QStringLiteral("loading"));

    const QString canonicalRoot = QFileInfo(libraryRootPath).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        fail(QStringLiteral("The active Library folder could not be resolved."));
        return;
    }

    const QString requestedPath =
        QDir(canonicalRoot).absoluteFilePath(relativePath);
    const QFileInfo sourceInfo(requestedPath);
    const QString canonicalSource = sourceInfo.canonicalFilePath();

    if (
        canonicalSource.isEmpty()
        || !sourceInfo.isFile()
        || !pathIsInsideRoot(canonicalRoot, canonicalSource)
    ) {
        fail(QStringLiteral("The document could not be resolved inside this Library."));
        return;
    }

    const QString extension = sourceInfo.suffix().toLower();

    if (extension == QStringLiteral("pdf")) {
        setConverterLabel(QStringLiteral("Native PDF"));
        setPreviewUrl(QUrl::fromLocalFile(canonicalSource));
        setState(QStringLiteral("ready"));
        return;
    }

    if (!isOfficeExtension(extension)) {
        fail(QStringLiteral("This document format is not supported."));
        return;
    }

    const QString converter = findLibreOffice();
    if (converter.isEmpty()) {
        fail(QStringLiteral(
            "LibreOffice is required to preview Word, Excel, "
            "PowerPoint, and OpenDocument files."
        ));
        return;
    }

    const QByteArray fingerprintInput = QStringLiteral("%1|%2|%3")
        .arg(
            canonicalSource,
            QString::number(sourceInfo.size()),
            QString::number(sourceInfo.lastModified().toMSecsSinceEpoch())
        )
        .toUtf8();
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(
            fingerprintInput,
            QCryptographicHash::Sha256
        ).toHex()
    );

    const QString cacheRoot = QDir(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
    ).absoluteFilePath(QStringLiteral("document-previews"));
    QDir().mkpath(cacheRoot);

    m_cachePath =
        QDir(cacheRoot).absoluteFilePath(fingerprint + QStringLiteral(".pdf"));

    if (QFileInfo::exists(m_cachePath)) {
        setConverterLabel(QStringLiteral("LibreOffice cache"));
        setPreviewUrl(QUrl::fromLocalFile(m_cachePath));
        setState(QStringLiteral("ready"));
        return;
    }

    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_jobDirectory = QDir(cacheRoot).absoluteFilePath(
        QStringLiteral("job-%1-%2").arg(fingerprint.left(12), jobId)
    );
    if (!QDir().mkpath(m_jobDirectory)) {
        fail(QStringLiteral("The document preview workspace could not be created."));
        return;
    }

    const QString generatedPath = QDir(m_jobDirectory).absoluteFilePath(
        sourceInfo.completeBaseName() + QStringLiteral(".pdf")
    );

    connect(
        &m_process,
        &QProcess::errorOccurred,
        this,
        [this, revision](QProcess::ProcessError error) {
            if (
                revision == m_requestRevision
                && error == QProcess::FailedToStart
            ) {
                fail(QStringLiteral("LibreOffice could not be started."));
            }
        }
    );

    connect(
        &m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, revision, generatedPath](int exitCode, QProcess::ExitStatus status) {
            if (revision != m_requestRevision) {
                return;
            }

            if (
                status != QProcess::NormalExit
                || exitCode != 0
                || !QFileInfo::exists(generatedPath)
            ) {
                const QString standardError =
                    QString::fromUtf8(m_process.readAllStandardError()).trimmed();
                const QString standardOutput =
                    QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
                fail(
                    !standardError.isEmpty()
                        ? standardError
                        : !standardOutput.isEmpty()
                            ? standardOutput
                            : QStringLiteral(
                                "LibreOffice could not convert this document."
                            )
                );
                return;
            }

            QFile::remove(m_cachePath);
            if (
                !QFile::rename(generatedPath, m_cachePath)
                && !QFile::copy(generatedPath, m_cachePath)
            ) {
                fail(QStringLiteral("The converted PDF preview could not be cached."));
                return;
            }

            cleanupJobDirectory();
            setPreviewUrl(QUrl::fromLocalFile(m_cachePath));
            setState(QStringLiteral("ready"));
        }
    );

    setConverterLabel(QStringLiteral("LibreOffice"));
    m_process.setProgram(converter);
    m_process.setArguments({
        QStringLiteral("--headless"),
        QStringLiteral("--convert-to"),
        QStringLiteral("pdf"),
        QStringLiteral("--outdir"),
        m_jobDirectory,
        canonicalSource,
    });
    m_process.start();
}

void DocumentPreviewService::clear()
{
    ++m_requestRevision;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    QObject::disconnect(&m_process, nullptr, this, nullptr);
    cleanupJobDirectory();
    m_cachePath.clear();
    setPreviewUrl({});
    setErrorMessage({});
    setConverterLabel({});
    setState(QStringLiteral("idle"));
}

void DocumentPreviewService::setPreviewUrl(const QUrl &url)
{
    if (m_previewUrl == url) {
        return;
    }
    m_previewUrl = url;
    emit previewUrlChanged();
}

void DocumentPreviewService::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void DocumentPreviewService::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void DocumentPreviewService::setConverterLabel(const QString &label)
{
    if (m_converterLabel == label) {
        return;
    }
    m_converterLabel = label;
    emit converterLabelChanged();
}

void DocumentPreviewService::cleanupJobDirectory()
{
    if (m_jobDirectory.isEmpty()) {
        return;
    }

    QDir(m_jobDirectory).removeRecursively();
    m_jobDirectory.clear();
}

void DocumentPreviewService::fail(const QString &message)
{
    cleanupJobDirectory();
    setPreviewUrl({});
    setErrorMessage(message);
    setState(QStringLiteral("error"));
}
