add_subdirectory(corio)

set(COMMON_SRC
  common.cpp
  events.cpp
  overlapped.cpp
  winsupport.cpp
)
list(TRANSFORM COMMON_SRC PREPEND "common/")
add_library(wsudo-common STATIC ${COMMON_SRC})

set(SERVER_SRC
  clientconnection.cpp
  namedpipehandlefactory.cpp
  server.cpp
  session.cpp
)
list(TRANSFORM SERVER_SRC PREPEND "server/")
add_library(wsudo-server STATIC ${SERVER_SRC})
