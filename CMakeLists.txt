cmake_minimum_required(VERSION 3.16)

# The version is taken from library.properties (single source of truth)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/library.properties" _tinyblefs_version_line
     REGEX "^version=")
string(REGEX REPLACE "^version=" "" _tinyblefs_version "${_tinyblefs_version_line}")

project(TinyBLEFS VERSION ${_tinyblefs_version} LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_tinyblefs_top_level ON)
else()
  set(_tinyblefs_top_level OFF)
endif()

option(TINYBLEFS_BUILD_TESTS "Build the host tests (loopback transport)" ${_tinyblefs_top_level})
option(TINYBLEFS_BUILD_TOOLS "Build the Linux tools tinyblefs and tinyblefs-server (needs SimpleBLE)" ${_tinyblefs_top_level})
set(TINYBLEFS_TINYFATFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TinyFATFS" CACHE PATH
    "TinyFATFS library (https://github.com/pschatzmann/TinyFATFS): enables the TinyFatBackend test")
option(TINYBLEFS_INSTALL "Install the headers and the CMake package" ${_tinyblefs_top_level})

# ---------------------------------------------------------------------------
# The library: header-only, so an INTERFACE target
#   target_link_libraries(myapp PRIVATE TinyBLEFS::TinyBLEFS)
# Adapters (TinyBLEFS/adapters/*.h) need their BLE library, e.g. SimpleBLE.h needs
# simpleble::simpleble; link it yourself.
# ---------------------------------------------------------------------------
add_library(tinyblefs_lib INTERFACE)
add_library(TinyBLEFS::TinyBLEFS ALIAS tinyblefs_lib)
target_include_directories(tinyblefs_lib INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include/TinyBLEFS>)
set_target_properties(tinyblefs_lib PROPERTIES EXPORT_NAME TinyBLEFS)
target_compile_features(tinyblefs_lib INTERFACE cxx_std_17)

find_package(Threads)
if(Threads_FOUND)
  target_link_libraries(tinyblefs_lib INTERFACE Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(TINYBLEFS_BUILD_TESTS)
  enable_testing()

  add_executable(test_loopback tests/test_loopback.cpp)
  target_link_libraries(test_loopback PRIVATE TinyBLEFS::TinyBLEFS)

  add_executable(test_cli tests/test_cli.cpp)
  target_link_libraries(test_cli PRIVATE TinyBLEFS::TinyBLEFS)
  target_include_directories(test_cli PRIVATE tools/tinyblefs-cli)  # Commands.h

  add_executable(test_arduinofs tests/test_arduinofs.cpp)
  target_link_libraries(test_arduinofs PRIVATE TinyBLEFS::TinyBLEFS)

  # backend for TinyFATFS (needs that library; it provides main() on the desktop)
  if(EXISTS "${TINYBLEFS_TINYFATFS_DIR}/src/fatfs.h")
    if(EXISTS "${TINYBLEFS_TINYFATFS_DIR}/src/fatfs.h")
      set(_tinyfatfs_src "${TINYBLEFS_TINYFATFS_DIR}/src")
    endif()
    add_executable(test_tinyfat tests/test_tinyfat.cpp)
    target_include_directories(test_tinyfat SYSTEM PRIVATE "${_tinyfatfs_src}")
    target_link_libraries(test_tinyfat PRIVATE TinyBLEFS::TinyBLEFS)
    add_test(NAME test_tinyfat COMMAND test_tinyfat)
  else()
    message(STATUS "TinyFATFS not found (TINYBLEFS_TINYFATFS_DIR): test_tinyfat is not built")
  endif()

  foreach(_t test_loopback test_cli test_arduinofs)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${_t} PRIVATE -Wall -Wextra)
    endif()
    # the tests need an empty scratch directory: cmake/RunTest.cmake creates it
    add_test(NAME ${_t}
      COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:${_t}>
        -DDIR=${CMAKE_CURRENT_BINARY_DIR}/${_t}-scratch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTest.cmake)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Linux tools (client and server)
# ---------------------------------------------------------------------------
if(TINYBLEFS_BUILD_TOOLS)
  add_subdirectory(tools/tinyblefs-cli)
endif()

# ---------------------------------------------------------------------------
# Install: headers + CMake package (find_package(TinyBLEFS) -> TinyBLEFS::TinyBLEFS)
# ---------------------------------------------------------------------------
if(TINYBLEFS_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(DIRECTORY src/ DESTINATION include/TinyBLEFS FILES_MATCHING PATTERN "*.h")
  install(TARGETS tinyblefs_lib EXPORT TinyBLEFSTargets)
  install(EXPORT TinyBLEFSTargets NAMESPACE TinyBLEFS:: DESTINATION lib/cmake/TinyBLEFS)

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyBLEFSConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TinyBLEFSConfig.cmake
    INSTALL_DESTINATION lib/cmake/TinyBLEFS)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TinyBLEFSConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TinyBLEFSConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TinyBLEFSConfigVersion.cmake
    DESTINATION lib/cmake/TinyBLEFS)
endif()
