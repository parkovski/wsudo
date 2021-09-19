set(SOURCES
  server.cpp
  session.cpp
)
add_library(wsudo-server STATIC ${SOURCES})
target_link_libraries(wsudo-server PUBLIC wsudo-common)
