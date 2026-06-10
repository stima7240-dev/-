# AeroMesh core library: framing, crypto identity, and (incrementally) DHT,
# ratchet, sharding, and storage.

add_library(aeromesh_core STATIC
    src/packet.cpp
    src/identity.cpp
    src/account_vault.cpp
    src/node_id.cpp
    src/routing_table.cpp
    src/kademlia.cpp
    src/onion.cpp
    src/cover.cpp
    src/ratchet.cpp
    src/pqhandshake.cpp
    src/reed_solomon.cpp
    src/filechunk.cpp
    src/ephemeral.cpp
    src/transport.cpp
    src/session.cpp
    src/stun.cpp
    src/holepunch.cpp
    src/relay.cpp
    src/link.cpp
)

target_include_directories(aeromesh_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- libsodium discovery ------------------------------------------------------
# Tries, in order: vcpkg config package -> pkg-config -> manual find ->
# FetchContent (downloads & builds libsodium, no manual install needed).
option(AEROMESH_FETCH_SODIUM
    "Download and build libsodium via FetchContent when it is not found" ON)

set(_aeromesh_sodium_target "")

# 1) vcpkg: find_package(unofficial-sodium) -> unofficial-sodium::sodium
find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
    set(_aeromesh_sodium_target unofficial-sodium::sodium)
    message(STATUS "AeroMesh: using libsodium from vcpkg (unofficial-sodium).")
endif()

# 2) pkg-config (Linux/macOS, MSYS2)
if(NOT _aeromesh_sodium_target)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM IMPORTED_TARGET QUIET libsodium)
        if(TARGET PkgConfig::SODIUM)
            set(_aeromesh_sodium_target PkgConfig::SODIUM)
            message(STATUS "AeroMesh: using libsodium from pkg-config.")
        endif()
    endif()
endif()

# 3) manual find (headers + lib already on the system)
if(NOT _aeromesh_sodium_target)
    find_path(SODIUM_INCLUDE_DIR sodium.h)
    find_library(SODIUM_LIBRARY NAMES sodium libsodium)
    if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
        add_library(aeromesh_sodium_manual INTERFACE)
        target_include_directories(aeromesh_sodium_manual
            INTERFACE ${SODIUM_INCLUDE_DIR})
        target_link_libraries(aeromesh_sodium_manual
            INTERFACE ${SODIUM_LIBRARY})
        set(_aeromesh_sodium_target aeromesh_sodium_manual)
        message(STATUS "AeroMesh: using libsodium found at ${SODIUM_LIBRARY}.")
    endif()
endif()

# 4) FetchContent fallback -- needs Git + internet at configure time.
if(NOT _aeromesh_sodium_target AND AEROMESH_FETCH_SODIUM)
    message(STATUS
        "AeroMesh: libsodium not found locally; fetching and building it via "
        "FetchContent (robinlinden/libsodium-cmake).")
    include(FetchContent)
    FetchContent_Declare(Sodium
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        # Pinned commit (HEAD as of 2026-02-28). Bump deliberately.
        GIT_TAG cfebfd3da486d5a86c644c8b47067e5411c7599c
    )
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Sodium)
    set(_aeromesh_sodium_target sodium)
endif()

if(NOT _aeromesh_sodium_target)
    message(FATAL_ERROR
        "libsodium not found and AEROMESH_FETCH_SODIUM is OFF. Either turn it "
        "on, install libsodium (vcpkg: 'vcpkg install libsodium', apt: "
        "libsodium-dev, brew: libsodium), or set SODIUM_INCLUDE_DIR / "
        "SODIUM_LIBRARY.")
endif()

target_link_libraries(aeromesh_core PUBLIC ${_aeromesh_sodium_target})

# --- liboqs discovery (post-quantum KEM: ML-KEM-768) -------------------------
# Powers the hybrid handshake. Tries find_package (vcpkg/system) first, then
# FetchContent. If unavailable, the build still succeeds and pqhandshake.cpp
# falls back to classical X25519 only (see AEROMESH_HAVE_PQ in that file).
option(AEROMESH_ENABLE_PQ
    "Enable the post-quantum hybrid handshake (ML-KEM-768 via liboqs)" ON)

set(_aeromesh_oqs_target "")
if(AEROMESH_ENABLE_PQ)
    # 1) vcpkg / system: find_package(liboqs) -> OQS::oqs
    find_package(liboqs CONFIG QUIET)
    if(TARGET OQS::oqs)
        set(_aeromesh_oqs_target OQS::oqs)
        message(STATUS "AeroMesh: using liboqs from find_package (OQS::oqs).")
    endif()

    # 2) FetchContent fallback -- downloads & builds liboqs (large one-time build).
    if(NOT _aeromesh_oqs_target)
        message(STATUS
            "AeroMesh: liboqs not found locally; fetching and building it via "
            "FetchContent (open-quantum-safe/liboqs). This is a large one-time "
            "build and may take several minutes.")
        include(FetchContent)
        set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "" FORCE)
        set(OQS_USE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(OQS_DIST_BUILD ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(liboqs
            GIT_REPOSITORY https://github.com/open-quantum-safe/liboqs.git
            # Pinned release tag. Bump deliberately.
            GIT_TAG 0.10.1
        )
        FetchContent_MakeAvailable(liboqs)
        if(TARGET oqs)
            set(_aeromesh_oqs_target oqs)
            # The build-tree 'oqs' target does not reliably export the location
            # of the generated public headers (oqs/oqs.h, oqs/oqsconfig.h),
            # which land in <binary>/include. Add it explicitly so consumers of
            # aeromesh_core can #include <oqs/oqs.h>.
            target_include_directories(aeromesh_core PUBLIC
                ${liboqs_BINARY_DIR}/include)
        endif()
    endif()
endif()

if(_aeromesh_oqs_target)
    target_link_libraries(aeromesh_core PUBLIC ${_aeromesh_oqs_target})
    target_compile_definitions(aeromesh_core PUBLIC AEROMESH_HAVE_PQ=1)
    message(STATUS "AeroMesh: post-quantum hybrid ENABLED (X25519 + ML-KEM-768).")
else()
    message(STATUS
        "AeroMesh: post-quantum hybrid DISABLED; using classical X25519 only.")
endif()

add_library(aeromesh::core ALIAS aeromesh_core)
