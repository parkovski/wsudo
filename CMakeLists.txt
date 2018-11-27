cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(stdo)
set(stdo_VERSION_MAJOR 0)
set(stdo_VERSION_MINOR 1)
set(stdo_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STDO_BUILD_TESTS "Build tests" ON)

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
  overlapped.cpp
  server.cpp
)
list(TRANSFORM SERVICE_SRC PREPEND "service/")

add_library(stdo_common STATIC ${COMMON_SRC})
add_executable(stdo ${CLIENT_SRC})
add_executable(TokenServer ${SERVICE_SRC})

include_directories(${PROJECT_SOURCE_DIR}/include)

set(STDO_LIBRARIES spdlog::spdlog fmt::fmt-header-only)

target_link_libraries(stdo_common ${STDO_LIBRARIES})
target_link_libraries(stdo stdo_common)
target_link_libraries(TokenServer stdo_common)

if(STDO_BUILD_TESTS)
  add_subdirectory(test)
endif()
