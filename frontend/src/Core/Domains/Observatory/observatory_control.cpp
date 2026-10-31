#include "observatory_control.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Observatory
{
namespace
{
// Long enough for the slowest operation (an evidence export of a large run);
// starting a test returns at once, the test itself runs in the background.
constexpr int kOperationTimeoutMs = 60000;
constexpr qint64 kMaxResponseBytes = 8 * 1024 * 1024;

} // namespace

Response controlError(int status, const QString &operation, const QString &code, const QString &message)
{
    const QJsonObject body{
        {QStringLiteral("schema"), QStringLiteral("archivist.observatory.response")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}},
    };
    return {status, "application/json; charset=utf-8", QJsonDocument(body).toJson(QJsonDocument::Compact), securityHeaders()};
}

QByteArray newControlToken()
{
    QByteArray bytes(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / int(sizeof(quint32)));
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool sameControlToken(const QByteArray &given, const QByteArray &expected)
{
    if (expected.isEmpty() || given.size() != expected.size()) {
        return false;
    }

    unsigned char difference = 0;

    for (qsizetype index = 0; index < given.size(); ++index) {
        difference |= static_cast<unsigned char>(given.at(index) ^ expected.at(index));
    }

    return difference == 0;
}

bool isControlOperationName(const QString &operation)
{
    static const QRegularExpression pattern(QStringLiteral("^observatory(\\.[a-z]{1,20}){1,2}$"));
    return pattern.match(operation).hasMatch();
}

QString controlScriptPath()
{
#ifdef ARCHIVIST_REPOSITORY_ROOT
    const QString script = QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT)).filePath(QStringLiteral("scripts/observatory/cli.mjs"));

    if (QFileInfo(script).isFile()) {
        return QFileInfo(script).canonicalFilePath();
    }
#endif
    return {};
}

QString nodeExecutable()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("node"));

    if (!onPath.isEmpty()) {
        return onPath;
    }

    return QStandardPaths::findExecutable(QStringLiteral("node"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")});
}

Response runControlOperation(const QString &root, const QString &operation, const QByteArray &input)
{
    if (!isControlOperationName(operation)) {
        return controlError(404, operation, QStringLiteral("unknown-operation"), QStringLiteral("Not an Observatory operation."));
    }

    const QString script = controlScriptPath();

    if (script.isEmpty()) {
        return controlError(404, operation, QStringLiteral("controls-disabled"),
            QStringLiteral("Test controls need a development build of Archivist (the repository's scripts/observatory)."));
    }

    const QString node = nodeExecutable();

    if (node.isEmpty()) {
        return controlError(503, operation, QStringLiteral("controls-disabled"),
            QStringLiteral("Test controls need Node.js, and no node executable was found. Start Archivist from a terminal where node works (npm run dev)."));
    }

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ARCHIVIST_OBSERVATORY_DIR"), root);
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QFileInfo(script).absolutePath());
    process.setProgram(node);
    process.setArguments({script, QStringLiteral("control"), operation});
    process.start();

    if (!process.waitForStarted(10000)) {
        return controlError(500, operation, QStringLiteral("internal"), QStringLiteral("Could not start node: ") + process.errorString());
    }

    process.write(input.isEmpty() ? QByteArray("{}") : input);
    process.closeWriteChannel();

    if (!process.waitForFinished(kOperationTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return controlError(504, operation, QStringLiteral("internal"), QStringLiteral("The Observatory script did not answer in time."));
    }

    const QByteArray output = process.readAllStandardOutput();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output.size() > kMaxResponseBytes ? QByteArray() : output, &error);

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        const QString detail = QString::fromUtf8(process.readAllStandardError().right(400)).trimmed();
        return controlError(502, operation, QStringLiteral("internal"),
            QStringLiteral("The Observatory script gave no usable answer") + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
    }

    // The envelope says whether it worked; the page reads `ok`.
    return {200, "application/json; charset=utf-8", output, securityHeaders()};
}
} // namespace Observatory
