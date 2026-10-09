#include "wsudo/client.h"

#include <catch2/catch_all.hpp>

#define esc(arg) wsudo::Client::escapeCommandLineArg(arg)

struct ClientTest {
  static HANDLE getPipe() {
    return CreateNamedPipeW(L"\\\\.\\pipe\\wsudo_test_pipe",
                            PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE,
                            2, 128, 128, 0, nullptr);
  }

  static wsudo::Client getClient(int argc, const wchar_t *const *argv) {
    return wsudo::Client{L"\\\\.\\pipe\\wsudo_test_pipe", argc, argv};
  }

  static void testCommandLineParsing() {
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
    HANDLE pipe = getPipe();
    REQUIRE(pipe != INVALID_HANDLE_VALUE);
    WSUDO_SCOPE_EXIT { CloseHandle(pipe); };
    auto client = getClient(5, argv);
    auto cl = client.createCommandLine();
    REQUIRE(cl == L"\"C:\\Program Files\\My Program\\program.exe\" -x \"foo bar\" \"\\\"\\\\test\\\"\" \"\\\\\\\"foo bar\\\"\\\\\""s);
  }

  static void testOneFileName(const wchar_t *filename, bool success = true) {
    HANDLE pipe = getPipe();
    REQUIRE(pipe != INVALID_HANDLE_VALUE);
    WSUDO_SCOPE_EXIT { CloseHandle(pipe); };
    auto client = getClient(1, &filename);
    REQUIRE(client.resolveProgramPath() == success);
  }

  static void testFileNameResolution() {
    testOneFileName(L"\\\\?\\C:\\Windows\\System32\\cmd.exe");
    testOneFileName(L"C:\\Windows\\System32\\cmd.exe");
    testOneFileName(L"\\Windows\\System32\\cmd.exe");
    testOneFileName(L"C:cmd.exe");
    testOneFileName(L"cmd.exe");
    testOneFileName(L"./bin/test.exe");
    testOneFileName(L"bin/test.exe");
    testOneFileName(L"C:\\Windows\\System32\\cmd");
    testOneFileName(L"cmd");
  }
};

TEST_CASE("Command line parsing", "[commandline]") {
  ClientTest::testCommandLineParsing();
}

TEST_CASE("File name resolution", "[commandline]") {
  ClientTest::testFileNameResolution();
}
