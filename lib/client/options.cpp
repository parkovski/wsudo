#include "wsudo/wsudo.h"
#include "wsudo/client.h"

using namespace wsudo;

Client::Options::Options() noexcept
  : showHelp{false}
  , startService{true}
  , keepEnvironment{false}
  , useShim{true}
  , runAsSystem{false}
  , detach{false}
  , commandLine{}
{}

static wchar_t *getMyDirectory() {
  DWORD pathSize = MAX_PATH, copied;
  wchar_t *path = new wchar_t[pathSize];
  while (true) {
    copied = pathSize;
    // Note: GetModuleFileName does not resolve symlinks.
    // QueryFullProcessImageName does.
    if (!QueryFullProcessImageName(GetCurrentProcess(), 0, path, &copied)) {
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        delete[] path;
        pathSize *= 2;
        path = new wchar_t[pathSize];
        continue;
      }
      delete[] path;
      THROW_LAST_ERROR();
    } else {
      break;
    }
  }

  for (int i = (int)copied - 2; i > 0; --i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      path[i + 1] = L'\0';
      break;
    }
  }

  return path;
}

bool Client::Options::parse(int argc, const wchar_t *const *argv) noexcept {
  int i;
  // Start from arg 0 because main passes us argv+1.
  for (i = 0; i < argc; ++i) {
    std::wstring_view arg{argv[i]};
    if (arg.starts_with(L'-')) {
      if (arg == L"--") {
        ++i;
        break;
      } else if (arg == L"-h" || arg == L"--help") {
        showHelp = true;
        return true;
      } else if (arg == L"-L" || arg.starts_with(L"--log-level=")) {
        std::wstring_view loglevel;
        if (arg == L"-L") {
          if (i + 1 < argc) {
            loglevel = argv[++i];
          } else {
            log::error("expected log level after '-L'");
            return false;
          }
        } else {
          loglevel = arg.substr(wcslen(L"--log-level="));
          if (loglevel.empty()) {
            log::error("expected log level after '--log-level='");
          }
        }
        if (loglevel == L"critical") {
          log::g_outLogger->set_level(spdlog::level::off);
          log::g_errLogger->set_level(spdlog::level::critical);
        } else if (loglevel == L"error") {
          log::g_outLogger->set_level(spdlog::level::off);
          log::g_errLogger->set_level(spdlog::level::err);
        } else if (loglevel == L"warn") {
          log::g_outLogger->set_level(spdlog::level::off);
          log::g_errLogger->set_level(spdlog::level::warn);
        } else if (loglevel == L"info") {
          log::g_outLogger->set_level(spdlog::level::info);
          log::g_errLogger->set_level(spdlog::level::warn);
        } else if (loglevel == L"debug") {
          log::g_outLogger->set_level(spdlog::level::debug);
          log::g_errLogger->set_level(spdlog::level::warn);
        } else if (loglevel == L"trace") {
          log::g_outLogger->set_level(spdlog::level::trace);
          log::g_errLogger->set_level(spdlog::level::warn);
        } else if (loglevel == L"off") {
          log::g_outLogger->set_level(spdlog::level::off);
          log::g_errLogger->set_level(spdlog::level::off);
        } else {
          log::error(L"unknown log level '{}'", loglevel);
          return false;
        }
      } else if (arg == L"-E" || arg == L"--preserve-env") {
        keepEnvironment = true;
      } else if (arg == L"--no-service") {
        startService = false;
      } else if (arg == L"--no-shim") {
        useShim = false;
      } else if (arg == L"--detach") {
        detach = true;
      } else if (arg == L"-S" || arg == L"--system") {
        runAsSystem = true;
      }
    } else {
      // First non-flag means we've hit the program part of the args.
      break;
    }
  }

  if (i == argc) {
    // No program specified.
    return false;
  }

  std::wstring cl;
  if (useShim) {
    auto myDir = getMyDirectory();
    cl = myDir;
    delete[] myDir;
    cl.append(L"wsudo-shim.exe");
    cl = escapeCommandLineArg(cl);
    cl.push_back(L' ');
  }

  std::wstring program = argv[i];
  if (!resolveProgramPath(program)) {
    return false;
  }
  cl.append(escapeCommandLineArg(program));

  if (++i < argc) {
    for (; i < argc; ++i) {
      cl.push_back(L' ');
      cl.append(escapeCommandLineArg(argv[i]));
    }
  }

  commandLine = std::move(cl);
  log::debug(L"Created command line: {}", commandLine);

  return true;
}
