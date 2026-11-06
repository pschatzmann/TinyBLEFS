cmake_minimum_required(VERSION 3.16)
project(tinyblefs-tools CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Built from the project root (-DTINYBLEFS_BUILD_TOOLS=ON) the library target
# exists already; standalone we use the headers in ../../src
if(NOT TARGET TinyBLEFS::TinyBLEFS)
  add_library(tinyblefs_headers INTERFACE)
  add_library(TinyBLEFS::TinyBLEFS ALIAS tinyblefs_headers)
  target_include_directories(tinyblefs_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../../src)
endif()

# SimpleBLE (https://github.com/OpenBluetoothToolbox/SimpleBLE): either install
# it so that find_package works, or use the FetchContent block below.
find_package(simpleble CONFIG QUIET)
if(NOT simpleble_FOUND)
  include(FetchContent)
  FetchContent_Declare(simpleble
    GIT_REPOSITORY https://github.com/OpenBluetoothToolbox/SimpleBLE.git
    GIT_TAG v1.4.0
    SOURCE_SUBDIR simpleble)  # the CMake project is not in the repository root
  FetchContent_MakeAvailable(simpleble)
endif()

add_executable(tinyblefs main.cpp)
target_link_libraries(tinyblefs PRIVATE TinyBLEFS::TinyBLEFS simpleble::simpleble)

add_executable(tinyblefs-server server.cpp)
target_link_libraries(tinyblefs-server PRIVATE TinyBLEFS::TinyBLEFS simpleble::simpleble)

install(TARGETS tinyblefs tinyblefs-server RUNTIME DESTINATION bin)
