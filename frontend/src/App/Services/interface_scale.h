#pragma once

#include <QObject>

// The size of the whole interface, like zooming a web page: Cmd/Ctrl + = - 0.
// The theme multiplies its type sizes and bar heights by `scale`, and the web
// views (Monaco, terminals) zoom their pages by it, so text grows everywhere
// and the layout reflows inside the same window. The size is remembered.
class InterfaceScale final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(bool canGrow READ canGrow NOTIFY scaleChanged)
    Q_PROPERTY(bool canShrink READ canShrink NOTIFY scaleChanged)

public:
    explicit InterfaceScale(QObject *parent = nullptr);

    [[nodiscard]] double scale() const { return m_scale; }
    [[nodiscard]] bool canGrow() const;
    [[nodiscard]] bool canShrink() const;

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
    // editor, a file view) can swallow them.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setScale(double value);

    double m_scale = 1.0;
    quint64 m_lastKeyTimestamp = 0;
    int m_lastKey = 0;
};
