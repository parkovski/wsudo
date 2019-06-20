#include "wsudo/server.h"
#include "wsudo/client.h"

#include <catch.hpp>

using namespace wsudo;

const wchar_t *const TestPipeName = L"\\\\.\\pipe\\wsudo_test_pipe";

TEST_CASE("Named pipe handle factory works", "[pipe]") {
  server::NamedPipeHandleFactory factory(TestPipeName);
  REQUIRE(factory.good());
  auto firstHandle = factory();
  REQUIRE(firstHandle != nullptr);
  auto secondHandle = factory();
  REQUIRE(secondHandle != nullptr);
}

#if 0
TEST_CASE("Named pipe client connections work", "[pipe]") {
  server::NamedPipeHandleFactory factory(TestPipeName);
  REQUIRE(factory.good());

  auto firstHandle = factory();
  auto secondHandle = factory();

  ClientConnection firstClient(TestPipeName);
  REQUIRE(firstClient.good());
  ClientConnection secondClient(TestPipeName);
  REQUIRE(secondClient.good());
}
#endif
