#include "markdown_document_bridge.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextLength>
#include <QVector>

namespace
{
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool isInsideRoot(const QString &rootPath, const QString &candidatePath)
{
    const Qt::CaseSensitivity sensitivity = pathCaseSensitivity();
    const QString normalizedRoot = QDir::cleanPath(rootPath);
    const QString normalizedCandidate = QDir::cleanPath(candidatePath);

    return normalizedCandidate.compare(normalizedRoot, sensitivity) == 0
        || normalizedCandidate.startsWith(
            normalizedRoot + QDir::separator(),
            sensitivity
        );
}

struct LocalTarget
{
    QString path;
    bool rootRelative = false;
};

LocalTarget decodedLocalTarget(const QString &value)
{
    QString target = value.trimmed();

    if (target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>'))) {
        target = target.mid(1, target.size() - 2).trimmed();
    }

    const QUrl url(target, QUrl::TolerantMode);

    if (!url.isValid() || !url.scheme().isEmpty() || url.isEmpty()) {
        return {};
    }

    QString path = url.path(QUrl::FullyDecoded).trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    const bool rootRelative = path.startsWith(QLatin1Char('/'));

    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }

    return {path, rootRelative};
}
}

MarkdownDocumentBridge::MarkdownDocumentBridge(QObject *parent)
    : QObject(parent)
{
}

QUrl MarkdownDocumentBridge::resolveImageUrl(
    const QString &libraryRootPath,
    const QString &documentRelativePath,
    const QString &target
) const
{
    QString directTarget = target.trimmed();

    if (
        directTarget.startsWith(QLatin1Char('<'))
        && directTarget.endsWith(QLatin1Char('>'))
    ) {
        directTarget = directTarget.mid(1, directTarget.size() - 2).trimmed();
    }

    const QUrl directUrl(directTarget, QUrl::TolerantMode);

    if (directUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
        return directUrl;
    }

    const LocalTarget localTarget = decodedLocalTarget(target);

    if (libraryRootPath.trimmed().isEmpty() || localTarget.path.isEmpty()) {
        return {};
    }

    const QFileInfo rootInfo(libraryRootPath);
    const QString canonicalRoot = rootInfo.canonicalFilePath();

    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        return {};
    }

    QString documentDirectory = localTarget.rootRelative
        ? QString{}
        : QFileInfo(documentRelativePath).path();

    if (documentDirectory == QStringLiteral(".")) {
        documentDirectory.clear();
    }

    const QString relativeCandidate = QDir::cleanPath(
        QDir(documentDirectory).filePath(localTarget.path)
    );

    if (
        relativeCandidate == QStringLiteral("..")
        || relativeCandidate.startsWith(QStringLiteral("../"))
    ) {
        return {};
    }

    const QFileInfo candidateInfo(QDir(canonicalRoot).filePath(relativeCandidate));
    const QString canonicalCandidate = candidateInfo.canonicalFilePath();

    if (
        canonicalCandidate.isEmpty()
        || !candidateInfo.isFile()
        || !isInsideRoot(canonicalRoot, canonicalCandidate)
    ) {
        return {};
    }

    const QString mimeType = QMimeDatabase()
        .mimeTypeForFile(canonicalCandidate, QMimeDatabase::MatchContent)
        .name();

    if (!mimeType.startsWith(QStringLiteral("image/"))) {
        return {};
    }

    return QUrl::fromLocalFile(canonicalCandidate);
}

void MarkdownDocumentBridge::constrainImages(
    QObject *quickDocument,
    qreal maximumWidth
) const
{
    auto *wrapper = qobject_cast<QQuickTextDocument *>(quickDocument);

    if (!wrapper || maximumWidth <= 0.0) {
        return;
    }

    QTextDocument *document = wrapper->textDocument();

    if (!document) {
        return;
    }

    struct ImageRange
    {
        int position;
        int length;
        QTextImageFormat format;
    };

    QVector<ImageRange> images;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator iterator = block.begin(); !iterator.atEnd(); ++iterator) {
            const QTextFragment fragment = iterator.fragment();

            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }

            QTextImageFormat format = fragment.charFormat().toImageFormat();
            format.setMaximumWidth(
                QTextLength(QTextLength::FixedLength, maximumWidth)
            );
            images.push_back({fragment.position(), fragment.length(), format});
        }
    }

    for (const ImageRange &image : images) {
        QTextCursor cursor(document);
        cursor.setPosition(image.position);
        cursor.setPosition(
            image.position + image.length,
            QTextCursor::KeepAnchor
        );
        cursor.mergeCharFormat(image.format);
    }
}
