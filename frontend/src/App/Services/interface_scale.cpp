#include "App/Services/interface_scale.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QList>
#include <QScreen>
#include <QWindow>
#include <QSettings>
#include <QtEnvironmentVariables>
#include <QtGui/private/qhighdpiscaling_p.h>

#include <cmath>

namespace {
// The same ladder browsers use, trimmed to sizes that still fit a window.
const QList<double> kSteps = {0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
const char kKey[] = "ui/interfaceScale";

QSettings settings()
{
    return QSettings(QSettings::UserScope, QStringLiteral("Archivist"), QStringLiteral("Archivist"));
}
}

QList<double> InterfaceScale::steps()
{
    return kSteps;
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

InterfaceScale::InterfaceScale(QObject *parent)
    : QObject(parent)
{
    m_fromEnvironment = qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
        || qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS");
    if (m_fromEnvironment) {
        return;
    }

    m_scale = nearestStep(settings().value(QLatin1String(kKey), 1.0).toDouble());
    m_startupScale = m_scale;
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        // A monitor plugged in later gets the same size.
        connect(app, &QGuiApplication::screenAdded, this, [this]() { apply(); });
        app->installEventFilter(this);
    }
    apply();
}

void InterfaceScale::apply()
{
    if (m_fromEnvironment || !qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }

    // Windows keep their size in logical pixels across a factor change, so
    // they would grow or shrink on screen. Like a browser, keep each window
    // where and as big as it is, and let the content reflow inside it.
    const double ratio = m_appliedScale > 0 ? m_appliedScale / m_scale : 1.0;
    struct Frame
    {
        QWindow *window;
        QRect geometry;
        QPoint screenOrigin;
    };
    QList<Frame> frames;
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (window->isVisible() && !(window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized))
            && window->type() != Qt::ToolTip && window->screen()) {
            frames.append({window, window->geometry(), window->screen()->geometry().topLeft()});
        }
    }

    // Re-evaluates each screen's geometry and DPI; windows on it get the new
    // device pixel ratio and re-render crisply.
    for (QScreen *screen : QGuiApplication::screens()) {
        QHighDpiScaling::setScreenFactor(screen, m_scale);
    }
    m_appliedScale = m_scale;

    if (qFuzzyCompare(ratio, 1.0)) {
        return;
    }
    for (const Frame &frame : frames) {
        const QPoint offset = frame.geometry.topLeft() - frame.screenOrigin;
        const QPoint origin = frame.window->screen() ? frame.window->screen()->geometry().topLeft() : frame.screenOrigin;
        const QRect target(
            origin + QPoint(qRound(offset.x() * ratio), qRound(offset.y() * ratio)),
            QSize(qRound(frame.geometry.width() * ratio), qRound(frame.geometry.height() * ratio))
        );
        // The native size barely changes, so the platform may not report a
        // resize and the content would keep its old logical size. Nudge it
        // by a pixel first so the window always relays out.
        frame.window->setGeometry(target.adjusted(0, 0, 1, 1));
        frame.window->setGeometry(target);
    }
}

bool InterfaceScale::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) {
        return QObject::eventFilter(watched, event);
    }

    auto *key = static_cast<QKeyEvent *>(event);
    // Qt::ControlModifier is Cmd on macOS. Shift is allowed because "+"
    // is Shift+= on most keyboards; Alt and the Mac Control key are not.
    const Qt::KeyboardModifiers modifiers = key->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (modifiers != Qt::ControlModifier) {
        return QObject::eventFilter(watched, event);
    }

    int direction = 2;
    switch (key->key()) {
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        direction = 1;
        break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        direction = -1;
        break;
    case Qt::Key_0:
    case Qt::Key_ParenRight:
        direction = 0;
        break;
    default:
        return QObject::eventFilter(watched, event);
    }

    // Claim the shortcut so no web page or text field acts on it, then act
    // once, on the key press itself.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    // Key events reach the filter once per receiver up the parent chain
    // (window, then item); act only on the first delivery of each press.
    if (key->timestamp() == m_lastKeyTimestamp && key->key() == m_lastKey) {
        return true;
    }
    m_lastKeyTimestamp = key->timestamp();
    m_lastKey = key->key();

    if (direction == 0) {
        reset();
    } else {
        step(direction);
    }
    return true;
}

bool InterfaceScale::canGrow() const
{
    return !m_fromEnvironment && m_scale < kSteps.last() - 0.001;
}

bool InterfaceScale::canShrink() const
{
    return !m_fromEnvironment && m_scale > kSteps.first() + 0.001;
}

void InterfaceScale::setScale(double value)
{
    if (m_fromEnvironment) {
        return;
    }
    const double next = nearestStep(value);
    if (qFuzzyCompare(next, m_scale)) {
        return;
    }
    m_scale = next;
    QSettings store = settings();
    store.setValue(QLatin1String(kKey), m_scale);
    apply();
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

double InterfaceScale::setTo(double value)
{
    setScale(value);
    return m_scale;
}
