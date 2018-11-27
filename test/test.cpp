#define CATCH_CONFIG_MAIN
#include <catch.hpp>

static int add_one(int i) {
  return i + 1;
}

TEST_CASE("Sample", "[sample]") {
  REQUIRE(add_one(1) == 2);
}
