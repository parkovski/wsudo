set(SOURCES
  connection.cpp
  server.cpp
  session.cpp
  tokenmanager.cpp
  tokenmanager-logon.cpp
)
add_library(wsudo-server STATIC ${SOURCES})
target_link_libraries(wsudo-server PUBLIC wsudo-common)
