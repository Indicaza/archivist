#include "App/Services/interface_scale.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QList>
#include <QSettings>

#include <cmath>

namespace {
// The same ladder browsers use, trimmed to sizes the layout still fits.
const QList<double> kSteps = {0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
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
    m_scale = nearestStep(settings().value(QLatin1String(kKey), 1.0).toDouble());
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->installEventFilter(this);
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

    int direction = 0;
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
    // A key press can reach the filter more than once (window, then item);
    // act on the first delivery only.
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
    return m_scale < kSteps.last() - 0.001;
}

bool InterfaceScale::canShrink() const
{
    return m_scale > kSteps.first() + 0.001;
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

double InterfaceScale::setTo(double value)
{
    setScale(value);
    return m_scale;
}
