cmake_minimum_required(VERSION 3.25)

project(AeroMesh
    VERSION 0.1.0
    DESCRIPTION "Decentralized, censorship-resistant P2P messenger core"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AEROMESH_BUILD_TESTS "Build unit tests" ON)
option(AEROMESH_BUILD_CLIENT "Build the Qt Quick (QML) client" ON)

# Warnings as a baseline quality gate.
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(platform)

# Qt Quick client (desktop now, mobile-ready). Skipped automatically if Qt6 is
# not found, so the core library and tests still build without Qt installed.
if(AEROMESH_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if(AEROMESH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
    add_subdirectory(platform/tests)
endif()
