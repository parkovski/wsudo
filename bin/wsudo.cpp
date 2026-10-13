#include "wsudo/wsudo.h"
#include "wsudo/client.h"

#include <spdlog/sinks/stdout_color_sinks.h>

using namespace wsudo;

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  spdlog::set_pattern("%^%l:%$ %v");

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD stdinMode, stdoutMode;

  GetConsoleMode(hStdin, &stdinMode);
  SetConsoleMode(hStdin,
                 ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
                 ENABLE_ECHO_INPUT);
  GetConsoleMode(hStdout, &stdoutMode);
  SetConsoleMode(hStdout,
                 ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                 ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  // Restore console modes on exit.
  WSUDO_SCOPE_EXIT {
    SetConsoleMode(hStdin, stdinMode);
    SetConsoleMode(hStdout, stdoutMode);
  };

  if (argc < 2) {
    log::eprint("Usage: wsudo <program> <args>\n");
    return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
  }

  char cdata alignas(Client)[sizeof(Client)];
  Client &client = *reinterpret_cast<Client *>(cdata);

  try {
    new (cdata) Client(PipeFullPath, argc - 1, argv + 1);
  } catch (wil::ResultException &e) {
    if (e.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      log::critical("Pipe connection failed. Is the server running?");
    }
    return (int)e.GetErrorCode();
  }
  WSUDO_SCOPE_EXIT { client.~Client(); };

  log::debug("Client initialized");
  return (int)client();
}
