#include "wsudo/wsudo.h"

#include <catch2/catch_all.hpp>

TEST_CASE("UTF8 to UTF16", "[unicode]") {
  std::string utf8 = "Привет, мир";
  std::wstring utf16 = wsudo::to_utf16(utf8);
  REQUIRE(utf16 == L"Привет, мир");
}

TEST_CASE("UTF16 to UTF8", "[unicode]") {
  std::wstring utf16 = L"Привет, мир";
  std::string utf8 = wsudo::to_utf8(utf16);
  REQUIRE(utf8 == "Привет, мир");
}
