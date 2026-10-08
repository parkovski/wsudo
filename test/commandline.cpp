#include "wsudo/client.h"

#include <catch2/catch_all.hpp>

#define esc(arg) wsudo::Client::escapeCommandLineArg(arg)

struct ClientTest {
  static void test() {
    using namespace std::string_literals;
    REQUIRE(esc(L"abc.exe") == L"abc.exe"s);
    REQUIRE(esc(L"my file") == L"\"my file\""s);
    REQUIRE(esc(L"C:\\foo.exe") == L"C:\\foo.exe"s);
    REQUIRE(esc(L"\\\"") == L"\"\\\\\\\"\""s);       // \" -> "\\\""
    REQUIRE(esc(L"\\\\") == L"\\\\"s);               // \\ -> \\ (EOL)
    REQUIRE(esc(L"\\\\\"") == L"\"\\\\\\\\\\\"\""s); // \\" -> "\\\\\""
    REQUIRE(esc(L"test \"test\\ test") == L"\"test \\\"test\\ test\""s);

    const wchar_t *const argv[] = {
      L"C:\\Program Files\\My Program\\program.exe",
      L"-x",
      L"foo bar",
      L"\"\\test\"",
      L"\\\"foo bar\"\\",
    };
    HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\wsudo_test_pipe",
                                   PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 2,
                                   128, 128, 0, nullptr);
    REQUIRE(pipe != INVALID_HANDLE_VALUE);
    WSUDO_SCOPE_EXIT { CloseHandle(pipe); };
    wsudo::Client client{L"\\\\.\\pipe\\wsudo_test_pipe"s, 5, argv};
    auto cl = client.createCommandLine();
    REQUIRE(cl == L"\"C:\\Program Files\\My Program\\program.exe\" -x \"foo bar\" \"\\\"\\\\test\\\"\" \"\\\\\\\"foo bar\\\"\\\\\""s);
  }
};

TEST_CASE("Command line parsing", "[commandline]") {
  ClientTest::test();
}
