#pragma once

#include <QObject>

// The size of the whole interface (Ctrl+Alt+= / - / 0). Qt applies a scale
// factor when the app starts, so a change takes effect on restart; the
// unsaved-edit recovery makes that restart safe. A QT_SCALE_FACTOR set in the
// environment wins over the setting.
class InterfaceScale final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(double appliedScale READ appliedScale CONSTANT)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY scaleChanged)
    Q_PROPERTY(bool controlledByEnvironment READ controlledByEnvironment CONSTANT)

public:
    explicit InterfaceScale(QObject *parent = nullptr);

    // Call before QGuiApplication exists: applies the saved scale.
    static void applyAtStartup();

    [[nodiscard]] double scale() const { return m_scale; }
    [[nodiscard]] double appliedScale() const { return m_applied; }
    [[nodiscard]] bool restartNeeded() const;
    [[nodiscard]] bool controlledByEnvironment() const { return m_fromEnvironment; }

    // direction: +1 larger, -1 smaller. Returns the new scale.
    Q_INVOKABLE double step(int direction);
    Q_INVOKABLE double reset();
    Q_INVOKABLE void restart();

    [[nodiscard]] static double nearestStep(double value);

signals:
    void scaleChanged();

private:
    void setScale(double value);

    double m_scale = 1.0;
    double m_applied = 1.0;
    bool m_fromEnvironment = false;
};
