cmake_minimum_required(VERSION 3.24)

project(ArchivistQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Network Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0004 NEW)

qt_add_executable(archivist-qt
    src/main.cpp
    src/App/Domains/Chat/chat_store.cpp
    src/App/Domains/Chat/chat_store.h
    src/App/Domains/Library/library_store.cpp
    src/App/Domains/Library/library_store.h
)

qt_add_qml_module(archivist-qt
    URI Archivist
    VERSION 1.0
    QML_FILES
        qml/App.qml
        qml/App/Theme/Theme.qml
        qml/App/Topbar/Topbar.qml
        qml/App/Workbench/WorkbenchShell/WorkbenchShell.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/ActivityRail.qml
        qml/App/Workbench/WorkbenchShell/ActivityRail/RailButton/RailButton.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerDock.qml
        qml/App/Workbench/WorkbenchShell/ExplorerDock/ExplorerItem/ExplorerItem.qml
        qml/App/Workbench/WorkbenchShell/StatusBar/StatusBar.qml
        qml/App/Workbench/Workspace/Workspace.qml
        qml/App/Workbench/Workspace/ChatMessage/ChatMessage.qml
        qml/App/Workbench/Workspace/JumpToLatestButton/JumpToLatestButton.qml
        qml/App/Workbench/ChatDock/ChatDock.qml
        qml/App/Workbench/ArtifactDrawer/ArtifactDrawer.qml
)

target_link_libraries(archivist-qt
    PRIVATE
        Qt6::Network
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(archivist-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME Archivist
    MACOSX_BUNDLE_GUI_IDENTIFIER com.archivist.desktop
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)
