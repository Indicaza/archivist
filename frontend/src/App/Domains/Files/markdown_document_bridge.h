#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class MarkdownDocumentBridge final : public QObject
{
    Q_OBJECT

public:
    explicit MarkdownDocumentBridge(QObject *parent = nullptr);

    Q_INVOKABLE QUrl resolveImageUrl(
        const QString &libraryRootPath,
        const QString &documentRelativePath,
        const QString &target
    ) const;
    Q_INVOKABLE void constrainImages(QObject *quickDocument, qreal maximumWidth) const;
};
