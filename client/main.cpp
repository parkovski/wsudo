#include "stdo/stdo.h"
#include "stdo/winsupport.h"

#include <fmt/format.h>
#include <Windows.h>
#include <string>
#include <cstdio>

using namespace stdo;

DWORD WINAPI Test(LPVOID p) {
  fmt::print("Hello from test thread\n");
  STARTUPINFOW si{};
  RtlZeroMemory(&si, sizeof(STARTUPINFOW));
  si.cb = sizeof(STARTUPINFOW);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  //si.dwFlags = STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi;
  auto const *name = L"C:\\Windows\\System32\\whoami.exe";
  std::wstring args(name);
  args.append(L" /all").push_back(L'\0');
  if (!CreateProcessW(name, args.data(), nullptr, nullptr, true, CREATE_PRESERVE_CODE_AUTHZ_LEVEL | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi)) {
    wchar_t buf[1024];
    DWORD err = GetLastError();
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buf, sizeof(buf)/sizeof(buf[0]), nullptr)) {
      fmt::print("Process creation error {}; format error {}\n", err, GetLastError());
    } else {
      wprintf(L"Process creation failed: %ls\n", buf);
    }
    return 1;
  }
  fmt::print("Created process {}\n", pi.dwProcessId);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitcode;
  GetExitCodeProcess(pi.hProcess, &exitcode);
  fmt::print("Process ended with code {}\n", exitcode);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("stdo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("stdo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  STDO_SCOPEEXIT { spdlog::drop_all(); };

  fmt::print(
    "Hello from client. I am process {0}; &Test = {1} (0x{1:X})\nEnter to exit.\n",
    GetCurrentProcessId(), reinterpret_cast<unsigned long long>(&Test)
  );
  WaitForSingleObject(GetCurrentProcess(), INFINITE);
  // while (std::getchar() != '\n') {
  //   //
  // }
  return 0;
}
