find_package(Catch2 CONFIG REQUIRED)

set(SOURCES test.cpp)

add_executable(test ${SOURCES})
target_link_libraries(test Catch2::Catch2)

include(CTest)
include(Catch)
catch_discover_tests(test)
