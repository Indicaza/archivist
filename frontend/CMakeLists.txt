cmake_minimum_required(VERSION 3.24)

project(ArchivistQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Network Pdf Quick QuickControls2 QuickDialogs2 WebChannel WebEngineQuick)

qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0004 NEW)

file(GLOB_RECURSE ARCHIVIST_ICON_ASSETS
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "qml/App/Icons/Assets/*.svg"
    "qml/App/Icons/Assets/*.json"
    "qml/App/Icons/Assets/*.ttf"
)

find_package(SQLite3 REQUIRED)

# In-process data layer that replaces the Node API one domain at a time.
qt_add_library(archivist-core STATIC
    src/Core/core_error.h
    src/Core/core_runtime.cpp
    src/Core/core_runtime.h
    src/Core/Database/core_database.cpp
    src/Core/Database/core_database.h
    src/Core/Database/sqlite_statement.cpp
    src/Core/Database/sqlite_statement.h
    src/Core/Domains/AppState/app_state_repository.cpp
    src/Core/Domains/AppState/app_state_repository.h
    src/Core/Domains/Agents/agent_defaults.h
    src/Core/Domains/Agents/agent_repository.cpp
    src/Core/Domains/Agents/agent_repository.h
    src/Core/Domains/Agents/agent_schemas.cpp
    src/Core/Domains/Agents/agent_schemas.h
    src/Core/Domains/Agents/context_compiler_catalog.cpp
    src/Core/Domains/Agents/context_compiler_catalog.h
    src/Core/Domains/Chats/chat_repository.cpp
    src/Core/Domains/Chats/chat_repository.h
    src/Core/Domains/AI/ai_run_repository.cpp
    src/Core/Domains/AI/ai_run_repository.h
    src/Core/Domains/AI/context_compilers.cpp
    src/Core/Domains/AI/context_compilers.h
    src/Core/Domains/AI/agent_instructions.cpp
    src/Core/Domains/AI/agent_instructions.h
    src/Core/Domains/AI/library_retrieval.cpp
    src/Core/Domains/AI/library_retrieval.h
    src/Core/Domains/AI/chat_evidence.cpp
    src/Core/Domains/AI/chat_evidence.h
    src/Core/Domains/AI/edit_proposals.cpp
    src/Core/Domains/AI/edit_proposals.h
    src/Core/Domains/AI/ai_tools.cpp
    src/Core/Domains/AI/ai_tools.h
    src/Core/Domains/AI/edit_transactions.cpp
    src/Core/Domains/AI/edit_transactions.h
    src/Core/Domains/AI/ai_provider.cpp
    src/Core/Domains/AI/ai_provider.h
    src/Core/Domains/AI/ai_runtime.cpp
    src/Core/Domains/AI/ai_runtime.h
    src/Core/Domains/AI/ai_settings.cpp
    src/Core/Domains/AI/ai_settings.h
    src/Core/Domains/Libraries/library_edit_inspection.cpp
    src/Core/Domains/Libraries/library_edit_inspection.h
    src/Core/Support/transactions.h
    src/Core/Domains/Ide/ide_http_error.h
    src/Core/Domains/Ide/web_socket_connection.cpp
    src/Core/Domains/Ide/web_socket_connection.h
    src/Core/Domains/Ide/terminal_sessions.cpp
    src/Core/Domains/Ide/terminal_sessions.h
    src/Core/Domains/Ide/language_support.cpp
    src/Core/Domains/Ide/language_support.h
    src/Core/Domains/Ide/ide_server.cpp
    src/Core/Domains/Ide/ide_server.h
    src/Core/Support/json_values.h
    src/Core/Domains/Collections/collection_repository.cpp
    src/Core/Domains/Collections/collection_repository.h
    src/Core/Domains/Libraries/library_file_operations.cpp
    src/Core/Domains/Libraries/library_file_operations.h
    src/Core/Domains/Libraries/library_file_policy.cpp
    src/Core/Domains/Libraries/library_file_policy.h
    src/Core/Domains/Libraries/library_file_repository.cpp
    src/Core/Domains/Libraries/library_file_repository.h
    src/Core/Domains/Libraries/library_files.cpp
    src/Core/Domains/Libraries/library_files.h
    src/Core/Domains/Libraries/library_repository.cpp
    src/Core/Domains/Libraries/library_repository.h
    src/Core/Domains/Libraries/library_text_chunker.cpp
    src/Core/Domains/Libraries/library_text_chunker.h
    src/Core/Domains/Models/model_catalog_cache.cpp
    src/Core/Domains/Models/model_catalog_cache.h
    src/Core/Support/core_values.h
    src/Core/Support/js_compat.h
    src/Core/Support/secret_store.cpp
    src/Core/Support/secret_store.h
    src/Core/Support/login_environment.cpp
    src/Core/Support/login_environment.h
)

file(GLOB ARCHIVIST_CORE_MIGRATIONS
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/Core/Database/Migrations/*.sql"
    "src/Core/Database/Migrations/*.json"
)

qt_add_resources(archivist-core "archivist-core-migrations"
    PREFIX "/archivist/core/migrations"
    BASE "src/Core/Database/Migrations"
    FILES ${ARCHIVIST_CORE_MIGRATIONS}
)

cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH ARCHIVIST_REPOSITORY_ROOT)

target_include_directories(archivist-core PUBLIC src)
# Dev builds fall back to the repository's backend/data/archivist.db, its
# frontend/dist/ide and its node_modules/.bin. Release packages turn this off
# so the app only uses its own bundle and Application Support.
option(ARCHIVIST_USE_REPOSITORY_PATHS "Let the app fall back to files in this repository" ON)

if(ARCHIVIST_USE_REPOSITORY_PATHS)
    target_compile_definitions(archivist-core PRIVATE
        ARCHIVIST_REPOSITORY_ROOT="${ARCHIVIST_REPOSITORY_ROOT}"
    )
endif()

# CMake 4.1 renamed the SQLite target; older versions only have the old name.
if(TARGET SQLite3::SQLite3)
    set(ARCHIVIST_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(ARCHIVIST_SQLITE_TARGET SQLite::SQLite3)
endif()

target_link_libraries(archivist-core PUBLIC Qt6::Core Qt6::Network ${ARCHIVIST_SQLITE_TARGET})

# forkpty() for the IDE terminal lives in libutil outside macOS.
if(UNIX AND NOT APPLE)
    target_link_libraries(archivist-core PUBLIC util)
endif()

# SecretStore keeps the API key in the macOS Keychain.
if(APPLE)
    target_link_libraries(archivist-core PUBLIC "-framework Security" "-framework CoreFoundation")
endif()

qt_add_executable(archivist-qt
    src/main.cpp
    src/App/Domains/Agent/agent_store.cpp
    src/App/Domains/Agent/agent_store.h
    src/App/Domains/Chat/chat_message_model.cpp
    src/App/Domains/Chat/chat_message_model.h
    src/App/Domains/Chat/message_block_parser.cpp
    src/App/Domains/Chat/message_block_parser.h
    src/App/Domains/Chat/chat_text_item.cpp
    src/App/Domains/Chat/chat_text_item.h
    src/App/Domains/Chat/chat_text_layout.cpp
    src/App/Domains/Chat/chat_text_layout.h
    src/App/Domains/Chat/chat_store.cpp
    src/App/Domains/Chat/chat_store.h
    src/App/Domains/Chat/chat_syntax.cpp
    src/App/Domains/Chat/chat_syntax.h
    src/App/Domains/Chat/chat_ui.cpp
    src/App/Domains/Chat/chat_ui.h
    src/App/Domains/Collection/collection_store.cpp
    src/App/Domains/Collection/collection_store.h
    src/App/Domains/Files/markdown_document_bridge.cpp
    src/App/Domains/Files/markdown_document_bridge.h
    src/App/Domains/Files/document_preview_service.cpp
    src/App/Domains/Files/document_preview_service.h
    src/App/Domains/Library/library_store.cpp
    src/App/Domains/Library/library_store.h
    src/App/Services/runtime_trace_store.cpp
    src/App/Services/runtime_trace_store.h
    src/App/Services/workspace_state_store.cpp
    src/App/Services/workspace_state_store.h
)

set_source_files_properties(
    qml/App/Icons/SetiFont.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(archivist-qt
    URI Archivist
    VERSION 1.0
    QML_FILES
        qml/App.qml
        qml/App/Theme/Theme.qml
        qml/App/Icons/AppIcon.qml
        qml/App/Icons/LanguageIcon.qml
        qml/App/Icons/SetiFont.qml
        qml/App/Icons/IconButton.qml
        qml/App/Icons/IconRegistry.js
        qml/App/Icons/LanguageRegistry.js
        qml/App/Icons/GeneratedSetiRegistry.js
        qml/App/Files/FileIdentity.js
        qml/App/Files/RendererRegistry.js
        qml/App/Files/Renderers/PlainTextRenderer.qml
        qml/App/Files/Renderers/MarkdownRenderer.qml
        qml/App/Files/Renderers/ImageRenderer.qml
        qml/App/Files/Renderers/PdfRenderer.qml
        qml/App/Files/Renderers/DocumentLoadingState.qml
        qml/App/Topbar/Topbar.qml
        qml/App/Workbench/WorkbenchShell/WorkbenchShell.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/ActivityRail.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/RailButton/RailButton.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerDock.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceDragSession.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerItem/ExplorerItem.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/WorkspaceNavigator.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/SidebarBandHeader.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionBand.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/ChatBand.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/ChatAgentRow.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionEditor/CollectionEditor.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionEditor/CollectionEditorHeader.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionEditor/CollectionEditorFooter.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionEditor/CollectionMembershipCard.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceNavigator/CollectionEditor/ArchivedCollectionsSection.qml
        qml/App/Workbench/WorkbenchShell/StatusBar/StatusBar.qml
        qml/App/Workbench/Workspace/Workspace.qml
        qml/App/Workbench/Workspace/ChatViewportPolicy.js
        qml/App/Workbench/Workspace/EditorTabs/EditorTabStrip.qml
        qml/App/Workbench/Workspace/FilePreview/FilePreview.qml
        qml/App/Workbench/Workspace/CodeEditor/CodeEditor.qml
        qml/App/Workbench/Workspace/ChatMessage/ChatMessage.qml
        qml/App/Workbench/Workspace/ChatMessage/AIEditProposalCard.qml
        qml/App/Workbench/Workspace/ChatMessage/RunActivityCard.qml
        qml/App/Workbench/Workspace/ChatMessage/RunActivityRow.qml
        qml/App/Workbench/Workspace/ChatMessage/CodeBlock.qml
        qml/App/Workbench/Workspace/ChatMessage/CodeFoldState.js
        qml/App/Workbench/Workspace/ChatMessage/ChatLaunchRegistry.js
        qml/App/Workbench/Workspace/ChatMessage/TimeDivider.qml
        qml/App/Workbench/Workspace/ChatMessage/CometGlyph.qml
        qml/App/Workbench/Workspace/ChatMessage/RichMessageCache.js
        qml/App/Workbench/Workspace/ChatMessage/RichMessageContent.qml
        qml/App/Workbench/Workspace/JumpToLatestButton/JumpToLatestButton.qml
        qml/App/Workbench/ChatDock/ChatDock.qml
        qml/App/Workbench/ChatDock/ChatAgentPicker/ChatAgentPicker.qml
        qml/App/Workbench/ChatDock/ChatComposer/ChatComposer.qml
        qml/App/Workbench/ChatDock/ChatEditor/ChatEditor.qml
        qml/App/Workbench/ChatDock/AgentEditor/AgentEditor.qml
        qml/App/Workbench/IdeHost/IdeHost.qml
        qml/App/Workbench/TerminalDock/TerminalDock.qml
        qml/App/Workbench/WorkbenchDock/WorkbenchDock.qml
        qml/App/Workbench/ArtifactDrawer/ArtifactDrawer.qml
        qml/App/Workbench/ArtifactDrawer/ContextInspector/ContextInspector.qml
    RESOURCES
        qml/App/Files/Renderers/Assets/checkerboard.svg
        qml/App/Icons/THIRD_PARTY_ICONS.md
        ${ARCHIVIST_ICON_ASSETS}
)

target_link_libraries(archivist-qt
    PRIVATE
        archivist-core
        Qt6::Network
        Qt6::Pdf
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::WebChannel
        Qt6::WebEngineQuick
)

set_target_properties(archivist-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME Archivist
    MACOSX_BUNDLE_GUI_IDENTIFIER com.archivist.desktop
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)

option(ARCHIVIST_BUILD_TESTS "Build native unit tests" OFF)

if(ARCHIVIST_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(chat-message-model-test
        tests/chat_message_model_test.cpp
        src/App/Domains/Chat/chat_message_model.cpp
        src/App/Domains/Chat/chat_message_model.h
    )
    target_include_directories(chat-message-model-test PRIVATE src)
    target_link_libraries(chat-message-model-test PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(chat-message-model-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME chat-message-model-test COMMAND chat-message-model-test)

    qt_add_executable(message-block-parser-test
        tests/message_block_parser_test.cpp
        src/App/Domains/Chat/message_block_parser.cpp
        src/App/Domains/Chat/message_block_parser.h
    )
    target_include_directories(message-block-parser-test PRIVATE src)
    target_compile_definitions(message-block-parser-test PRIVATE
        ARCHIVIST_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    target_link_libraries(message-block-parser-test PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(message-block-parser-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME message-block-parser-test COMMAND message-block-parser-test)

    qt_add_executable(chat-text-test
        tests/chat_text_test.cpp
        src/App/Domains/Chat/chat_syntax.cpp
        src/App/Domains/Chat/chat_syntax.h
        src/App/Domains/Chat/chat_text_item.cpp
        src/App/Domains/Chat/chat_text_item.h
        src/App/Domains/Chat/chat_text_layout.cpp
        src/App/Domains/Chat/chat_text_layout.h
    )
    target_include_directories(chat-text-test PRIVATE src)
    target_link_libraries(chat-text-test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    set_target_properties(chat-text-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME chat-text-test COMMAND chat-text-test)
    set_tests_properties(chat-text-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(core-database-test tests/core_database_test.cpp)
    target_link_libraries(core-database-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-database-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-database-test COMMAND core-database-test)

    qt_add_executable(core-collections-test tests/core_collections_test.cpp)
    target_link_libraries(core-collections-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-collections-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-collections-test COMMAND core-collections-test)

    qt_add_executable(core-agents-test tests/core_agents_test.cpp)
    target_link_libraries(core-agents-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-agents-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-agents-test COMMAND core-agents-test)

    qt_add_executable(core-libraries-test tests/core_libraries_test.cpp)
    target_link_libraries(core-libraries-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-libraries-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-libraries-test COMMAND core-libraries-test)

    qt_add_executable(core-library-files-test tests/core_library_files_test.cpp)
    target_link_libraries(core-library-files-test PRIVATE archivist-core Qt6::Test)
    target_compile_definitions(core-library-files-test PRIVATE
        ARCHIVIST_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    set_target_properties(core-library-files-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-library-files-test COMMAND core-library-files-test)
    qt_add_executable(core-library-file-operations-test tests/core_library_file_operations_test.cpp)
    target_link_libraries(core-library-file-operations-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-library-file-operations-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-library-file-operations-test COMMAND core-library-file-operations-test)
    qt_add_executable(core-chats-test tests/core_chats_test.cpp)
    target_link_libraries(core-chats-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-chats-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-chats-test COMMAND core-chats-test)

    qt_add_executable(core-ai-test tests/core_ai_test.cpp)
    target_link_libraries(core-ai-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-ai-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ai-test COMMAND core-ai-test)

    qt_add_executable(core-ai-runtime-test tests/core_ai_runtime_test.cpp)
    target_link_libraries(core-ai-runtime-test PRIVATE archivist-core Qt6::Network Qt6::Test)
    set_target_properties(core-ai-runtime-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ai-runtime-test COMMAND core-ai-runtime-test)

    qt_add_executable(core-ide-test tests/core_ide_test.cpp)
    target_link_libraries(core-ide-test PRIVATE archivist-core Qt6::Network Qt6::Test)
    set_target_properties(core-ide-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ide-test COMMAND core-ide-test)
endif()
