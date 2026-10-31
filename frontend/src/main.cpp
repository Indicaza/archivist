#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QVariantMap>
#include <QtEnvironmentVariables>
#include <QtWebEngineQuick>
#include <memory>
#include <qqml.h>

#include "App/Domains/Agent/agent_store.h"
#include "App/Domains/Chat/chat_draft_store.h"
#include "App/Domains/Chat/chat_store.h"
#include "App/Domains/Chat/chat_text_item.h"
#include "App/Domains/Chat/chat_ui.h"
#include "App/Domains/Chat/message_block_parser.h"
#include "App/Domains/Collection/collection_store.h"
#include "App/Domains/Files/markdown_document_bridge.h"
#include "App/Domains/Files/document_preview_service.h"
#include "App/Domains/Library/library_store.h"
#include "App/Domains/Home/home_store.h"
#include "App/Domains/Shell/shell_store.h"
#include "App/Domains/Worktree/worktree_store.h"
#include "App/Domains/Documents/document_store.h"
#include "App/Domains/Documents/unsaved_recovery.h"
#include "App/Domains/Workspace/workspace_agent_bridge.h"
#include "App/Domains/Workspace/workspace_store.h"
#include "App/Services/runtime_trace_store.h"
#include "App/Services/workspace_state_store.h"
#include "App/Services/interface_scale.h"
#include "App/Services/live_recorder.h"
#include "App/Services/ui_lab.h"
#if defined(Q_OS_MACOS)
#include "App/Services/ui_lab_mac.h"
#endif
#include "Core/Domains/AI/ai_runtime.h"
#include "Core/Domains/AI/ai_settings.h"
#include "Core/Domains/Ide/ide_server.h"
#include "Core/Domains/Themes/code_theme_library.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/app_profile.h"
#include "Core/Support/login_environment.h"
#include "Core/core_runtime.h"

int main(int argc, char *argv[])
{
    // Startup stages are timed on the Observatory clock. They are reported
    // only when something collects events from the start (the UI Lab);
    // time before main() is not measured.
    const qint64 mainNs = LiveEvents::nowNs();
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    const bool uiLabBackground = UiLab::requested() && qEnvironmentVariableIsSet("ARCHIVIST_UI_LAB_BACKGROUND");
#if defined(Q_OS_MACOS)
    if (uiLabBackground) configureUiLabMacApplication();
#endif
    const qint64 appNs = LiveEvents::nowNs();

    QCoreApplication::setApplicationName("Archivist");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Archivist");
    QCoreApplication::setOrganizationDomain("archivist.local");

    QQuickStyle::setStyle("Basic");

    // A disposable test profile (ARCHIVIST_PROFILE_DIR) keeps settings,
    // the database and recovery files out of the person's own profile.
    if (!AppProfile::isolateSettings()) {
        qCritical().noquote() << "[Profile] Cannot create the test profile at" << AppProfile::directory();
        return 3;
    }

    // The Observatory UI Lab runs a scenario in this instance. It checks
    // the profile before the database is opened, and refuses otherwise.
    std::unique_ptr<UiLab> uiLab;
    if (UiLab::requested()) {
        uiLab = std::make_unique<UiLab>();
        QString error;
        if (!uiLab->load(CoreRuntime::defaultDatabasePath(), &error)) {
            qCritical().noquote() << "[UiLab] Refusing to run:" << error;
            return 3;
        }
        qInfo().noquote() << "[UiLab] Test profile:" << AppProfile::directory();
    }
    LiveEvents::span(QStringLiteral("startup"), QStringLiteral("qt-init"), mainNs, appNs);
    LiveEvents::span(QStringLiteral("startup"), QStringLiteral("profile"), appNs, LiveEvents::nowNs());

    // Finder launches get a bare PATH; the terminal and language servers
    // need the same tools the user has in their shell.
    LoginEnvironment::adoptLoginShellPath();

    CoreRuntime coreRuntime;

    const qint64 coreNs = LiveEvents::nowNs();
    const bool coreStarted = coreRuntime.start(CoreRuntime::defaultDatabasePath());
    // Opening the database and running its migrations.
    LiveEvents::span(QStringLiteral("startup"), QStringLiteral("core-runtime"), coreNs, LiveEvents::nowNs(), coreStarted ? 1 : 0,
                     {{QStringLiteral("schema"), coreRuntime.schemaVersion()}});
    if (coreStarted) {
        qInfo().noquote() << "[Core]" << coreRuntime.databasePath()
                          << "· schema v" + QString::number(coreRuntime.schemaVersion());
    } else {
        qWarning().noquote() << "[Core] Native data layer unavailable:"
                             << coreRuntime.errorMessage();
    }

    // Chat runs, the model tool loop and AI edit review run in process on
    // their own thread, against the same database.
    AIRuntime aiRuntime;

    {
        LiveEvents::Scope live(QStringLiteral("startup"), QStringLiteral("ai-runtime"));
        if (coreRuntime.ready() && !aiRuntime.start(coreRuntime.databasePath())) {
            live.setOk(false);
            qWarning().noquote() << "[AI] Native AI runtime unavailable.";
        }
    }

    // OpenAI key (Keychain), local model server address and model lists for
    // the Agent editor.
    AISettings aiSettings;

    if (coreRuntime.ready()) {
        aiSettings.setDatabasePath(coreRuntime.databasePath());
    }

    // The Observatory's live flight recorder. It belongs to the app (not to
    // the Observatory panel) and is created before the IDE host that serves
    // its controls, so it outlives every request.
    LiveRecorder liveRecorder(Observatory::defaultRoot());

    // The IDE panel's web bundle, terminals and language servers.
    IdeServer ideServer;
    ideServer.setLiveHandler([&liveRecorder](const QString &operation, const QByteArray &body, const QString &caller) {
        return liveRecorder.handle(operation, body, caller);
    });

    {
        LiveEvents::Scope live(QStringLiteral("startup"), QStringLiteral("ide-host"));
        if (coreRuntime.ready() && !ideServer.start(coreRuntime.databasePath())) {
            live.setOk(false);
            qWarning().noquote() << "[IDE] Native IDE host unavailable.";
        }
    }

    AgentStore agentStore;
    CollectionStore collectionStore;
    LibraryStore libraryStore;
    MarkdownDocumentBridge markdownDocumentBridge;
    ChatStore chatStore;
    // Unsent composer text, one draft per conversation.
    ChatDraftStore chatDrafts;
    MessageBlockParser messageBlocks;
    ChatUi chatUi;
    RuntimeTraceStore runtimeTraceStore;
    WorkspaceStateStore workspaceState;
    // Code colors imported from VS Code, TextMate and Sublime themes.
    CodeThemeLibrary codeThemes;
    // Git worktrees of the selected Library's repository, for the selector at
    // the bottom of the side panel.
    WorktreeStore worktreeStore;
    // Shared document buffers and the docking workspace (Anchors and views).
    DocumentStore documentStore;
    WorkspaceStore dockingStore;
    // Home or the workspace, the rail, recent items and the Home draft.
    ShellStore shellStore;
    HomeStore homeStore;
    documentStore.setSaver([&libraryStore](const QString &libraryId, const QString &fileId, const QString &text, const QString &expectedModifiedAt) {
        libraryStore.saveDocumentContent(libraryId, fileId, text, expectedModifiedAt);
    });
    QObject::connect(&libraryStore, &LibraryStore::fileSaved, &documentStore, &DocumentStore::noteFileSaved);
    QObject::connect(&libraryStore, &LibraryStore::fileSaveFailed, &documentStore, &DocumentStore::noteFileSaveFailed);
    dockingStore.setDocumentStore(&documentStore);
    // Agent tools reach the workspace from the AI thread through this. An
    // agent's conversation is "in front" when it is the selected chat.
    WorkspaceAgentBridge workspaceAgents(&dockingStore);
    workspaceAgents.setForegroundCheck([&chatStore](const QString &chatId) {
        return !chatId.isEmpty() && chatStore.selectedChatId() == chatId;
    });
    // Unsaved editor text survives crashes: it is kept beside the database
    // and comes back, marked unsaved, on the next start.
    UnsavedRecovery unsavedRecovery(
        &documentStore,
        AppProfile::dataLocation() + QStringLiteral("/recovery/unsaved.json")
    );
    const int recoveredDocuments = unsavedRecovery.restore();
    if (recoveredDocuments > 0) {
        qInfo().noquote() << "[Documents] Recovered unsaved edits in" << recoveredDocuments << "file(s)";
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &unsavedRecovery, &UnsavedRecovery::flush);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &chatDrafts, &ChatDraftStore::flush);
    // A deleted conversation takes its unsent draft with it.
    QObject::connect(&chatStore, &ChatStore::chatDeleted, &chatDrafts, &ChatDraftStore::clearDraft);
    QObject::connect(&chatStore, &ChatStore::chatDeleted, &dockingStore, &WorkspaceStore::forgetComposerConversation);
    InterfaceScale interfaceScale;

    QObject::connect(
        &chatStore,
        &ChatStore::editTransactionSynchronized,
        &libraryStore,
        [&libraryStore](const QString &libraryId) {
            if (libraryStore.selectedLibraryId() == libraryId) {
                libraryStore.refreshSelectedFiles();
            }
        }
    );
    // The worktree selector follows the selected Library.
    QObject::connect(
        &libraryStore,
        &LibraryStore::selectedLibraryIdChanged,
        &worktreeStore,
        [&libraryStore, &worktreeStore]() {
            worktreeStore.setLibraryId(libraryStore.selectedLibraryId());
        }
    );
    QObject::connect(
        &libraryStore,
        &LibraryStore::gitStatusChanged,
        &worktreeStore,
        &WorktreeStore::noteSelectedWorkspaceChanged
    );
    QObject::connect(
        &aiRuntime,
        &AIRuntime::runEvent,
        &worktreeStore,
        &WorktreeStore::handleRunEvent
    );
    QObject::connect(
        &worktreeStore,
        &WorktreeStore::worktreeOpened,
        &libraryStore,
        [&libraryStore, &collectionStore](const QVariantMap &library, bool) {
            libraryStore.includeLibrary(library);
            collectionStore.refreshScope();
        }
    );
    QObject::connect(
        &worktreeStore,
        &WorktreeStore::librariesChanged,
        &libraryStore,
        [&libraryStore, &collectionStore]() {
            libraryStore.refresh();
            collectionStore.refreshScope();
        }
    );
    worktreeStore.setLibraryId(libraryStore.selectedLibraryId());
    qmlRegisterType<DocumentPreviewService>(
        "Archivist.Services",
        1,
        0,
        "DocumentPreviewService"
    );
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "Core", &coreRuntime);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "IdeServer", &ideServer);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "LiveRecorder", &liveRecorder);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "AISettings", &aiSettings);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "AgentStore", &agentStore);
    qmlRegisterSingletonInstance(
        "Archivist.Services",
        1,
        0,
        "CollectionStore",
        &collectionStore
    );
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "LibraryStore", &libraryStore);
    qmlRegisterSingletonInstance(
        "Archivist.Services",
        1,
        0,
        "MarkdownDocumentBridge",
        &markdownDocumentBridge
    );
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "ChatStore", &chatStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "ChatDrafts", &chatDrafts);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "MessageBlocks", &messageBlocks);
    qmlRegisterType<ChatTextItem>("Archivist.Services", 1, 0, "ChatText");
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "ChatUi", &chatUi);
    qmlRegisterSingletonInstance(
        "Archivist.Services",
        1,
        0,
        "RuntimeTrace",
        &runtimeTraceStore
    );
    qmlRegisterSingletonInstance(
        "Archivist.Services",
        1,
        0,
        "WorkspaceState",
        &workspaceState
    );

    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "CodeThemes", &codeThemes);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "WorktreeStore", &worktreeStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "Documents", &documentStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "InterfaceScale", &interfaceScale);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "Docking", &dockingStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "Shell", &shellStore);
    qmlRegisterSingletonInstance("Archivist.Services", 1, 0, "HomeStore", &homeStore);

    QQmlApplicationEngine engine;

    QVariantMap diagnostics;
    diagnostics.insert(
        "chatViewportTrace",
        qEnvironmentVariableIsSet("ARCHIVIST_CHAT_TRACE")
    );
    diagnostics.insert(
        "chatDisableHistoryPrefetch",
        qEnvironmentVariableIsSet("ARCHIVIST_CHAT_NO_HISTORY")
    );
    diagnostics.insert(
        "chatDisableViewportSave",
        qEnvironmentVariableIsSet("ARCHIVIST_CHAT_NO_VIEWPORT_SAVE")
    );
    diagnostics.insert(
        "chatPlainMessages",
        qEnvironmentVariableIsSet("ARCHIVIST_CHAT_PLAIN_MESSAGES")
    );
    // Escape hatch while the docking workspace beds in: the old single
    // workspace with its tab strip.
    diagnostics.insert(
        "legacyWorkspace",
        qEnvironmentVariableIsSet("ARCHIVIST_LEGACY_WORKSPACE")
    );
    diagnostics.insert("uiLab", uiLab != nullptr);
    diagnostics.insert("uiLabBackground", uiLabBackground);
    engine.rootContext()->setContextProperty("UiLab", uiLab.get());
    engine.rootContext()->setContextProperty(
        "ArchivistDiagnostics",
        diagnostics
    );

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    // Marks when the root object exists inside qml-load, splitting compile
    // and create from what runs after it before loadFromModule returns.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [uiLabBackground](QObject *object, const QUrl &) {
        LiveEvents::marker(QStringLiteral("startup"), QStringLiteral("qml-root-created"), {}, object ? 1 : 0);
#if defined(Q_OS_MACOS)
        if (uiLabBackground) configureUiLabMacWindow(qobject_cast<QQuickWindow *>(object));
#else
        Q_UNUSED(uiLabBackground);
#endif
    });

    {
        // Compiling and creating the QML scene (the window and everything it
        // creates synchronously).
        LiveEvents::Scope live(QStringLiteral("startup"), QStringLiteral("qml-load"));
        const QString qmlSourceDir = qEnvironmentVariable("ARCHIVIST_QML_SOURCE_DIR");
        if (!qmlSourceDir.isEmpty()) {
            const QString source = QDir(qmlSourceDir).absoluteFilePath(QStringLiteral("App.qml"));
            if (!QFileInfo::exists(source)) {
                qCritical().noquote() << "[Dev QML] App.qml does not exist at" << source;
                return 2;
            }
            qInfo().noquote() << "[Dev QML] Loading source files from" << qmlSourceDir;
            engine.load(QUrl::fromLocalFile(source));
        } else {
            engine.loadFromModule("Archivist", "App");
        }
    }
    LiveEvents::marker(QStringLiteral("startup"), QStringLiteral("enter-event-loop"));

    return app.exec();
}
