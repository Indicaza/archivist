#include "App/Services/interface_scale.h"

#include <QCoreApplication>
#include <QList>
#include <QProcess>
#include <QSettings>
#include <QtEnvironmentVariables>

#include <cmath>

namespace {
const QList<double> kSteps = {0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
const char kKey[] = "ui/interfaceScale";
const char kAppliedEnv[] = "ARCHIVIST_APPLIED_SCALE";

QSettings settings()
{
    // Usable before QCoreApplication has its names.
    return QSettings(QSettings::UserScope, QStringLiteral("Archivist"), QStringLiteral("Archivist"));
}

double savedScale()
{
    return InterfaceScale::nearestStep(settings().value(QLatin1String(kKey), 1.0).toDouble());
}
}

double InterfaceScale::nearestStep(double value)
{
    double best = 1.0;
    for (double candidate : kSteps) {
        if (std::abs(candidate - value) < std::abs(best - value)) {
            best = candidate;
        }
    }
    return best;
}

void InterfaceScale::applyAtStartup()
{
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        return;
    }

    const double scale = savedScale();
    if (!qFuzzyCompare(scale, 1.0)) {
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale));
        // Marks the factor as ours, so a restart from inside the app does
        // not mistake it for one the user set.
        qputenv(kAppliedEnv, QByteArray::number(scale));
    }
}

InterfaceScale::InterfaceScale(QObject *parent)
    : QObject(parent)
{
    m_scale = savedScale();
    const bool ours = qEnvironmentVariableIsSet(kAppliedEnv);
    m_fromEnvironment = qEnvironmentVariableIsSet("QT_SCALE_FACTOR") && !ours;
    m_applied = qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
        ? qEnvironmentVariable("QT_SCALE_FACTOR").toDouble()
        : 1.0;
    if (m_applied <= 0) {
        m_applied = 1.0;
    }
}

bool InterfaceScale::restartNeeded() const
{
    return !m_fromEnvironment && !qFuzzyCompare(m_scale, m_applied);
}

void InterfaceScale::setScale(double value)
{
    const double next = nearestStep(value);
    if (qFuzzyCompare(next, m_scale)) {
        return;
    }
    m_scale = next;
    QSettings store = settings();
    store.setValue(QLatin1String(kKey), m_scale);
    emit scaleChanged();
}

double InterfaceScale::step(int direction)
{
    const int index = kSteps.indexOf(nearestStep(m_scale));
    const int next = qBound(0, index + (direction > 0 ? 1 : -1), int(kSteps.size()) - 1);
    setScale(kSteps.at(next));
    return m_scale;
}

double InterfaceScale::reset()
{
    setScale(1.0);
    return m_scale;
}

void InterfaceScale::restart()
{
    // Drop our own factor so the new process applies the saved one.
    QStringList environment = QProcess::systemEnvironment();
    if (qEnvironmentVariableIsSet(kAppliedEnv)) {
        environment.removeIf([](const QString &entry) {
            return entry.startsWith(QStringLiteral("QT_SCALE_FACTOR="))
                || entry.startsWith(QLatin1String(kAppliedEnv) + QLatin1Char('='));
        });
    }

    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(QCoreApplication::arguments().mid(1));
    process.setEnvironment(environment);
    if (process.startDetached()) {
        QCoreApplication::quit();
    }
}
