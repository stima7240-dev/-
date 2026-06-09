# Platform module: OS-dependent networking (Winsock / BSD sockets).
# Kept separate from aeromesh_core so the core stays OS-independent.

add_library(aeromesh_platform src/udp_socket.cpp)
add_library(aeromesh::platform ALIAS aeromesh_platform)

target_include_directories(aeromesh_platform PUBLIC include)
target_link_libraries(aeromesh_platform PUBLIC aeromesh::core)

if(WIN32)
    target_link_libraries(aeromesh_platform PUBLIC ws2_32)
endif()
