add_executable(aeromesh_tests test_packet.cpp)
target_link_libraries(aeromesh_tests PRIVATE aeromesh::core)

add_test(NAME core_tests COMMAND aeromesh_tests)
set_tests_properties(core_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_dht_tests test_dht.cpp)
target_link_libraries(aeromesh_dht_tests PRIVATE aeromesh::core)

add_test(NAME dht_tests COMMAND aeromesh_dht_tests)
set_tests_properties(dht_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_onion_tests test_onion.cpp)
target_link_libraries(aeromesh_onion_tests PRIVATE aeromesh::core)

add_test(NAME onion_tests COMMAND aeromesh_onion_tests)
set_tests_properties(onion_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_cover_tests test_cover.cpp)
target_link_libraries(aeromesh_cover_tests PRIVATE aeromesh::core)

add_test(NAME cover_tests COMMAND aeromesh_cover_tests)
set_tests_properties(cover_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_ratchet_tests test_ratchet.cpp)
target_link_libraries(aeromesh_ratchet_tests PRIVATE aeromesh::core)

add_test(NAME ratchet_tests COMMAND aeromesh_ratchet_tests)
set_tests_properties(ratchet_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_pq_tests test_pqhandshake.cpp)
target_link_libraries(aeromesh_pq_tests PRIVATE aeromesh::core)

add_test(NAME pq_tests COMMAND aeromesh_pq_tests)
set_tests_properties(pq_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_rs_tests test_reed_solomon.cpp)
target_link_libraries(aeromesh_rs_tests PRIVATE aeromesh::core)

add_test(NAME rs_tests COMMAND aeromesh_rs_tests)
set_tests_properties(rs_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_file_tests test_filechunk.cpp)
target_link_libraries(aeromesh_file_tests PRIVATE aeromesh::core)

add_test(NAME file_tests COMMAND aeromesh_file_tests)
set_tests_properties(file_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_ephemeral_tests test_ephemeral.cpp)
target_link_libraries(aeromesh_ephemeral_tests PRIVATE aeromesh::core)

add_test(NAME ephemeral_tests COMMAND aeromesh_ephemeral_tests)
set_tests_properties(ephemeral_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_transport_tests test_transport.cpp)
target_link_libraries(aeromesh_transport_tests PRIVATE aeromesh::core)

add_test(NAME transport_tests COMMAND aeromesh_transport_tests)
set_tests_properties(transport_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_session_tests test_session.cpp)
target_link_libraries(aeromesh_session_tests PRIVATE aeromesh::core)

add_test(NAME session_tests COMMAND aeromesh_session_tests)
set_tests_properties(session_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_stun_tests test_stun.cpp)
target_link_libraries(aeromesh_stun_tests PRIVATE aeromesh::core)

add_test(NAME stun_tests COMMAND aeromesh_stun_tests)
set_tests_properties(stun_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_holepunch_tests test_holepunch.cpp)
target_link_libraries(aeromesh_holepunch_tests PRIVATE aeromesh::core)

add_test(NAME holepunch_tests COMMAND aeromesh_holepunch_tests)
set_tests_properties(holepunch_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_relay_tests test_relay.cpp)
target_link_libraries(aeromesh_relay_tests PRIVATE aeromesh::core)

add_test(NAME relay_tests COMMAND aeromesh_relay_tests)
set_tests_properties(relay_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_link_tests test_link.cpp)
target_link_libraries(aeromesh_link_tests PRIVATE aeromesh::core)

add_test(NAME link_tests COMMAND aeromesh_link_tests)
set_tests_properties(link_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

add_executable(aeromesh_vault_tests test_account_vault.cpp)
target_link_libraries(aeromesh_vault_tests PRIVATE aeromesh::core)

add_test(NAME vault_tests COMMAND aeromesh_vault_tests)
set_tests_properties(vault_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
