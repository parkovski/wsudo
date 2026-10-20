#include "wsudo/wsudo.h"
#include <Windows.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace wsudo;

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo-shim");
  log::g_errLogger = log::g_outLogger;
  spdlog::set_level(spdlog::level::off);
  if (argc < 2) {
    return ERROR_BAD_COMMAND;
  }
  if (GetFileAttributes(argv[1]) == INVALID_FILE_ATTRIBUTES) {
    return ERROR_FILE_NOT_FOUND;
  }
  std::wstring commandLine;
  commandLine = escapeCommandLineArg(argv[1]);
  for (int i = 2; i < argc; ++i) {
    commandLine.push_back(L' ');
    commandLine.append(escapeCommandLineArg(argv[i]));
  }
  commandLine.push_back(L'\0');
  STARTUPINFO si{};
  si.cb = sizeof(STARTUPINFO);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(
    nullptr, commandLine.data(), nullptr, nullptr, true,
    CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP
    | CREATE_PRESERVE_CODE_AUTHZ_LEVEL, nullptr, nullptr, &si, &pi
  )) {
    return GetLastError();
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)exitCode;
}
