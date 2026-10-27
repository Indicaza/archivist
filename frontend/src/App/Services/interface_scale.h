#pragma once

#include <QObject>

// The size of the whole interface, like zooming a web page: Cmd/Ctrl + = - 0.
// It applies live by changing Qt's per-screen scale factor, so text, icons,
// panels and the web views (Monaco, terminals) all re-render crisply at the
// new size and the layout reflows. The size is remembered. A QT_SCALE_FACTOR
// or QT_SCREEN_SCALE_FACTORS set in the environment wins over it.
class InterfaceScale final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(bool canGrow READ canGrow NOTIFY scaleChanged)
    Q_PROPERTY(bool canShrink READ canShrink NOTIFY scaleChanged)
    Q_PROPERTY(bool controlledByEnvironment READ controlledByEnvironment CONSTANT)
    // The size the app started at: windows divide their default size by it
    // so they open at the same size on screen whatever the zoom.
    Q_PROPERTY(double startupScale READ startupScale CONSTANT)

public:
    // Needs the QGuiApplication; applies the saved size at once.
    explicit InterfaceScale(QObject *parent = nullptr);

    [[nodiscard]] double scale() const { return m_scale; }
    [[nodiscard]] double startupScale() const { return m_startupScale; }
    [[nodiscard]] bool canGrow() const;
    [[nodiscard]] bool canShrink() const;
    [[nodiscard]] bool controlledByEnvironment() const { return m_fromEnvironment; }

    // direction: +1 larger, -1 smaller. Returns the new scale.
    Q_INVOKABLE double step(int direction);
    Q_INVOKABLE double reset();
    Q_INVOKABLE double setTo(double value);

    [[nodiscard]] static double nearestStep(double value);
    [[nodiscard]] static QList<double> steps();

signals:
    void scaleChanged();

protected:
    // Catches Cmd/Ctrl + = - 0 before any focused item (a web view, an
    // editor) can swallow them.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setScale(double value);
    void apply();

    double m_scale = 1.0;
    double m_appliedScale = 1.0;
    double m_startupScale = 1.0;
    bool m_fromEnvironment = false;
    quint64 m_lastKeyTimestamp = 0;
    int m_lastKey = 0;
};
