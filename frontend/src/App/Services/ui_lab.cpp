#include "App/Services/ui_lab.h"

#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/app_profile.h"
#include "Core/core_runtime.h"

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
    m_probe.setTimerType(Qt::PreciseTimer);
    m_probe.setInterval(kProbeMs);
    connect(&m_probe, &QTimer::timeout, this, &UiLab::probe);
}

UiLab::~UiLab()
{
    if (m_sinkInstalled) {
        LiveEvents::setSink(nullptr);
    }
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

    // From here on, what the app reports through LiveEvents is collected
    // (startup included). The live recorder refuses to start meanwhile.
    if (!LiveEvents::recording()) {
        LiveEvents::setSink(this);
        m_sinkInstalled = true;
    }
    m_lastProbeNs = LiveEvents::nowNs();
    m_probe.start();
    return true;
}

double UiLab::nowMs() const
{
    return LiveEvents::nowNs() / 1e6;
}

void UiLab::setAction(int index)
{
    m_action.store(index, std::memory_order_relaxed);
}

void UiLab::probe()
{
    const qint64 now = LiveEvents::nowNs();
    const double lateMs = (now - m_lastProbeNs) / 1e6 - kProbeMs;
    m_lastProbeNs = now;
    if (lateMs < kStallMs) {
        return;
    }
    QMutexLocker lock(&m_eventsMutex);
    if (m_stalls.size() >= kMaxStalls) {
        ++m_dropped;
        return;
    }
    // The stall ended now and began about lateMs earlier.
    m_stalls.append({
        {QStringLiteral("atMs"), now / 1e6 - lateMs},
        {QStringLiteral("lateMs"), lateMs},
        {QStringLiteral("action"), m_action.load(std::memory_order_relaxed)},
    });
}

void UiLab::addEvent(LiveEvents::Event event)
{
    const int action = m_action.load(std::memory_order_relaxed);
    QMutexLocker lock(&m_eventsMutex);
    if (m_events.size() >= kMaxEvents) {
        ++m_dropped;
        return;
    }
    m_events.append({std::move(event), action});
}

quint64 UiLab::openSpan(LiveEvents::Event event)
{
    const int action = m_action.load(std::memory_order_relaxed);
    QMutexLocker lock(&m_eventsMutex);
    if (m_events.size() + m_open.size() >= kMaxEvents) {
        ++m_dropped;
        return 0;
    }
    const quint64 token = m_nextToken++;
    m_open.insert(token, {std::move(event), action});
    return token;
}

void UiLab::closeSpan(quint64 token, qint64 endNs, int ok, const QVariantMap &fields)
{
    QMutexLocker lock(&m_eventsMutex);
    auto it = m_open.find(token);
    if (it == m_open.end()) {
        return;
    }
    Recorded recorded = it.value();
    m_open.erase(it);
    recorded.event.endNs = endNs;
    recorded.event.ok = ok;
    for (auto field = fields.constBegin(); field != fields.constEnd() && recorded.event.fields.size() < 8; ++field) {
        recorded.event.fields.insert(field.key(), field.value());
    }
    m_events.append(std::move(recorded));
}

QVariantMap UiLab::operations() const
{
    QMutexLocker lock(&m_eventsMutex);
    QVariantList list;
    list.reserve(m_events.size());
    auto toMap = [](const Recorded &recorded) {
        const LiveEvents::Event &event = recorded.event;
        QVariantMap entry{
            {QStringLiteral("kind"), event.kind},
            {QStringLiteral("category"), event.category},
            {QStringLiteral("name"), event.name},
            {QStringLiteral("startMs"), event.startNs / 1e6},
            {QStringLiteral("action"), recorded.action},
        };
        entry.insert(QStringLiteral("endMs"), event.endNs >= 0 ? QVariant(event.endNs / 1e6) : QVariant());
        entry.insert(QStringLiteral("ok"), event.ok < 0 ? QVariant() : QVariant(event.ok == 1));
        if (!event.fields.isEmpty()) {
            entry.insert(QStringLiteral("fields"), event.fields);
        }
        return entry;
    };
    for (const Recorded &recorded : m_events) {
        list.append(toMap(recorded));
    }
    QVariantList open;
    for (const Recorded &recorded : m_open) {
        open.append(toMap(recorded));
    }
    QVariantList stalls;
    for (const QVariantMap &stall : m_stalls) {
        stalls.append(stall);
    }
    return {
        {QStringLiteral("clock"), QStringLiteral("LiveEvents::nowNs (monotonic, ms since the process first read it)")},
        {QStringLiteral("collecting"), m_sinkInstalled},
        {QStringLiteral("probeMs"), kProbeMs},
        {QStringLiteral("stallMs"), kStallMs},
        {QStringLiteral("operations"), list},
        {QStringLiteral("open"), open},
        {QStringLiteral("stalls"), stalls},
        {QStringLiteral("dropped"), m_dropped},
    };
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
        if (m_frames.isEmpty()) {
            LiveEvents::marker(QStringLiteral("startup"), QStringLiteral("first-frame"));
        }
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

QString UiLab::fixtureMessage(quint32 seed, int index)
{
    // A small deterministic generator (no std:: distributions, whose output
    // differs between standard libraries).
    quint32 state = seed * 2654435761u + static_cast<quint32>(index) * 40503u + 1u;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    static const QStringList words{
        QStringLiteral("archive"), QStringLiteral("layout"), QStringLiteral("render"), QStringLiteral("panel"),
        QStringLiteral("signal"), QStringLiteral("model"), QStringLiteral("thread"), QStringLiteral("index"),
        QStringLiteral("frame"), QStringLiteral("cache"), QStringLiteral("library"), QStringLiteral("delegate"),
        QStringLiteral("binding"), QStringLiteral("workspace"), QStringLiteral("query"), QStringLiteral("token"),
    };
    auto sentence = [&](int length) {
        QStringList parts;
        for (int i = 0; i < length; ++i) {
            parts.append(words.at(static_cast<int>(next() % words.size())));
        }
        QString text = parts.join(QLatin1Char(' '));
        text[0] = text[0].toUpper();
        return text + QLatin1Char('.');
    };
    QStringList blocks;
    const bool assistant = index % 2 == 1;
    if (!assistant) {
        blocks.append(sentence(8 + static_cast<int>(next() % 20)));
        return blocks.join(QStringLiteral("\n\n"));
    }
    switch (index % 10) {
    case 1: // a long paragraph
        for (int i = 0; i < 12; ++i) {
            blocks.append(sentence(14 + static_cast<int>(next() % 10)));
        }
        return blocks.join(QLatin1Char(' '));
    case 3: { // Markdown: heading, list, emphasis
        blocks.append(QStringLiteral("## ") + sentence(4));
        QStringList list;
        for (int i = 0; i < 8; ++i) {
            list.append(QStringLiteral("- **") + words.at(i * 2) + QStringLiteral("**: ") + sentence(10));
        }
        blocks.append(list.join(QLatin1Char('\n')));
        blocks.append(sentence(20));
        return blocks.join(QStringLiteral("\n\n"));
    }
    case 5: { // a code block
        blocks.append(sentence(12));
        QStringList code{QStringLiteral("```cpp")};
        for (int i = 0; i < 24; ++i) {
            code.append(QStringLiteral("    auto %1_%2 = load(%3, %4);").arg(words.at(static_cast<int>(next() % words.size()))).arg(i).arg(next() % 1000).arg(next() % 100));
        }
        code.append(QStringLiteral("```"));
        blocks.append(code.join(QLatin1Char('\n')));
        return blocks.join(QStringLiteral("\n\n"));
    }
    default:
        blocks.append(sentence(20 + static_cast<int>(next() % 30)));
        blocks.append(sentence(20 + static_cast<int>(next() % 30)));
        return blocks.join(QStringLiteral("\n\n"));
    }
}

bool UiLab::seedChats(const QVariantMap &spec)
{
    CoreRuntime *core = CoreRuntime::active();
    if (!core || !AppProfile::active() || !AppProfile::contains(core->databasePath())) {
        m_seed = {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("error"), QStringLiteral("no test-profile database")}};
        return false;
    }
    m_seed = {{QStringLiteral("state"), QStringLiteral("running")}};
    const QString libraryRoot = QDir(AppProfile::directory()).filePath(QStringLiteral("ui-lab-library"));
    QDir().mkpath(libraryRoot);
    const quint32 seed = spec.value(QStringLiteral("seed"), 1).toUInt();
    const QVariantList chats = spec.value(QStringLiteral("chats")).toList();
    core->write(
        [libraryRoot, seed, chats](CoreDatabase &database) {
            LiveEvents::Scope live(QStringLiteral("ui-lab"), QStringLiteral("seed-chats"));
            const QVariantMap library = LibraryRepository::createLibrary(database, {
                {QStringLiteral("rootPath"), libraryRoot},
                {QStringLiteral("name"), QStringLiteral("UI Lab")},
            });
            const QString libraryId = library.value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
            QVariantMap ids;
            int total = 0;
            for (const QVariant &entry : chats) {
                const QVariantMap chat = entry.toMap();
                const QVariantMap created = ChatRepository::createChat(database, {
                    {QStringLiteral("libraryId"), libraryId},
                    {QStringLiteral("title"), chat.value(QStringLiteral("title")).toString()},
                });
                const QString chatId = created.value(QStringLiteral("id")).toString();
                const int count = qBound(0, chat.value(QStringLiteral("messages")).toInt(), 5000);
                for (int index = 0; index < count; ++index) {
                    ChatRepository::insertMessage(database, chatId, index % 2 ? QStringLiteral("assistant") : QStringLiteral("user"),
                                                  fixtureMessage(seed, index), QStringLiteral("complete"));
                }
                total += count;
                ids.insert(chat.value(QStringLiteral("key")).toString(), chatId);
            }
            // Nothing selected, so the scenario chooses what opens first.
            ChatRepository::selectChat(database, QString());
            return QVariant(QVariantMap{{QStringLiteral("chats"), ids}, {QStringLiteral("messages"), total}});
        },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                m_seed = {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("error"), result.errorMessage}};
                return;
            }
            QVariantMap seeded = result.value.toMap();
            seeded.insert(QStringLiteral("state"), QStringLiteral("done"));
            m_seed = seeded;
        });
    return true;
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
