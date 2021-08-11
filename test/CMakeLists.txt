find_package(Catch2 3.0 REQUIRED)

set(SOURCES test.cpp events.cpp pipe.cpp user.cpp corio.cpp)

add_executable(test ${SOURCES})
target_link_libraries(test Catch2::Catch2 wsudo_common wsudo_server wsudo_client)

include(CTest)
include(Catch)
catch_discover_tests(test)
