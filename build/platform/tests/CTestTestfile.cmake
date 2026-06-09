# CMake generated Testfile for 
# Source directory: C:/mcp-test/aeromesh/platform/tests
# Build directory: C:/mcp-test/aeromesh/build/platform/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[udp_tests]=] "C:/mcp-test/aeromesh/build/platform/tests/Debug/aeromesh_udp_tests.exe")
  set_tests_properties([=[udp_tests]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FAIL" _BACKTRACE_TRIPLES "C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;4;add_test;C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[udp_tests]=] "C:/mcp-test/aeromesh/build/platform/tests/Release/aeromesh_udp_tests.exe")
  set_tests_properties([=[udp_tests]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FAIL" _BACKTRACE_TRIPLES "C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;4;add_test;C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[udp_tests]=] "C:/mcp-test/aeromesh/build/platform/tests/MinSizeRel/aeromesh_udp_tests.exe")
  set_tests_properties([=[udp_tests]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FAIL" _BACKTRACE_TRIPLES "C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;4;add_test;C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[udp_tests]=] "C:/mcp-test/aeromesh/build/platform/tests/RelWithDebInfo/aeromesh_udp_tests.exe")
  set_tests_properties([=[udp_tests]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FAIL" _BACKTRACE_TRIPLES "C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;4;add_test;C:/mcp-test/aeromesh/platform/tests/CMakeLists.txt;0;")
else()
  add_test([=[udp_tests]=] NOT_AVAILABLE)
endif()
