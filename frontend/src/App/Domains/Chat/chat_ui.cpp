#include "chat_ui.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSettings>

namespace
{
bool systemReducedMotion()
{
    const QByteArray forced = qgetenv("ARCHIVIST_REDUCED_MOTION");

    if (!forced.isEmpty()) {
        return forced != "0";
    }

#ifdef Q_OS_MACOS
    // The Accessibility "Reduce motion" switch lives in this preferences
    // domain; reading it needs no entitlement outside the sandbox.
    const QSettings accessibility(QStringLiteral("com.apple"), QStringLiteral("universalaccess"));
    return accessibility.value(QStringLiteral("reduceMotion")).toBool();
#else
    return false;
#endif
}
}

ChatUi::ChatUi(QObject *parent)
    : QObject(parent)
    , m_reducedMotion(systemReducedMotion())
{
}

void ChatUi::copyText(const QString &text, const QString &html) const
{
    auto *mime = new QMimeData;
    mime->setText(text);

    if (!html.isEmpty()) {
        mime->setHtml(html);
    }

    QGuiApplication::clipboard()->setMimeData(mime);
}
