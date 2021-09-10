#include "wsudo/server.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/format.h>
#include <Psapi.h>
#include <cstring>
#include <string>
#include <iostream>
#include <system_error>
#include <thread>

using namespace wsudo;

static HANDLE gs_quitEventHandle = nullptr;
static Server *g_server = nullptr;
BOOL WINAPI consoleControlHandler(DWORD event) {
  const char *eventName;
  switch (event) {
  case CTRL_C_EVENT:
    eventName = "Ctrl-C";
    break;
  case CTRL_BREAK_EVENT:
    eventName = "Ctrl-Break";
    break;
  case CTRL_CLOSE_EVENT:
    eventName = "close";
    break;
  case CTRL_LOGOFF_EVENT:
    eventName = "logoff";
    break;
  case CTRL_SHUTDOWN_EVENT:
    eventName = "shutdown";
    break;
  default:
    eventName = "unknown";
  }

  log::info("Received {} event, quitting.", eventName);
  if (g_server) {
    g_server->quit();
    g_server = nullptr;
    return true;
  }
  if (!gs_quitEventHandle || !SetEvent(gs_quitEventHandle)) {
    log::warn("Can't notify server thread; forcing shutdown.");
    std::terminate();
  }

  // If this attempt fails, next time we will hit the terminate() path.
  gs_quitEventHandle = nullptr;
  return true;
}

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
#ifndef NDEBUG
  // Set a more compact, readable log format for debugging.
  spdlog::set_pattern("\033[90m[%T.%e]\033[m (#%5t) %^[%5l]%$ %v");
#else
  // Use a complete format for release mode.
  spdlog::set_pattern("[%Y-%m-%d %T.%e] %^[%5l]%$ %v");
#endif

  // VC++ deadlock bug
  WSUDO_SCOPEEXIT { spdlog::drop_all(); };

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD stdinMode, stdoutMode;
  GetConsoleMode(hStdin, &stdinMode);
  SetConsoleMode(hStdin, stdinMode | ENABLE_PROCESSED_INPUT);
  GetConsoleMode(hStdout, &stdoutMode);
  SetConsoleMode(hStdout,
                 ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                 ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  // Restore console modes on exit.
  WSUDO_SCOPEEXIT {
    SetConsoleMode(hStdin, stdinMode);
    SetConsoleMode(hStdout, stdoutMode);
  };

  log::info("WSudo Token Server: Pid = {}.", GetCurrentProcessId());

  // Clear control handlers and then set ours.
  if (!SetConsoleCtrlHandler(nullptr, false) ||
      !SetConsoleCtrlHandler(consoleControlHandler, true))
  {
    log::warn("Failed to set Ctrl-C handler; kill process to exit.");
  } else {
    log::info("Starting server. Press Ctrl-C to exit.");
  }

  if (argc >= 2 && argv[1][0] == L'-' && argv[1][1] == L'2' && argv[1][2] == 0) {
    log::debug("Using CorIO server.");
    Server server{PipeFullPath};
    g_server = &server;
    server(2);
    return 0;
  }

  server::Config config{ PipeFullPath, &gs_quitEventHandle };
  std::thread serverThread{&server::serverMain, std::ref(config)};
  serverThread.join();
  log::info("Event loop returned {}.", server::statusToString(config.status));
  return 0;
}
