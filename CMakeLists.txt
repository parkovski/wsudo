cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(stdo)
set(stdo_VERSION_MAJOR 0)
set(stdo_VERSION_MINOR 1)
set(stdo_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(-diagnostics:caret)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

find_package(spdlog CONFIG REQUIRED)
find_package(fmt)
find_package(catch2)

set(COMMON_SRC
  common/common.cpp
  common/winsupport.cpp
)

set(CLIENT_SRC
  client/main.cpp
)

set(SERVICE_SRC
  service/main.cpp
  service/server.cpp
)

add_library(stdo_common STATIC ${COMMON_SRC})
add_executable(stdo ${CLIENT_SRC})
add_executable(TokenServer ${SERVICE_SRC})

include_directories(${PROJECT_SOURCE_DIR}/include)

set(STDO_LIBRARIES spdlog::spdlog)
if(fmt_FOUND)
  list(APPEND STDO_LIBRARIES fmt::fmt-header-only)
endif()

target_link_libraries(stdo_common ${STDO_LIBRARIES})
target_link_libraries(stdo stdo_common)
target_link_libraries(TokenServer stdo_common)
