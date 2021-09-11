find_package(Catch2 3.0 REQUIRED)

set(SOURCES
  test.cpp
  user.cpp
  corio.cpp
)

add_executable(test ${SOURCES})
target_link_libraries(test
  Catch2::Catch2
  wsudo-common
  wsudo-server
)

include(CTest)
include(Catch)
catch_discover_tests(test)
