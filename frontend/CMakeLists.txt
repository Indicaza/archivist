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
    src/Core/Domains/Composer/composer_state.cpp
    src/Core/Domains/Composer/composer_state.h
    src/Core/Domains/Chats/chat_drafts.cpp
    src/Core/Domains/Chats/chat_drafts.h
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
    src/Core/Domains/Themes/code_theme.cpp
    src/Core/Domains/Themes/code_theme.h
    src/Core/Domains/Themes/code_theme_library.cpp
    src/Core/Domains/Themes/code_theme_library.h
    src/Core/Domains/Worktrees/git_worktrees.cpp
    src/Core/Domains/Worktrees/git_worktrees.h
    src/Core/Domains/Worktrees/workspace_change_journal.cpp
    src/Core/Domains/Worktrees/workspace_change_journal.h
    src/Core/Domains/Worktrees/worktree_repository.cpp
    src/Core/Domains/Worktrees/worktree_repository.h
    src/Core/Domains/Workspace/workspace_types.h
    src/Core/Domains/Workspace/view_type_registry.cpp
    src/Core/Domains/Workspace/view_type_registry.h
    src/Core/Domains/Workspace/workspace_model.cpp
    src/Core/Domains/Workspace/workspace_model.h
    src/Core/Domains/Workspace/workspace_codec.cpp
    src/Core/Domains/Workspace/workspace_codec.h
    src/Core/Domains/Workspace/workspace_commands.cpp
    src/Core/Domains/Workspace/workspace_commands.h
    src/Core/Domains/Workspace/workspace_presets.cpp
    src/Core/Domains/Workspace/workspace_presets.h
    src/Core/Domains/Workspace/workspace_resources.cpp
    src/Core/Domains/Workspace/workspace_resources.h
    src/Core/Domains/Workspace/workspace_placement.cpp
    src/Core/Domains/Workspace/workspace_placement.h
    src/Core/Domains/Workspace/workspace_intents.cpp
    src/Core/Domains/Workspace/workspace_intents.h
    src/Core/Domains/Workspace/workspace_arrangements.cpp
    src/Core/Domains/Workspace/workspace_arrangements.h
    src/Core/Domains/Workspace/workspace_magnets.cpp
    src/Core/Domains/Workspace/workspace_magnets.h
    src/Core/Domains/Workspace/workspace_wells.cpp
    src/Core/Domains/Workspace/workspace_wells.h
    src/Core/Domains/Workspace/workspace_gateway.cpp
    src/Core/Domains/Workspace/workspace_gateway.h
    src/Core/Domains/Workspace/workspace_preferences.cpp
    src/Core/Domains/Workspace/workspace_preferences.h
    src/Core/Domains/Workspace/workspace_compose.cpp
    src/Core/Domains/Workspace/workspace_compose.h
    src/Core/Domains/Shell/shell_navigation.cpp
    src/Core/Domains/Shell/shell_navigation.h
    src/Core/Domains/Shell/recent_items.cpp
    src/Core/Domains/Shell/recent_items.h
    src/Core/Domains/Home/home_modules.cpp
    src/Core/Domains/Home/home_modules.h
    src/Core/Domains/Home/home_items.cpp
    src/Core/Domains/Home/home_items.h
    src/Core/Domains/Documents/document_buffers.cpp
    src/Core/Domains/Documents/document_buffers.h
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

# Archivist's own colors as a VS Code color theme, for export.
qt_add_resources(archivist-core "archivist-core-themes"
    PREFIX "/archivist/themes"
    BASE "src/Core/Domains/Themes"
    FILES src/Core/Domains/Themes/archivist-dark-color-theme.json
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
    src/App/Domains/Chat/chat_draft_store.cpp
    src/App/Domains/Chat/chat_draft_store.h
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
    src/App/Domains/Worktree/worktree_store.cpp
    src/App/Domains/Worktree/worktree_store.h
    src/App/Domains/Documents/document_store.cpp
    src/App/Domains/Documents/document_store.h
    src/App/Domains/Documents/unsaved_recovery.cpp
    src/App/Domains/Documents/unsaved_recovery.h
    src/App/Domains/Workspace/keyed_list_model.cpp
    src/App/Domains/Workspace/keyed_list_model.h
    src/App/Domains/Shell/shell_store.cpp
    src/App/Domains/Shell/shell_store.h
    src/App/Domains/Home/home_store.cpp
    src/App/Domains/Home/home_store.h
    src/App/Domains/Workspace/workspace_store.cpp
    src/App/Domains/Workspace/workspace_store.h
    src/App/Domains/Workspace/workspace_store_agent.cpp
    src/App/Domains/Workspace/workspace_store_composer.cpp
    src/App/Domains/Workspace/workspace_store_magnets.cpp
    src/App/Domains/Workspace/workspace_store_wells.cpp
    src/App/Domains/Workspace/workspace_agent_bridge.cpp
    src/App/Domains/Workspace/workspace_agent_bridge.h
    src/App/Services/runtime_trace_store.cpp
    src/App/Services/runtime_trace_store.h
    src/App/Services/workspace_state_store.cpp
    src/App/Services/workspace_state_store.h
    src/App/Services/interface_scale.cpp
    src/App/Services/interface_scale.h
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
        qml/App/Topbar/CodeThemePicker.qml
        qml/App/Workbench/WorkbenchShell/WorkbenchShell.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/ActivityRail.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/RailButton/RailButton.qml
        qml/App/Workbench/WorkbenchShell/SidePanelResizeHandle/SidePanelResizeHandle.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerDock.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorkspaceDragSession.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerItem/ExplorerItem.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorktreeBar/WorktreeBar.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorktreeBar/WorktreeRow.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorktreeBar/WorktreeField.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorktreeBar/WorktreeActionButton.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/WorktreeBar/AgentRunningBadge.qml
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
        qml/App/Workbench/WorkbenchShell/MotionTuner/MotionTuner.qml
        qml/App/Workbench/Home/HomeView.qml
        qml/App/Workbench/CompactChat/CompactChatComposer.qml
        qml/App/Workbench/Home/HomeComposer.qml
        qml/App/Workbench/Home/HomeSidebar.qml
        qml/App/Workbench/Home/HomeSidebarRow.qml
        qml/App/Workbench/Home/RecentActivityTracker.qml
        qml/App/Workbench/Home/HomeChip.qml
        qml/App/Workbench/Home/HomeSendButton.qml
        qml/App/Workbench/Home/HomeCard.qml
        qml/App/Workbench/Home/HomePanel.qml
        qml/App/Workbench/Home/HomeListRow.qml
        qml/App/Workbench/Home/HomeKeyedModel.qml
        qml/App/Workbench/Home/HomeModuleFrame.qml
        qml/App/Workbench/Home/HomeModuleGrid.qml
        qml/App/Workbench/Home/HomeCustomizeBar.qml
        qml/App/Workbench/Home/HomeFirstRun.qml
        qml/App/Workbench/Home/ResumeModule.qml
        qml/App/Workbench/Home/PinnedModule.qml
        qml/App/Workbench/Home/ActivityModule.qml
        qml/App/Workbench/Home/LayoutsModule.qml
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
        qml/App/Workbench/Dock/DockWorkspace.qml
        qml/App/Workbench/Dock/DockAnchor.qml
        qml/App/Workbench/Dock/DockTab.qml
        qml/App/Workbench/Dock/DockViewContent.qml
        qml/App/Workbench/Dock/DockEditorHost.qml
        qml/App/Workbench/Dock/DockSplitter.qml
        qml/App/Workbench/Dock/DockSurfaceArea.qml
        qml/App/Workbench/Dock/DockCarry.qml
        qml/App/Workbench/Dock/DockFloatingPanel.qml
        qml/App/Workbench/Dock/DockSidebarSlot.qml
        qml/App/Workbench/Dock/DockDragShield.qml
        qml/App/Workbench/Dock/DockFloatingWindow.qml
        qml/App/Workbench/Dock/DockDropOverlay.qml
        qml/App/Workbench/Dock/DockDialogButton.qml
        qml/App/Workbench/Dock/DockNoticeBar.qml
        qml/App/Workbench/Dock/DockLayoutCard.qml
        qml/App/Workbench/Dock/DockLayoutPicker.qml
        qml/App/Workbench/Dock/DockShelfPicker.qml
        qml/App/Workbench/Dock/DockLayoutThumb.qml
        qml/App/Workbench/Dock/DockMenu.qml
        qml/App/Workbench/Dock/DockMenuItem.qml
        qml/App/Workbench/Dock/DockMenuSeparator.qml
        qml/App/Workbench/Dock/DockPillButton.qml
        qml/App/Workbench/Dock/DockSplitGlyph.qml
        qml/App/Workbench/Dock/DockWell.qml
        qml/App/Workbench/Dock/DockWellIcon.qml
        qml/App/Workbench/Dock/DockWellTip.qml
        qml/App/Workbench/Dock/WellGlow.qml
        qml/App/Workbench/Dock/DockGenie.qml
        qml/App/Workbench/Dock/WellGeometry.js
        qml/App/Workbench/Dock/WellTip.js
        qml/App/Workbench/Dock/TabStrip.js
        qml/App/Workbench/TerminalDock/TerminalDock.qml
        qml/App/Workbench/WorkbenchDock/WorkbenchDock.qml
        qml/App/Workbench/ArtifactDrawer/ArtifactDrawer.qml
        qml/App/Workbench/ArtifactDrawer/ContextInspector/ContextInspector.qml
    RESOURCES
        qml/App/Workbench/Dock/shaders/genie.vert.qsb
        qml/App/Workbench/Dock/shaders/genie.frag.qsb
        qml/App/Workbench/Dock/shaders/genie_shadow.frag.qsb
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
    qt_add_executable(core-chat-drafts-test
        tests/core_chat_drafts_test.cpp
        src/App/Domains/Chat/chat_draft_store.cpp
        src/App/Domains/Chat/chat_draft_store.h
    )
    target_include_directories(core-chat-drafts-test PRIVATE src)
    target_link_libraries(core-chat-drafts-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-chat-drafts-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-chat-drafts-test COMMAND core-chat-drafts-test)
    qt_add_executable(core-composer-test tests/core_composer_test.cpp)
    target_link_libraries(core-composer-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-composer-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-composer-test COMMAND core-composer-test)

    qt_add_executable(core-ai-test tests/core_ai_test.cpp)
    target_link_libraries(core-ai-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-ai-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ai-test COMMAND core-ai-test)

    qt_add_executable(core-ai-runtime-test tests/core_ai_runtime_test.cpp)
    target_link_libraries(core-ai-runtime-test PRIVATE archivist-core Qt6::Network Qt6::Test)
    set_target_properties(core-ai-runtime-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ai-runtime-test COMMAND core-ai-runtime-test)

    qt_add_executable(core-code-themes-test tests/core_code_themes_test.cpp)
    target_link_libraries(core-code-themes-test PRIVATE archivist-core Qt6::Test)
    target_compile_definitions(core-code-themes-test PRIVATE
        ARCHIVIST_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    set_target_properties(core-code-themes-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-code-themes-test COMMAND core-code-themes-test)

    qt_add_executable(core-worktrees-test tests/core_worktrees_test.cpp)
    target_link_libraries(core-worktrees-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-worktrees-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-worktrees-test COMMAND core-worktrees-test)

    qt_add_executable(core-ide-test tests/core_ide_test.cpp)
    target_link_libraries(core-ide-test PRIVATE archivist-core Qt6::Network Qt6::Test)
    set_target_properties(core-ide-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-ide-test COMMAND core-ide-test)

    qt_add_executable(core-shell-test
        tests/core_shell_test.cpp
        src/App/Domains/Shell/shell_store.cpp
        src/App/Domains/Shell/shell_store.h
    )
    target_include_directories(core-shell-test PRIVATE src)
    target_link_libraries(core-shell-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-shell-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-shell-test COMMAND core-shell-test)

    qt_add_executable(core-home-test
        tests/core_home_test.cpp
        src/App/Domains/Home/home_store.cpp
        src/App/Domains/Home/home_store.h
    )
    target_include_directories(core-home-test PRIVATE src)
    target_link_libraries(core-home-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-home-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-home-test COMMAND core-home-test)

    qt_add_executable(core-workspace-test tests/core_workspace_test.cpp)
    target_link_libraries(core-workspace-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-workspace-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-workspace-test COMMAND core-workspace-test)

    qt_add_executable(core-magnets-test tests/core_magnets_test.cpp)
    target_link_libraries(core-magnets-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-magnets-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-magnets-test COMMAND core-magnets-test)

    qt_add_executable(core-wells-test tests/core_wells_test.cpp)
    target_link_libraries(core-wells-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-wells-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-wells-test COMMAND core-wells-test)

    qt_add_executable(core-workspace-agent-test tests/core_workspace_agent_test.cpp)
    target_link_libraries(core-workspace-agent-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-workspace-agent-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-workspace-agent-test COMMAND core-workspace-agent-test)

    qt_add_executable(core-document-buffers-test tests/core_document_buffers_test.cpp)
    target_link_libraries(core-document-buffers-test PRIVATE archivist-core Qt6::Test)
    set_target_properties(core-document-buffers-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME core-document-buffers-test COMMAND core-document-buffers-test)

    qt_add_executable(workspace-store-test
        tests/workspace_store_test.cpp
        src/App/Domains/Documents/document_store.cpp
        src/App/Domains/Documents/document_store.h
        src/App/Domains/Documents/unsaved_recovery.cpp
        src/App/Domains/Documents/unsaved_recovery.h
        src/App/Domains/Workspace/keyed_list_model.cpp
        src/App/Domains/Workspace/keyed_list_model.h
        src/App/Domains/Workspace/workspace_store.cpp
        src/App/Domains/Workspace/workspace_store.h
        src/App/Domains/Workspace/workspace_store_agent.cpp
        src/App/Domains/Workspace/workspace_store_composer.cpp
        src/App/Domains/Workspace/workspace_store_magnets.cpp
        src/App/Domains/Workspace/workspace_store_wells.cpp
        src/App/Domains/Workspace/workspace_agent_bridge.cpp
        src/App/Domains/Workspace/workspace_agent_bridge.h
    )
    target_include_directories(workspace-store-test PRIVATE src)
    target_link_libraries(workspace-store-test PRIVATE archivist-core Qt6::Gui Qt6::Test)
    set_target_properties(workspace-store-test PROPERTIES MACOSX_BUNDLE FALSE)
    add_test(NAME workspace-store-test COMMAND workspace-store-test)
endif()
