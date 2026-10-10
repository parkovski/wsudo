#include "wsudo/client.h"
#include "wsudo/wsudo.h"

#include <catch2/catch_all.hpp>

#define esc(arg) wsudo::escapeCommandLineArg(arg)

struct ClientTest {
  static void testCreateCommandLine() {
    using namespace std::string_view_literals;
    const wchar_t *const argv[] = {
      L"C:\\Program Files\\My Program\\program.exe",
      L"-x",
      L"foo bar",
      L"\"\\test\"",
      L"\\\"foo bar\"\\",
    };
    const int argc = WSUDO_ARRAYSIZE(argv);
    HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\wsudo_test_pipe",
                                   PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE,
                                   2, 128, 128, 0, nullptr);
    REQUIRE(pipe != INVALID_HANDLE_VALUE);
    WSUDO_SCOPE_EXIT { CloseHandle(pipe); };
    auto client = wsudo::Client{L"\\\\.\\pipe\\wsudo_test_pipe", argc, argv};
    auto cl = client.createCommandLine();
    REQUIRE(cl == L"\"C:\\Program Files\\My Program\\program.exe\" -x \"foo bar\" \"\\\"\\\\test\\\"\" \"\\\\\\\"foo bar\\\"\\\\\""sv);
  }
};

TEST_CASE("Command line parsing", "[commandline]") {
  using namespace std::string_view_literals;
  REQUIRE(esc(L"abc.exe") == L"abc.exe"sv);
  REQUIRE(esc(L"my file") == L"\"my file\""sv);
  REQUIRE(esc(L"C:\\foo.exe") == L"C:\\foo.exe"sv);
  REQUIRE(esc(L"\\\"") == L"\"\\\\\\\"\""sv);       // \" -> "\\\""
  REQUIRE(esc(L"\\\\") == L"\\\\"sv);               // \\ -> \\    (.)
  REQUIRE(esc(L"\\\\\"") == L"\"\\\\\\\\\\\"\""sv); // \\" -> "\\\\\""
  REQUIRE(esc(L"test \"test\\ test") == L"\"test \\\"test\\ test\""sv);

  ClientTest::testCreateCommandLine();
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
