#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

class QQuickWindow;

// The Observatory UI Lab's hook into the running app. It exists only when
// ARCHIVIST_UI_LAB_SCENARIO is set, and only in a disposable profile
// (ARCHIVIST_PROFILE_DIR, see AppProfile); otherwise the app refuses to
// start. The scenario itself runs in QML (UiLabDriver.qml); this gives it a
// monotonic clock, the window's real frame swaps, synthetic mouse input
// delivered through the window, and a place to write results.
class UiLab final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap scenario READ scenario CONSTANT)

public:
    explicit UiLab(QObject *parent = nullptr);

    // ARCHIVIST_UI_LAB_SCENARIO is set.
    [[nodiscard]] static bool requested();

    // Reads the scenario and checks, before anything opens a database, that
    // the database and settings are inside the test profile.
    bool load(const QString &databasePath, QString *error);

    [[nodiscard]] QVariantMap scenario() const { return m_scenario; }

    // Milliseconds since the lab started, from a monotonic clock.
    Q_INVOKABLE double nowMs() const;

    // Records every frame the window swaps (QQuickWindow::frameSwapped, on
    // the render thread, timed there).
    Q_INVOKABLE void attachWindow(QObject *window);
    // Frames swapped in (fromMs, toMs]: count, first (ms after fromMs),
    // last (ms after fromMs), maxIntervalMs between consecutive swaps.
    Q_INVOKABLE QVariantMap frames(double fromMs, double toMs) const;
    Q_INVOKABLE double lastFrameMs() const;

    // Where this instance keeps its data, and whether each is in the profile.
    Q_INVOKABLE QVariantMap isolation() const;

    // The first item under the window with this objectName, or null.
    Q_INVOKABLE QObject *findItem(const QString &objectName) const;
    // Sends a left-button mouse event ("press", "move", "release") to the
    // window at a scene position, as the windowing system would.
    Q_INVOKABLE bool mouse(const QString &type, double sceneX, double sceneY);

    // Writes the results (ARCHIVIST_UI_LAB_OUTPUT), replacing the file
    // atomically, so a crash leaves the last complete copy.
    Q_INVOKABLE bool writeResult(const QVariantMap &result);
    Q_INVOKABLE void log(const QString &message) const;
    Q_INVOKABLE void finish(int exitCode);

private:
    QElapsedTimer m_clock;
    QPointer<QQuickWindow> m_window;
    mutable QMutex m_framesMutex;
    QList<double> m_frames;
    QVariantMap m_scenario;
    QString m_databasePath;
    QString m_outputPath;
    bool m_buttonDown = false;
};
