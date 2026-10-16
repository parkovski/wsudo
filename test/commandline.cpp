#include "wsudo/wsudo.h"
#include "wsudo/client.h"

#include <catch2/catch_all.hpp>

#define esc(arg) wsudo::escapeCommandLineArg(arg)

TEST_CASE("Command line parsing", "[commandline]") {
  using namespace std::string_view_literals;
  REQUIRE(esc(L"abc.exe") == L"abc.exe"sv);
  REQUIRE(esc(L"my file") == L"\"my file\""sv);
  REQUIRE(esc(L"C:\\foo.exe") == L"C:\\foo.exe"sv);
  REQUIRE(esc(L"\\\"") == L"\"\\\\\\\"\""sv);       // \" -> "\\\""
  REQUIRE(esc(L"\\\\") == L"\\\\"sv);               // \\ -> \\    (.)
  REQUIRE(esc(L"\\\\\"") == L"\"\\\\\\\\\\\"\""sv); // \\" -> "\\\\\""
  REQUIRE(esc(L"test \"test\\ test") == L"\"test \\\"test\\ test\""sv);

  const wchar_t *const argv[] = {
    L"C:\\Windows\\System32\\cmd.exe",
    L"/c",
    L"foo bar",
    L"\"\\test\"",
    L"\\\"foo bar\"\\",
  };
  const int argc = WSUDO_ARRAYSIZE(argv);
  auto options = wsudo::Client::Options{};
  options.useShim = false;
  options.parse(argc, argv);
  auto &cl = options.commandLine;
  REQUIRE(cl == L"C:\\Windows\\System32\\cmd.exe /c \"foo bar\" \"\\\"\\\\test\\\"\" \"\\\\\\\"foo bar\\\"\\\\\""sv);
}

static void testOneFileName(std::wstring_view filename) {
  std::wstring mutableFilename{filename};
  REQUIRE(wsudo::resolveProgramPath(mutableFilename));
}

TEST_CASE("File name resolution", "[commandline]") {
  using namespace std::string_view_literals;
  testOneFileName(L"\\\\?\\C:\\Windows\\system32\\cmd.exe"sv);
  testOneFileName(L"C:\\Windows\\system32\\cmd.exe"sv);
  testOneFileName(L"\\Windows\\system32\\cmd.exe"sv);
  testOneFileName(L"C:cmd.exe"sv);
  testOneFileName(L"cmd.exe"sv);
  testOneFileName(L"./bin/test.exe"sv);
  testOneFileName(L"bin/test.exe"sv);
  testOneFileName(L"C:\\Windows\\system32\\cmd"sv);
  testOneFileName(L"cmd"sv);
}
