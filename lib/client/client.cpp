#include "wsudo/client.h"

#define SECURITY_WIN32
#include <Security.h>
#pragma comment(lib, "Secur32.lib")

using namespace wsudo;

void Client::lookupUsername() {
  std::wstring domain;
  std::wstring username;
  ULONG usernameLength = 0;
  GetUserNameEx(NameSamCompatible, nullptr, &usernameLength);
  auto err = GetLastError();
  if (err == ERROR_MORE_DATA) {
    username.resize(usernameLength);
    if (!GetUserNameEx(NameSamCompatible, username.data(), &usernameLength)) {
      log::critical("Can't get username.\n");
      THROW_LAST_ERROR();
    }
    // It ends in a null that we don't want copied.
    username.erase(username.cend() - 1);
  } else {
    log::critical("Can't get username.\n");
    THROW_WIN32(err);
  }

  if (auto slash = username.find(L'\\'); slash != std::wstring::npos) {
    domain = username.substr(0, slash);
    _domain = to_utf8(domain);
    username.erase(0, slash + 1);
  }
  _username = to_utf8(username);

  if (!domain.empty()) {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computerNameLength = MAX_COMPUTERNAME_LENGTH;
    if (GetComputerName(computerName, &computerNameLength)) {
      if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE,
                        computerName, -1, domain.c_str(), -1) == CSTR_EQUAL) {
        // Ignore local domain.
        _domain = ".";
      }
    }
  }

  log::debug("Current user: {}{}{}", _domain == "." ? "" : _domain,
             (_domain.empty() || _domain == ".") ? "" : "\\", _username);
}

bool Client::readConsolePassword(std::string &password) const {
  if (_domain.empty() || _domain == ".") {
    log::print("[wsudo] password for {}: ", _username);
  } else {
    log::print("[wsudo] password for {}\\{}: ", _domain, _username);
  }

  fflush(stdout);

  HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD previousInMode;
  GetConsoleMode(hStdIn, &previousInMode);
  SetConsoleMode(hStdIn, ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE);
  WSUDO_SCOPE_EXIT { SetConsoleMode(hStdIn, previousInMode); };

  std::wstring wpassword;
  WSUDO_SCOPE_EXIT { wpassword.assign(wpassword.length(), L'\0'); };
  while (true) {
    // TODO: Some characters are 2 wchars. Backspace should handle this. Also
    // should they be read 2 at a time?
    wchar_t ch;
    DWORD chRead;
    if (!ReadConsole(hStdIn, &ch, 1, &chRead, nullptr)) {
      return false;
    }
    if (ch == 13 || ch == 10) {
      // Enter
      log::print("\n");
      break;
    } else if (ch == 8 || ch == 0x7F) {
      // Backspace
      if (wpassword.length() > 0) {
        wpassword.erase(wpassword.cend() - 1);
      }
    } else if (ch == 3) {
      // Ctrl-C
      log::print("\nCanceled.\n");
      return false;
    } else {
      wpassword.push_back(ch);
    }
  }
  // TODO: make sure this doesn't leave the password in memory somewhere.
  // better to convert 1 char at a time while reading.
  password = to_utf8(wpassword);
  return true;
}

bool Client::resolveProgramPath() {
  size_t i, j;
  std::wstring resolvedPath;
  bool searchPath = false;
  DWORD curdirLength = GetCurrentDirectory(0, nullptr);
  auto curdir = std::make_unique<wchar_t[]>(curdirLength);
  GetCurrentDirectory(curdirLength, curdir.get());
  if (_program[0] == '\\' || _program[0] == '/') {
    if (_program[1] == '\\' || _program[1] == '/') {
      if (
        _program[2] == '?' && _program[0] == '\\' && _program[1] == '\\'
        && _program[3] == '\\'
      ) {
        // Absolute NT path (\\?\...) - must use backslashes.
        resolvedPath = _program;
      } else {
        // UNC path.
        resolvedPath = _program;
      }
    } else {
      // Absolute path without drive letter.
      if ((curdir[0] == '\\' || curdir[0] == '/')
        && (curdir[1] == '\\' || curdir[1] == '/')) {
        // curdir is a UNC path. The root is in the form "\\server\share".
        for (i = 2; curdir[i] != 0; ++i) {
          if (curdir[i] == '\\' || curdir[i] == '/') {
            // Server is curdir[2..i].
            for (j = i + 1; curdir[j] != 0; ++j) {
              if (curdir[j] == '\\' || curdir[j] == '/') {
                // Share is curdir[i+1..j].
                // Root path is curdir[0..j].
                resolvedPath = std::wstring_view{curdir.get(), j};
                goto breakUnc;
              }
            }
            // Didn't find a \ or /. curdir is something like "\\server\share".
            resolvedPath = std::wstring_view{curdir.get(), j};
            goto breakUnc;
          }
        }
        // Didn't find something of the form "\\server\share...".
        log::error(L"Invalid UNC path \"{}\".", curdir.get());
        return false;
      breakUnc:
        // resolvedPath has no trailing \. _program has an initial \.
        resolvedPath.append(_program);
      } else {
        assert(((curdir[0] >= 'a' && curdir[0] <= 'z')
                || (curdir[0] >= 'A' && curdir[0] <= 'Z'))
               && curdir[1] == ':');
        resolvedPath = std::wstring_view{curdir.get(), 2}; // e.g., C:
        resolvedPath += _program; // _program has an initial \.
      }
    }
  } else if (
    (
      (_program[0] >= 'A' && _program[0] <= 'Z')
      || (_program[0] >= 'a' && _program[0] <= 'z')
    )
    && _program[1] == ':'
  ) {
    if (_program[2] == '\\' || _program[2] == '/') {
      // Absolute path with drive letter.
      resolvedPath = _program;
    } else {
      // Relative path with drive letter. Windows doesn't keep track of current
      // directories for each drive, so require that the drive is the same as
      // the one from the current directory.
      if ((_program[0] | 0x20) != (curdir[0] | 0x20)) {
        log::error("Drive letter must match current directory.");
        return false;
      }
      // Now it's either a relative path to the current directory
      // (includes a \) or a path lookup (no \).
      for (i = 3; i < _program.length(); ++i) {
        if (_program[i] == '\\' || _program[i] == '/') {
          resolvedPath = curdir.get();
          if (!resolvedPath.ends_with('\\')
            && !resolvedPath.ends_with('/')) {
            resolvedPath.push_back('\\');
          }
          resolvedPath.append(std::wstring_view{_program}.substr(2));
          break;
        }
      }
      if (resolvedPath.empty()) {
        // Didn't find a \. It's path relative.
        resolvedPath = std::wstring_view{_program}.substr(2);
        searchPath = true;
      }
    }
  } else if (
    _program[0] == '.' && (_program[1] == '\\' || _program[1] == '/')
  ) {
    // Relative path to current dir.
    resolvedPath = curdir.get();
    if (!resolvedPath.ends_with('\\') && !resolvedPath.ends_with('/')) {
      resolvedPath.push_back('\\');
    }
    resolvedPath.append(std::wstring_view{_program}.substr(2));
  } else {
    for (i = 0; i < _program.length(); ++i) {
      if (_program[i] == '\\' || _program[i] == '/') {
        // Relative path to current dir.
        resolvedPath = curdir.get();
        if (!resolvedPath.ends_with('\\') && !resolvedPath.ends_with('/')) {
          resolvedPath.push_back('\\');
        }
        resolvedPath.append(_program);
        break;
      }
    }
    if (resolvedPath.empty()) {
      // Relative to Path environment variable.
      resolvedPath = _program;
      searchPath = true;
    }
  }

  auto isValidFile = [](std::wstring &path) -> bool {
    DWORD attributes = GetFileAttributes(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      path += L".exe";
      attributes = GetFileAttributes(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        path.erase(path.length() - 4);
        return false;
      }
    }
    return true;
  };

  if (searchPath) {
    log::debug(L"Searching Path for \"{}\".", _program);
    DWORD pathLength = GetEnvironmentVariable(L"Path", nullptr, 0);
    auto path = std::make_unique<wchar_t[]>(pathLength + 1);
    std::wstring potentialPath;
    GetEnvironmentVariable(L"Path", path.get(), pathLength + 1);
    i = j = 0;
    while (j <= pathLength) {
      if (path[j] == ';' || path[j] == '\0') {
        if (j == i) {
          // Empty path segment
          i = ++j;
          continue;
        }
        std::wstring_view segment{path.get() + i, j - i};
        potentialPath = segment;
        if (!segment.ends_with('\\') && !segment.ends_with('/')) {
          potentialPath.push_back('\\');
        }
        potentialPath.append(resolvedPath);
        if (isValidFile(potentialPath)) {
          _program = potentialPath;
          log::debug(L"Found \"{}\".", _program);
          return true;
        }
        i = ++j;
        continue;
      }
      ++j;
    }
  } else {
    if (isValidFile(resolvedPath)) {
      _program = resolvedPath;
      log::debug(L"Found \"{}\".", _program);
      return true;
    }
  }

  log::error(L"Could not find \"{}\".", _program);
  log::trace(L"Note: path was resolved to \"{}\".", resolvedPath);
  return false;
}

std::wstring Client::escapeCommandLineArg(std::wstring_view arg) {
  std::wstring newarg;
  bool hasSpace = false;
  newarg.reserve(arg.size());

  for (size_t i = 0; i < arg.size(); ++i) {
    switch (arg[i]) {
    case L' ':
    case L'\t':
    case L'\r':
    case L'\n':
      hasSpace = true;
      break;
    }
  }

  if (hasSpace) {
    newarg.push_back(L'"');
  }
  for (size_t i = 0; i < arg.size(); ++i) {
    switch (arg[i]) {
    case L'"':
      if (hasSpace) {
        newarg.append(L"\\\"");
      } else {
        newarg.append(L"\"\\\"");
        while (++i < arg.size()) {
          switch (arg[i]) {
          case L'"':
            newarg.append(L"\\\"");
            break;

          case L'\\':
            newarg.append(L"\\\\");
            break;

          default:
            newarg.push_back(arg[i]);
            break;
          }
        }
        --i;
        newarg.push_back(L'"');
      }
      break;

    case L'\\':
      {
        size_t j = i + 1;
        while (j < arg.size() && arg[j] == L'\\') {
          ++j;
        }
        if (j < arg.size() && arg[j] == '"') {
          if (!hasSpace) {
            newarg.push_back(L'"');
          }
          for (size_t k = i; k < j; ++k) {
            newarg.append(L"\\\\");
          }
          while (j < arg.size()) {
            switch (arg[j]) {
            case L'"':
              newarg.append(L"\\\"");
              break;

            case L'\\':
              newarg.append(L"\\\\");
              break;

            default:
              newarg.push_back(arg[j]);
              break;
            }
            ++j;
          }
          if (!hasSpace) {
            newarg.push_back(L'"');
          }
        } else {
          for (size_t k = i; k < j; ++k) {
            newarg.push_back(L'\\');
          }
        }
        i = j - 1;
      }
      break;

    default:
      newarg.push_back(arg[i]);
      break;
    }
  }
  if (hasSpace) {
    newarg.push_back('"');
  }

  return newarg;
}

std::wstring Client::createCommandLine() const {
  std::wstring cl{escapeCommandLineArg(_program)};
  std::wstring arg;
  for (int i = 0; i < _argc1; ++i) {
    arg = escapeCommandLineArg(_argv1[i]);
    cl.push_back(L' ');
    cl.append(arg);
  }
  log::debug(L"Created command line: {}", cl);
  return cl;
}

PROCESS_INFORMATION
Client::createSuspendedProcess() const {
  STARTUPINFOW si{};
  si.cb = sizeof(STARTUPINFOW);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi{};
  std::wstring commandLine{createCommandLine()};
  // The system is allowed to modify this string, so we can't use c_str as it
  // returns a const pointer.
  commandLine.push_back(L'\0');
  commandLine.push_back(L'\0');
  if (!CreateProcess(nullptr, commandLine.data(), nullptr, nullptr, true,
                     CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                     nullptr, nullptr, &si, &pi)) {
    log::error("Process creation failed.");
    THROW_LAST_ERROR();
  }
  return pi;
}

bool Client::userHasActiveSession() {
  _conn.send(msg::QuerySession{_domain, _username, "key"});
  return std::holds_alternative<msg::Success>(_conn.recv());
}

bool Client::validateCredentials(std::string &password) {
  _conn.send(msg::Credential{_domain, _username, password});
  auto res = _conn.recv();
  if (std::holds_alternative<msg::Success>(res)) {
    return true;
  }
  log::error("Invalid credentials.");
  return false;
}

bool Client::bless(HANDLE process) {
  _conn.send(msg::Bless{"key", process});
  auto res = _conn.recv();
  if (std::holds_alternative<msg::Success>(res)) {
    return true;
  }
  log::error("Bless (elevate) process failed.");
  return false;
}

int Client::resume(PROCESS_INFORMATION &pi, bool wait) {
  DWORD exitCode = 0;
  // TODO: Return errors if any of this fails.
  ResumeThread(pi.hThread);
  if (wait) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
  }
  return static_cast<int>(exitCode);
}

Client::Client(const std::wstring &pipeName, int argc,
               const wchar_t *const *argv)
  : _conn{pipeName}
  , _program{argv[0]}
  , _argc1{argc - 1}
  , _argv1{argv + 1}
{
  lookupUsername();
}

HRESULT Client::operator()() {
  if (!resolveProgramPath()) {
    return ERROR_FILE_NOT_FOUND;
  }

  if (!userHasActiveSession()) {
    std::string password;
    WSUDO_SCOPE_EXIT { password.assign(password.length(), '\0'); };
    if (!readConsolePassword(password)) {
      return ERROR_CANCELLED;
    }
    if (!validateCredentials(password)) {
      return ERROR_LOGON_FAILURE;
    }
  }

  auto pi = createSuspendedProcess();
  if (pi.hProcess == nullptr) {
    // TODO: this has a pretty accurate description but figure out better error
    // handling here.
    return ERROR_NO_PROC_SLOTS;
  }
  WSUDO_SCOPE_EXIT {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  };

  if (!bless(pi.hProcess)) {
    TerminateProcess(pi.hProcess, (UINT)(-1));
    return ERROR_ACCESS_DENIED;
  }

  return (HRESULT)resume(pi, true);
}
