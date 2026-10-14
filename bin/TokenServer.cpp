#include "wsudo/log.h"
#include "wsudo/wsudo.h"
#include "wsudo/server.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/win_eventlog_sink.h>

#include <fmt/format.h>

#include <Psapi.h>

#include <string>

using namespace wsudo;

static BOOL WINAPI consoleControlHandler(DWORD event) {
  const char *eventName;
  int exitCode = 0;
  switch (event) {
  case CTRL_C_EVENT:
    eventName = "Ctrl-C";
    exitCode = STATUS_CONTROL_C_EXIT;
    break;
  case CTRL_BREAK_EVENT:
    eventName = "Ctrl-Break";
    exitCode = STATUS_CONTROL_C_EXIT;
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
    g_server->quit(exitCode);
    // If this attempt fails, next time we will hit the terminate() path.
    g_server = nullptr;
  } else {
    log::warn("Can't notify server thread; forcing shutdown.");
    std::terminate();
  }

  return true;
}

static bool isLocalSystem() {
  BOOL  bIsMember = false;
  BYTE  sidService[SECURITY_MAX_SID_SIZE] = { 0 };
  DWORD cbSidSize = SECURITY_MAX_SID_SIZE;

  if (CreateWellKnownSid(WinLocalSystemSid, nullptr, &sidService, &cbSidSize))
  {
    CheckTokenMembership(nullptr, &sidService, &bIsMember);
  }

  return bIsMember;
}

static bool isElevated() {
  HANDLE hToken = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
    TOKEN_ELEVATION elevation;
    DWORD cbSize = sizeof(elevation);
    if (GetTokenInformation(hToken, TokenElevation, &elevation,
                            sizeof(elevation), &cbSize)) {
      CloseHandle(hToken);
      return elevation.TokenIsElevated;
    }
    CloseHandle(hToken);
  }
  return false;
}


void WINAPI serviceCtrlHandler(DWORD ctrl) {
  log::debug("Service received message {}.", ctrl);
  switch (ctrl) {
  case SERVICE_CONTROL_INTERROGATE:
    break;

  case SERVICE_CONTROL_STOP:
    g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    g_serviceStatus.dwControlsAccepted = 0;
    g_serviceStatus.dwWaitHint = 500;
    g_serviceStatus.dwCheckPoint = 1;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
    g_serviceStatus.dwWaitHint = 0;

    g_server->quit();
    return;

  default:
    break;
  }

  SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

VOID WINAPI serviceMain(DWORD argc, LPWSTR *argv) {
  g_serviceStatusHandle = RegisterServiceCtrlHandler(
    L"wsudo",
    serviceCtrlHandler
  );

  // These SERVICE_STATUS members remain as set here
  g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_serviceStatus.dwServiceSpecificExitCode = 0;

  if (!g_serviceStatusHandle) {
    auto err = GetLastError();
    log::error("RegisterServiceCtrlHandler failed: (0x{:X}): {}",
               err, lastErrorString(err));
    g_serviceStatus.dwWin32ExitCode = (DWORD)HRESULT_FROM_WIN32(err);
    return;
  }

  // Report initial status to the SCM
  g_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
  g_serviceStatus.dwWin32ExitCode = NO_ERROR;
  g_serviceStatus.dwWaitHint = 500;
  g_serviceStatus.dwControlsAccepted = 0;
  g_serviceStatus.dwCheckPoint = 1;
  SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
  g_serviceStatus.dwWaitHint = 0;

  Server server{PipeFullPath};
  g_server = &server;

  int exitCode = server(2, 2);

  g_serviceStatus.dwWin32ExitCode = (DWORD)exitCode;
  g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
  g_serviceStatus.dwControlsAccepted = 0;
  g_serviceStatus.dwCheckPoint = 0;
  SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

int wmain(int argc, wchar_t *argv[]) {
  g_isService = isLocalSystem();
  if (g_isService) {
    auto sink = std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("wsudo");
    log::g_outLogger = std::make_shared<spdlog::logger>("wsudo", sink);
    spdlog::register_logger(log::g_outLogger);
    log::g_errLogger = log::g_outLogger;
#ifdef NDEBUG
    log::g_outLogger->set_level(spdlog::level::info);
#else
    log::g_outLogger->set_level(spdlog::level::debug);
#endif
    spdlog::set_pattern("(#%t) %v");
  } else {
    log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
    log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");

    log::g_errLogger->set_level(spdlog::level::warn);
#ifdef NDEBUG
    log::g_outLogger->set_level(spdlog::level::info);
    // Use a complete format for release mode.
    spdlog::set_pattern("[%Y-%m-%d %T.%e] %^[%5l]%$ %v");
#else
    log::g_outLogger->set_level(spdlog::level::trace);
    // Set a more compact, readable log format for debugging.
    spdlog::set_pattern("\033[90m[%T.%e]\033[m (#%5t) %^[%5l]%$ %v");
#endif
  }

  WSUDO_SCOPE_EXIT {
    log::g_outLogger = nullptr;
    log::g_errLogger = nullptr;
  };

  if (!isElevated()) {
    log::error("This program must be run from an elevated context.");
    return HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED);
  }

  if (argc == 2) {
    if (!wcscmp(argv[1], L"install")) {
      log::info("Installing wsudo token server...");
      auto result = installService();
      if (SUCCEEDED(result)) {
        log::info("Installation successful.");
        return 0;
      } else {
        log::error("Installation failed: (0x{:X}): {}",
                   result, lastErrorString(result));
        return result;
      }
    } else if (!wcscmp(argv[1], L"uninstall")) {
      log::info("Uninstalling wsudo token server...");
      auto result = uninstallService();
      if (SUCCEEDED(result)) {
        log::info("Uninstallation successful.");
        return 0;
      } else {
        log::error("Uninstallation failed: (0x{:X}): {}",
                   result, lastErrorString(result));
        return result;
      }
    } else if (!wcscmp(argv[1], L"clear-event-log")) {
      auto result = clearEventLog();
      if (SUCCEEDED(result)) {
        log::info("Event log cleared.");
        return 0;
      }
      log::error("Couldn't clear event log.");
      return result;
    } else {
      log::error(L"Unknown argument '{}'.", argv[1]);
      return E_INVALIDARG;
    }
  } else if (argc > 2) {
    log::error("Too many arguments.");
    return E_INVALIDARG;
  }

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD stdinMode, stdoutMode;

  if (!g_isService) {
    GetConsoleMode(hStdin, &stdinMode);
    SetConsoleMode(hStdin, stdinMode | ENABLE_PROCESSED_INPUT);
    GetConsoleMode(hStdout, &stdoutMode);
    SetConsoleMode(hStdout,
                   ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                   ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }

  // Restore console modes on exit.
  WSUDO_SCOPE_EXIT {
    if (!g_isService) {
      SetConsoleMode(hStdin, stdinMode);
      SetConsoleMode(hStdout, stdoutMode);
    }
  };

  log::info("WSudo Token Server: Pid = {}.", GetCurrentProcessId());

  if (!g_isService) {
    // Clear control handlers and then set ours.
    if (!SetConsoleCtrlHandler(nullptr, false) ||
        !SetConsoleCtrlHandler(consoleControlHandler, true)) {
      log::warn("Failed to set Ctrl-C handler; kill process to exit.");
    } else {
      log::info("Starting server. Press Ctrl-C to exit.");
    }

    Server server{PipeFullPath};
    g_server = &server;
    return server(2);
  } else {
    wchar_t serviceName[] = L"wsudo";
    SERVICE_TABLE_ENTRY dispatchTable[] = {
      { serviceName, serviceMain },
      { nullptr, nullptr },
    };
    if (!StartServiceCtrlDispatcher(dispatchTable)) {
      auto err = GetLastError();
      log::error("StartServiceCtrlDispatcher failed: (0x{:X}): {}",
                 err, lastErrorString(err));
      return HRESULT_FROM_WIN32(err);
    }

    return (int)g_serviceStatus.dwWin32ExitCode;
  }
}
