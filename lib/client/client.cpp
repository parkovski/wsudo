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
}

bool Client::readConsolePassword(std::string &password) const {
  if (_domain.empty()) {
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
  return true;
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
    log::error("CreateProcess failed!");
    THROW_LAST_ERROR();
  }
  return pi;
}

bool Client::userHasActiveSession() {
  _conn.send(msg::QuerySession{_domain, _username});
  return std::holds_alternative<msg::Success>(_conn.recv());
}

bool Client::validateCredentials(std::string &password) {
  _conn.send(msg::Credential{_domain, _username, password});
  auto res = _conn.recv();
  if (std::holds_alternative<msg::Success>(res)) {
    return true;
  }
  return false;
}

bool Client::bless(HANDLE process) {
  _conn.send(msg::Bless{process});
  auto res = _conn.recv();
  if (std::holds_alternative<msg::Success>(res)) {
    return true;
  }
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
