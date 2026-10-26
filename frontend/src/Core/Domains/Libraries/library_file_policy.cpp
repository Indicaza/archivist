#include "library_file_policy.h"

#include "Core/Support/js_compat.h"

#include <QSet>
#include <QStringList>

namespace
{
const QSet<QString> &supportedTextExtensions()
{
    static const QSet<QString> values{
        QStringLiteral(".md"),   QStringLiteral(".markdown"), QStringLiteral(".txt"),  QStringLiteral(".log"),
        QStringLiteral(".json"), QStringLiteral(".yaml"),     QStringLiteral(".yml"),  QStringLiteral(".toml"),
        QStringLiteral(".xml"),  QStringLiteral(".ts"),       QStringLiteral(".tsx"),  QStringLiteral(".js"),
        QStringLiteral(".jsx"),  QStringLiteral(".css"),      QStringLiteral(".scss"), QStringLiteral(".sass"),
        QStringLiteral(".html"), QStringLiteral(".qml"),      QStringLiteral(".py"),   QStringLiteral(".rs"),
        QStringLiteral(".go"),   QStringLiteral(".c"),        QStringLiteral(".cc"),   QStringLiteral(".cxx"),
        QStringLiteral(".cpp"),  QStringLiteral(".h"),        QStringLiteral(".hpp"),  QStringLiteral(".sh"),
        QStringLiteral(".bash"), QStringLiteral(".zsh"),      QStringLiteral(".sql"),
    };
    return values;
}

const QSet<QString> &supportedTextFileNames()
{
    static const QSet<QString> values{
        QStringLiteral(".editorconfig"), QStringLiteral(".gitattributes"), QStringLiteral(".gitignore"),
        QStringLiteral("cmakelists.txt"), QStringLiteral("dockerfile"),     QStringLiteral("license"),
        QStringLiteral("makefile"),       QStringLiteral("readme"),
    };
    return values;
}

const QSet<QString> &dangerousExtensions()
{
    static const QSet<QString> values{
        QStringLiteral(".apk"),  QStringLiteral(".appx"), QStringLiteral(".appxbundle"), QStringLiteral(".com"),
        QStringLiteral(".cpl"),  QStringLiteral(".deb"),  QStringLiteral(".dll"),        QStringLiteral(".dmg"),
        QStringLiteral(".drv"),  QStringLiteral(".dylib"), QStringLiteral(".ear"),       QStringLiteral(".exe"),
        QStringLiteral(".ipa"),  QStringLiteral(".iso"),  QStringLiteral(".jar"),        QStringLiteral(".lnk"),
        QStringLiteral(".msi"),  QStringLiteral(".msp"),  QStringLiteral(".msix"),       QStringLiteral(".ocx"),
        QStringLiteral(".pif"),  QStringLiteral(".pkg"),  QStringLiteral(".rpm"),        QStringLiteral(".scr"),
        QStringLiteral(".so"),   QStringLiteral(".sys"),  QStringLiteral(".vhd"),        QStringLiteral(".vhdx"),
        QStringLiteral(".war"),
    };
    return values;
}

const QStringList &managedPhotoLibraryExtensions()
{
    static const QStringList values{
        QStringLiteral(".aplibrary"),
        QStringLiteral(".migratedphotolibrary"),
        QStringLiteral(".photolibrary"),
        QStringLiteral(".photoslibrary"),
    };
    return values;
}

const QStringList &excludedDirectoryExtensions()
{
    static const QStringList values{
        QStringLiteral(".app"),       QStringLiteral(".bundle"), QStringLiteral(".captureone"),
        QStringLiteral(".framework"), QStringLiteral(".kext"),   QStringLiteral(".lrcat-data"),
        QStringLiteral(".lrdata"),    QStringLiteral(".plugin"), QStringLiteral(".xpc"),
    };
    return values;
}

QString normalized(const QString &name)
{
    return JsCompat::trimmed(name).toLower();
}
}

namespace LibraryFilePolicy
{
bool isCatalogableFile(const QString &fileName)
{
    const QString name = normalized(fileName);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const QString extension = dot > 0 ? name.mid(dot) : QString();

    return !name.isEmpty() && name != QStringLiteral("autorun.inf") && !dangerousExtensions().contains(extension);
}

bool isManagedPhotoLibraryDirectory(const QString &directoryName)
{
    const QString name = normalized(directoryName);

    for (const QString &extension : managedPhotoLibraryExtensions()) {
        if (name.endsWith(extension)) {
            return true;
        }
    }

    return false;
}

bool isCatalogableDirectory(const QString &directoryName)
{
    const QString name = normalized(directoryName);

    if (isManagedPhotoLibraryDirectory(name)) {
        return false;
    }

    for (const QString &extension : excludedDirectoryExtensions()) {
        if (name.endsWith(extension)) {
            return false;
        }
    }

    return !name.isEmpty();
}

bool isSupportedTextFile(const QString &fileName, const QString &extension)
{
    return supportedTextExtensions().contains(normalized(extension)) || supportedTextFileNames().contains(normalized(fileName));
}

bool isIgnoredScanDirectory(const QString &directoryName)
{
    static const QSet<QString> names{
        QStringLiteral(".git"), QStringLiteral(".obsidian"), QStringLiteral("node_modules"),
        QStringLiteral("dist"), QStringLiteral("build"),     QStringLiteral("coverage"),
    };
    return names.contains(directoryName.toLower());
}

bool isIgnoredCatalogDirectory(const QString &directoryName)
{
    return isIgnoredScanDirectory(directoryName) || directoryName.toLower() == QStringLiteral(".archivist-trash");
}

bool isIgnoredFile(const QString &fileName)
{
    return fileName == QStringLiteral(".DS_Store") || fileName == QStringLiteral(".localized");
}

QString extensionOf(const QString &fileName)
{
    // Node's path.extname: from the last dot, unless that dot starts the name.
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));

    if (dot <= 0 || fileName == QStringLiteral("..")) {
        return {};
    }

    return fileName.mid(dot).toLower();
}
}
