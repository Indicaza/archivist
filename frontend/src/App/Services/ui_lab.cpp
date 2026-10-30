#include "App/Services/ui_lab.h"

#include "Core/Support/app_profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QtEnvironmentVariables>

namespace
{
QObject *findUnder(QQuickItem *item, const QString &objectName)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == objectName) {
        return item;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QObject *found = findUnder(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

QVariantMap check(const QString &name, const QString &path)
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("insideProfile"), AppProfile::contains(path)},
    };
}
} // namespace

UiLab::UiLab(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

bool UiLab::requested()
{
    return !qEnvironmentVariable("ARCHIVIST_UI_LAB_SCENARIO").trimmed().isEmpty();
}

bool UiLab::load(const QString &databasePath, QString *error)
{
    m_databasePath = databasePath;
    m_outputPath = qEnvironmentVariable("ARCHIVIST_UI_LAB_OUTPUT").trimmed();

    if (!AppProfile::active()) {
        *error = QStringLiteral("ARCHIVIST_PROFILE_DIR is not set; the UI Lab only runs in a disposable profile.");
        return false;
    }
    if (m_outputPath.isEmpty()) {
        *error = QStringLiteral("ARCHIVIST_UI_LAB_OUTPUT is not set.");
        return false;
    }
    const QVariantMap report = isolation();
    if (!report.value(QStringLiteral("ok")).toBool()) {
        *error = QStringLiteral("Storage is not isolated in the test profile: ")
            + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(report)).toJson(QJsonDocument::Compact));
        return false;
    }

    QFile file(qEnvironmentVariable("ARCHIVIST_UI_LAB_SCENARIO"));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read the scenario: ") + file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        *error = QStringLiteral("The scenario is not a JSON object: ") + parseError.errorString();
        return false;
    }
    m_scenario = document.object().toVariantMap();
    return true;
}

double UiLab::nowMs() const
{
    return m_clock.nsecsElapsed() / 1e6;
}

void UiLab::attachWindow(QObject *window)
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow || m_window == quickWindow) {
        return;
    }
    m_window = quickWindow;
    connect(quickWindow, &QQuickWindow::frameSwapped, this, [this]() {
        const double now = nowMs();
        QMutexLocker lock(&m_framesMutex);
        m_frames.append(now);
    }, Qt::DirectConnection);
}

QVariantMap UiLab::frames(double fromMs, double toMs) const
{
    QMutexLocker lock(&m_framesMutex);
    int count = 0;
    double first = -1;
    double last = -1;
    double previous = -1;
    double maxInterval = 0;
    for (double frame : m_frames) {
        if (frame <= fromMs || frame > toMs) {
            continue;
        }
        if (count == 0) {
            first = frame - fromMs;
        } else {
            maxInterval = qMax(maxInterval, frame - previous);
        }
        previous = frame;
        last = frame - fromMs;
        ++count;
    }
    QVariantMap result{{QStringLiteral("count"), count}};
    result.insert(QStringLiteral("firstMs"), count ? QVariant(first) : QVariant());
    result.insert(QStringLiteral("lastMs"), count ? QVariant(last) : QVariant());
    result.insert(QStringLiteral("maxIntervalMs"), count > 1 ? QVariant(maxInterval) : QVariant());
    return result;
}

double UiLab::lastFrameMs() const
{
    QMutexLocker lock(&m_framesMutex);
    return m_frames.isEmpty() ? -1 : m_frames.last();
}

QVariantMap UiLab::isolation() const
{
    const QVariantList checks{
        check(QStringLiteral("database"), m_databasePath),
        check(QStringLiteral("settings"), QSettings().fileName()),
        check(QStringLiteral("appData"), AppProfile::dataLocation()),
    };
    bool ok = AppProfile::active();
    for (const QVariant &entry : checks) {
        ok = ok && entry.toMap().value(QStringLiteral("insideProfile")).toBool();
    }
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("profileDir"), AppProfile::directory()},
        {QStringLiteral("checks"), checks},
    };
}

QObject *UiLab::findItem(const QString &objectName) const
{
    return m_window ? findUnder(m_window->contentItem(), objectName) : nullptr;
}

bool UiLab::mouse(const QString &type, double sceneX, double sceneY)
{
    if (!m_window) {
        return false;
    }
    QEvent::Type eventType = QEvent::MouseMove;
    Qt::MouseButton button = Qt::NoButton;
    if (type == QLatin1String("press")) {
        eventType = QEvent::MouseButtonPress;
        button = Qt::LeftButton;
        m_buttonDown = true;
    } else if (type == QLatin1String("release")) {
        eventType = QEvent::MouseButtonRelease;
        button = Qt::LeftButton;
        m_buttonDown = false;
    } else if (type != QLatin1String("move")) {
        return false;
    }
    const QPointF scene(sceneX, sceneY);
    const QPointF global = m_window->mapToGlobal(scene);
    QMouseEvent event(eventType, scene, scene, global, button,
                      m_buttonDown ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    event.setTimestamp(static_cast<quint64>(m_clock.elapsed()));
    QCoreApplication::sendEvent(m_window, &event);
    return event.isAccepted();
}

bool UiLab::writeResult(const QVariantMap &result)
{
    QSaveFile file(m_outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Indented));
    return file.commit();
}

void UiLab::log(const QString &message) const
{
    qInfo().noquote() << "[UiLab]" << message;
}

void UiLab::finish(int exitCode)
{
    QTimer::singleShot(0, qApp, [exitCode]() { QCoreApplication::exit(exitCode); });
}
