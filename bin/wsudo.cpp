#include "wsudo/wsudo.h"
#include "wsudo/client.h"

#include <spdlog/sinks/stdout_color_sinks.h>

using namespace wsudo;

static void showHelp() {
  log::print("Usage: wsudo [options] <program> <args...>\n\n");

  log::print("Options:\n");
  log::print("  -E, --preserve-env          preserve environment when"
             " running command\n");
  log::print("  -h, --help                  display help and exit\n");
  log::print("  --no-shim                   don't use wsudo-shim.exe\n");
  log::print("  --no-service                don't try to start the wsudo"
             " service\n");
  log::print("  --system                    run as NT AUTHORITY\\System\n");
}

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
#ifdef NDEBUG
  log::g_outLogger->set_level(spdlog::level::info);
#else
  log::g_outLogger->set_level(spdlog::level::trace);
#endif
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");
  log::g_errLogger->set_level(spdlog::level::warn);

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

  if (GetFileType(hStdout) == FILE_TYPE_CHAR
      && GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_CHAR) {
    spdlog::set_pattern("%^%l:%$ %v");
  } else {
    spdlog::set_pattern("%l: %v");
  }

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
    showHelp();
    return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
  }

  Client::Options opts;
  if (!opts.parse(argc - 1, argv + 1)) {
    showHelp();
    return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
  } else if (opts.showHelp) {
    showHelp();
    return 0;
  }

  char cdata alignas(Client)[sizeof(Client)];
  Client &client = *reinterpret_cast<Client *>(cdata);

  try {
    new (cdata) Client(PipeFullPath, argc - 1, argv + 1);
  } catch (wil::ResultException &e) {
    auto err = e.GetErrorCode();
    if (err == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      log::critical("Pipe connection failed. Is the server running?");
    }
    return (int)err;
  }
  WSUDO_SCOPE_EXIT { client.~Client(); };
  log::debug("Client initialized");

  return client(opts.commandLine);
}
