#pragma once

#include <QObject>
#include <QString>

// Small helpers the chat UI needs from native code.
class ChatUi final : public QObject
{
    Q_OBJECT
    // True when the system asks for less motion (macOS "Reduce motion", or
    // ARCHIVIST_REDUCED_MOTION=1). Chat keeps its layout changes but drops
    // springs, launches and pulses.
    Q_PROPERTY(bool reducedMotion READ reducedMotion CONSTANT)

public:
    explicit ChatUi(QObject *parent = nullptr);

    [[nodiscard]] bool reducedMotion() const { return m_reducedMotion; }

    // Puts `text` on the clipboard (with `html` as rich text when given).
    Q_INVOKABLE void copyText(const QString &text, const QString &html = QString()) const;

private:
    bool m_reducedMotion = false;
};
