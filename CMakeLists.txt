cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(stdo)
set(stdo_VERSION_MAJOR 0)
set(stdo_VERSION_MINOR 1)
set(stdo_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}/bin/minsizerel)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/bin/optdebug)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(spdlog CONFIG REQUIRED)
find_package(fmt REQUIRED)
find_package(cereal REQUIRED)
find_package(Clara REQUIRED)
find_package(sodium REQUIRED)
find_package(catch2)

set(COMMON_SRC
  common/logging.cpp
  common/winsupport.cpp
)

set(CLIENT_SRC
  client/main.cpp
)

set(SERVICE_SRC
  service/main.cpp
  service/server.cpp
)

include_directories(${PROJECT_SOURCE_DIR}/include)
set(STDO_LIBRARIES
  spdlog::spdlog fmt::fmt cereal Clara sodium
)

add_library(stdo_common STATIC ${COMMON_SRC})
add_executable(stdo ${CLIENT_SRC})
add_executable(TokenServer ${SERVICE_SRC})

target_link_libraries(stdo_common ${STDO_LIBRARIES})
target_link_libraries(stdo stdo_common)
target_link_libraries(TokenServer stdo_common)
