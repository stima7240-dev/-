# AeroMesh desktop/mobile client (Qt Quick / QML).
#
# The UI is intentionally cross-platform: the same QML runs on desktop today
# and on Android/iOS later. The client links the OS-independent aeromesh::core
# so it is ready to be wired to the live network in a later milestone.
#
# Qt is discovered softly: if it is not available, the client is skipped with a
# warning rather than failing the whole build. That keeps the core library and
# its tests buildable on machines without Qt installed.

find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Qml Quick QuickControls2)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 (>= 6.5) with Quick/QuickControls2 was not found; skipping the "
        "AeroMesh client. Pass -DCMAKE_PREFIX_PATH=<your Qt path> (e.g. "
        "C:/Qt/6.7.2/msvc2022_64) to enable it, or -DAEROMESH_BUILD_CLIENT=OFF "
        "to silence this.")
    return()
endif()

qt_standard_project_setup(REQUIRES 6.5)

# Keep generated QML/moc artifacts from colliding with anything else.
set(CMAKE_AUTOMOC ON)

qt_add_executable(aeromesh_client
    main.cpp
    Backend.h
)

qt_add_qml_module(aeromesh_client
    URI AeroMesh
    VERSION 1.0
    QML_FILES
        Main.qml
        ChatListItem.qml
        MessageBubble.qml
)

target_link_libraries(aeromesh_client
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        aeromesh::core
)

# On Windows, make this a GUI app (no console window) for Release builds.
set_target_properties(aeromesh_client PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
