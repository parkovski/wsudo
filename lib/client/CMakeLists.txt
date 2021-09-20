set(SOURCES
  client.cpp
)
add_library(wsudo-client STATIC ${SOURCES})
target_link_libraries(wsudo-client PUBLIC wsudo-common)
