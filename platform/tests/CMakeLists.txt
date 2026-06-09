add_executable(aeromesh_udp_tests test_udp_socket.cpp)
target_link_libraries(aeromesh_udp_tests PRIVATE aeromesh::platform)

add_test(NAME udp_tests COMMAND aeromesh_udp_tests)
set_tests_properties(udp_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
