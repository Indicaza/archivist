#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QVariantMap>
#include <QtEnvironmentVariables>
#include <QtWebEngineQuick>
#include <qqml.h>

#include "App/Domains/Agent/agent_store.h"
#include "App/Domains/Chat/chat_store.h"
#include "App/Domains/Chat/chat_text_item.h"
#include "App/Domains/Chat/chat_ui.h"
#include "App/Domains/Chat/message_block_parser.h"
#include "App/Domains/Collection/collection_store.h"
#include "App/Domains/Files/markdown_document_bridge.h"
#include "App/Domains/Files/document_preview_service.h"
#include "App/Domains/Library/library_store.h"
#include "App/Domains/Shell/shell_store.h"
#include "App/Domains/Worktree/worktree_store.h"
#include "App/Domains/Documents/document_store.h"
#include "App/Domains/Documents/unsaved_recovery.h"
#include "App/Domains/Workspace/workspace_agent_bridge.h"
#include "App/Domains/Workspace/workspace_store.h"
#include "App/Services/runtime_trace_store.h"
#include "App/Services/workspace_state_store.h"
#include "App/Services/interface_scale.h"
#include "Core/Domains/AI/ai_runtime.h"
#include "Core/Domains/AI/ai_settings.h"
#include "Core/Domains/Ide/ide_server.h"
#include "Core/Domains/Themes/code_theme_library.h"
#include "Core/Support/login_environment.h"
#include "Core/core_runtime.h"

int main(int argc, char *argv[])
{
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName("Archivist");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Archivist");
    QCoreApplication::setOrganizationDomain("archivist.local");

    QQuickStyle::setStyle("Basic");

    // Finder launches get a bare PATH; the terminal and language servers
    // need the same tools the user has in their shell.
    LoginEnvironment::adoptLoginShellPath();

    CoreRuntime coreRuntime;

    if (coreRuntime.start(CoreRuntime::defaultDatabasePath())) {
        qInfo().noquote() << "[Core]" << coreRuntime.databasePath()
                          << "· schema v" + QString::number(coreRuntime.schemaVersion());
    } else {
        qWarning().noquote() << "[Core] Native data layer unavailable:"
                             << coreRuntime.errorMessage();
    }

    // Chat runs, the model tool loop and AI edit review run in process on
    // their own thread, against the same database.
    AIRuntime aiRuntime;

    if (coreRuntime.ready() && !aiRuntime.start(coreRuntime.databasePath())) {
        qWarning().noquote() << "[AI] Native AI runtime unavailable.";
    }

    // OpenAI key (Keychain), local model server address and model lists for
    // the Agent editor.
    AISettings aiSettings;

    if (coreRuntime.ready()) {
        aiSettings.setDatabasePath(coreRuntime.databasePath());
    }

    // The IDE panel's web bundle, terminals and language servers.
    IdeServer ideServer;

    if (coreRuntime.ready() && !ideServer.start(coreRuntime.databasePath())) {
        qWarning().noquote() << "[IDE] Native IDE host unavailable.";
    }

    AgentStore agentStore;
    CollectionStore collectionStore;
    LibraryStore libraryStore;
    MarkdownDocumentBridge markdownDocumentBridge;
    ChatStore chatStore;
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
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery/unsaved.json")
    );
    const int recoveredDocuments = unsavedRecovery.restore();
    if (recoveredDocuments > 0) {
        qInfo().noquote() << "[Documents] Recovered unsaved edits in" << recoveredDocuments << "file(s)";
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &unsavedRecovery, &UnsavedRecovery::flush);
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

    engine.loadFromModule("Archivist", "App");

    return app.exec();
}
