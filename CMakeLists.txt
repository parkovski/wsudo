cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(wsudo)
set(wsudo_VERSION_MAJOR 0)
set(wsudo_VERSION_MINOR 1)
set(wsudo_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WSUDO_BUILD_TESTS "Build tests" ON)

if(MSVC)
  add_compile_options(-diagnostics:caret)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

set(COMMON_SRC
  common.cpp
  events.cpp
  overlapped.cpp
  winsupport.cpp
)
list(TRANSFORM COMMON_SRC PREPEND "common/")

set(CLIENT_SRC
  main.cpp
)
list(TRANSFORM CLIENT_SRC PREPEND "client/")

set(SERVICE_SRC
  clientconnection.cpp
  main.cpp
  server.cpp
)
list(TRANSFORM SERVICE_SRC PREPEND "service/")

add_library(wsudo_common STATIC ${COMMON_SRC})
add_executable(wsudo ${CLIENT_SRC})
add_executable(TokenServer ${SERVICE_SRC})

include_directories(${PROJECT_SOURCE_DIR}/include)

set(WSUDO_LIBRARIES spdlog::spdlog fmt::fmt-header-only)

target_link_libraries(wsudo_common ${WSUDO_LIBRARIES})
target_link_libraries(wsudo wsudo_common)
target_link_libraries(TokenServer wsudo_common)

if(WSUDO_BUILD_TESTS)
  add_subdirectory(test)
endif()
