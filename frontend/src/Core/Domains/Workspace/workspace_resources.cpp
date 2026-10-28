#include "Core/Domains/Workspace/workspace_resources.h"

#include "Core/Domains/Workspace/view_type_registry.h"

#include <QSet>

namespace Workspace::Resources
{
namespace
{
QString normalizedExtension(const QString &name, const QString &extension)
{
    QString suffix = extension.trimmed().toLower();
    if (!suffix.isEmpty() && !suffix.startsWith(QLatin1Char('.'))) {
        suffix.prepend(QLatin1Char('.'));
    }
    if (!suffix.isEmpty()) {
        return suffix;
    }
    const QString base = name.section(QLatin1Char('/'), -1).toLower();
    const qsizetype dot = base.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? base.mid(dot) : QString();
}
} // namespace

FileView viewForFile(const QString &name, const QString &extension, const QString &mimeType)
{
    // Mirrors FileIdentity.js: documents and media render, everything else
    // (code, data, unknown text) edits.
    static const QSet<QString> textPreview{
        QStringLiteral(".md"), QStringLiteral(".markdown"), QStringLiteral(".txt"), QStringLiteral(".log"),
    };
    static const QSet<QString> binaryPreview{
        QStringLiteral(".svg"), QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
        QStringLiteral(".gif"), QStringLiteral(".webp"), QStringLiteral(".bmp"), QStringLiteral(".ico"),
        QStringLiteral(".pdf"), QStringLiteral(".doc"), QStringLiteral(".docx"), QStringLiteral(".odt"),
        QStringLiteral(".rtf"), QStringLiteral(".xls"), QStringLiteral(".xlsx"), QStringLiteral(".ods"),
        QStringLiteral(".ppt"), QStringLiteral(".pptx"), QStringLiteral(".odp"), QStringLiteral(".fbx"),
        QStringLiteral(".obj"), QStringLiteral(".gltf"), QStringLiteral(".glb"), QStringLiteral(".usd"),
        QStringLiteral(".usdz"), QStringLiteral(".zip"), QStringLiteral(".tar"), QStringLiteral(".gz"),
        QStringLiteral(".mp3"), QStringLiteral(".wav"), QStringLiteral(".ogg"), QStringLiteral(".mp4"),
        QStringLiteral(".mov"), QStringLiteral(".webm"),
    };
    static const QSet<QString> textNames{QStringLiteral("readme"), QStringLiteral("license")};

    const QString base = name.section(QLatin1Char('/'), -1).toLower();
    const QString suffix = normalizedExtension(name, extension);
    const QString mime = mimeType.trimmed().toLower();

    if (textNames.contains(base) || textPreview.contains(suffix) || mime == QStringLiteral("text/markdown")) {
        return {ViewTypes::Preview, true};
    }
    if (binaryPreview.contains(suffix) || mime.startsWith(QStringLiteral("image/")) || mime.startsWith(QStringLiteral("audio/"))
        || mime.startsWith(QStringLiteral("video/")) || mime == QStringLiteral("application/pdf")) {
        return {ViewTypes::Preview, false};
    }
    return {ViewTypes::Editor, true};
}

ResourceRef document(const QString &collectionId, const QString &libraryId, const QVariantMap &file)
{
    const QString fileId = file.value(QStringLiteral("id")).toString();
    const QString path = file.value(QStringLiteral("relativePath")).toString();
    const QString name = file.value(QStringLiteral("name")).toString().isEmpty()
        ? path.section(QLatin1Char('/'), -1)
        : file.value(QStringLiteral("name")).toString();
    const QString extension = file.value(QStringLiteral("extension")).toString();
    const QString mimeType = file.value(QStringLiteral("mimeType")).toString();
    const FileView view = viewForFile(name, extension, mimeType);

    ResourceRef resource;
    resource.kind = QStringLiteral("document");
    resource.id = collectionId + QLatin1Char(':') + libraryId + QLatin1Char(':') + fileId;
    resource.collectionId = collectionId;
    resource.libraryId = libraryId;
    resource.details = {
        {QStringLiteral("fileId"), fileId},
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("extension"), extension},
        {QStringLiteral("mimeType"), mimeType},
        {QStringLiteral("languageId"), file.value(QStringLiteral("languageId")).toString()},
        {QStringLiteral("sizeBytes"), file.value(QStringLiteral("sizeBytes")).toLongLong()},
        {QStringLiteral("textual"), view.textual},
        {QStringLiteral("file"), file},
    };
    return resource;
}

ResourceRef terminal(const QString &collectionId, const QString &libraryId, const QString &sessionId)
{
    ResourceRef resource;
    resource.kind = QStringLiteral("terminal");
    resource.id = libraryId + QLatin1Char(':') + sessionId;
    resource.collectionId = collectionId;
    resource.libraryId = libraryId;
    resource.details = {{QStringLiteral("sessionId"), sessionId}};
    return resource;
}

ResourceRef chat(const QString &chatId)
{
    ResourceRef resource;
    resource.kind = QStringLiteral("chat");
    resource.id = chatId;
    return resource;
}
} // namespace Workspace::Resources
