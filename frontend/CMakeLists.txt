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

qt_add_executable(archivist-qt
    src/main.cpp
    src/App/Domains/Agent/agent_store.cpp
    src/App/Domains/Agent/agent_store.h
    src/App/Domains/Chat/chat_store.cpp
    src/App/Domains/Chat/chat_store.h
    src/App/Domains/Collection/collection_store.cpp
    src/App/Domains/Collection/collection_store.h
    src/App/Domains/Files/markdown_document_bridge.cpp
    src/App/Domains/Files/markdown_document_bridge.h
    src/App/Domains/Files/document_preview_service.cpp
    src/App/Domains/Files/document_preview_service.h
    src/App/Domains/Library/library_store.cpp
    src/App/Domains/Library/library_store.h
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
        qml/App/Workbench/Workspace/EditorTabs/EditorTabStrip.qml
        qml/App/Workbench/Workspace/FilePreview/FilePreview.qml
        qml/App/Workbench/Workspace/CodeEditor/CodeEditor.qml
        qml/App/Workbench/Workspace/ChatMessage/ChatMessage.qml
        qml/App/Workbench/Workspace/ChatMessage/RunActivityCard.qml
        qml/App/Workbench/Workspace/ChatMessage/RunActivityRow.qml
        qml/App/Workbench/Workspace/ChatMessage/CodeBlock.qml
        qml/App/Workbench/Workspace/ChatMessage/CometGlyph.qml
        qml/App/Workbench/Workspace/ChatMessage/RichMessageContent.qml
        qml/App/Workbench/Workspace/JumpToLatestButton/JumpToLatestButton.qml
        qml/App/Workbench/ChatDock/ChatDock.qml
        qml/App/Workbench/ChatDock/ChatAgentPicker/ChatAgentPicker.qml
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
