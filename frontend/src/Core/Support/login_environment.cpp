#include "login_environment.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QtGlobal>

namespace LoginEnvironment
{
bool hasBarePath(const QString &path)
{
    static const QStringList system{
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin"),
        QStringLiteral("/usr/sbin"),
        QStringLiteral("/sbin"),
    };

    for (const QString &entry : path.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        if (!system.contains(entry)) {
            return false;
        }
    }

    return true;
}

QString mergedPath(const QString &login, const QString &current)
{
    QStringList entries;

    for (const QString &source : {login, current}) {
        for (const QString &entry : source.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
            const QString trimmed = entry.trimmed();

            if (!trimmed.isEmpty() && !entries.contains(trimmed)) {
                entries.append(trimmed);
            }
        }
    }

    return entries.join(QLatin1Char(':'));
}

void adoptLoginShellPath()
{
#ifdef Q_OS_MACOS
    const QString current = qEnvironmentVariable("PATH");

    if (qEnvironmentVariableIntValue("ARCHIVIST_SKIP_LOGIN_PATH") == 1 || !hasBarePath(current)) {
        return;
    }

    QString shell = qEnvironmentVariable("SHELL");

    if (shell.isEmpty() || !QFileInfo(shell).isExecutable()) {
        shell = QStringLiteral("/bin/zsh");
    }

    // Interactive and login, so PATH set in .zshrc (nvm, pyenv) is included.
    // The marker skips anything the shell's startup files print.
    static const QString marker = QStringLiteral("__ARCHIVIST_PATH__=");
    QProcess process;
    process.setProgram(shell);
    process.setArguments({QStringLiteral("-ilc"), QStringLiteral("printf '%s%s' '%1' \"$PATH\"").arg(marker)});
    process.setWorkingDirectory(QDir::homePath());
    process.setStandardInputFile(QProcess::nullDevice());
    process.start();

    QString login;

    if (process.waitForFinished(4000)) {
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        const qsizetype at = output.lastIndexOf(marker);

        if (at >= 0) {
            login = output.mid(at + marker.size()).trimmed();
        }
    } else {
        process.kill();
        process.waitForFinished(500);
        qWarning().noquote() << "[Env] Login shell did not answer; using common tool folders.";
    }

    // Homebrew's folders even when the shell gave nothing back.
    const QString fallback = QStringLiteral("/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin");
    qputenv("PATH", mergedPath(mergedPath(login, fallback), current).toUtf8());
#endif
}
}
