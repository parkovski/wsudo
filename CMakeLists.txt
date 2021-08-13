cmake_minimum_required(VERSION 3.20.0 FATAL_ERROR)

project(wsudo)
set(wsudo_VERSION_MAJOR 0)
set(wsudo_VERSION_MINOR 1)
set(wsudo_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE MATCHES "Deb")
  set(WSUDO_BUILD_TESTS ON)
endif()
option(WSUDO_BUILD_TESTS "Build tests" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(WIL REQUIRED)

find_path(wscoro_INCLUDE_DIR wscoro/wscoro.h HINTS ../wscoro/include REQUIRED)

add_definitions(-DUNICODE -D_UNICODE)
include_directories(${PROJECT_SOURCE_DIR}/include ${wscoro_INCLUDE_DIR})
link_libraries(spdlog::spdlog fmt::fmt-header-only WIL::WIL)

add_subdirectory(lib)
add_subdirectory(bin)

if(WSUDO_BUILD_TESTS)
  add_subdirectory(test)
endif()
