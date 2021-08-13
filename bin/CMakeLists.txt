add_executable(wsudo wsudo.cpp)
target_link_libraries(wsudo wsudo-common)

add_executable(TokenServer TokenServer.cpp)
target_link_libraries(TokenServer wsudo-server wsudo-common)
