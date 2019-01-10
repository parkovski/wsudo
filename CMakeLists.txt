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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

set(COMMON_SRC
  common.cpp
  events.cpp
  overlapped.cpp
  winsupport.cpp
)
list(TRANSFORM COMMON_SRC PREPEND "lib/common/")

set(CLIENT_SRC
  main.cpp
)
list(TRANSFORM CLIENT_SRC PREPEND "lib/client/")

set(SERVER_SRC
  clientconnection.cpp
  main.cpp
  namedpipehandlefactory.cpp
  server.cpp
  session.cpp
)
list(TRANSFORM SERVER_SRC PREPEND "lib/server/")

include_directories(${PROJECT_SOURCE_DIR}/include)
link_libraries(spdlog::spdlog fmt::fmt-header-only)

add_library(wsudo_common STATIC ${COMMON_SRC})
add_library(wsudo_client STATIC ${CLIENT_SRC})
add_library(wsudo_server STATIC ${SERVER_SRC})

add_executable(wsudo lib/client/main.cpp)
add_executable(TokenServer lib/server/main.cpp)

target_link_libraries(wsudo wsudo_client wsudo_common)
target_link_libraries(TokenServer wsudo_server wsudo_common)

if(WSUDO_BUILD_TESTS)
  add_subdirectory(test)
endif()
