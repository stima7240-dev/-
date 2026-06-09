# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/mcp-test/aeromesh/build/_deps/sodium-src")
  file(MAKE_DIRECTORY "C:/mcp-test/aeromesh/build/_deps/sodium-src")
endif()
file(MAKE_DIRECTORY
  "C:/mcp-test/aeromesh/build/_deps/sodium-build"
  "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix"
  "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/tmp"
  "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/src/sodium-populate-stamp"
  "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/src"
  "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/src/sodium-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/src/sodium-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/mcp-test/aeromesh/build/_deps/sodium-subbuild/sodium-populate-prefix/src/sodium-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
