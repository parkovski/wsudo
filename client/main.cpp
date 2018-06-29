#include "stdo/client.h"

#include <fmt/format.h>
#include <string>
#include <iostream>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>

#define SECURITY_WIN32
#include <Security.h>
#pragma comment(lib, "Secur32.lib")

using namespace stdo;

void ClientConnection::connect(
  LPSECURITY_ATTRIBUTES secAttr,
  const wchar_t *pipeName,
  int attempts
)
{
  if (attempts >= MaxConnectAttempts) {
    return;
  }
  HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            secAttr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    WaitNamedPipeW(pipeName, NMPWAIT_USE_DEFAULT_WAIT);
    connect(secAttr, pipeName, ++attempts);
    return;
  }

  _pipe = pipe;
}

ClientConnection::ClientConnection(const wchar_t *pipeName) {
  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = false;
  secAttr.lpSecurityDescriptor = nullptr;
  connect(&secAttr, pipeName, 0);
  if (good()) {
    _buffer.reserve(PipeBufferSize);
  }
}

bool ClientConnection::negotiate(const wchar_t *credentials, size_t length) {
  DWORD bytes;
  size_t messageLength = length * sizeof(wchar_t) + 4;
  _buffer.resize(messageLength);
  assert(strlen(MsgHeaderCredential) == 4);
  std::memcpy(_buffer.data(), MsgHeaderCredential, 4);
  std::memcpy(_buffer.data() + 4, credentials, length * sizeof(wchar_t));
  log::trace("Writing credential message, size {}", messageLength);
  if (
    !WriteFile(_pipe, _buffer.data(), (DWORD)messageLength, &bytes, nullptr) ||
    bytes != messageLength
  )
  {
    return false;
  }

  _buffer.resize(PipeBufferSize);
  if (!ReadFile(_pipe, _buffer.data(), PipeBufferSize, &bytes, nullptr)) {
    return false;
  }
  _buffer.resize(bytes);
  _buffer.push_back(0);
  log::info("Negotiate response: {}", _buffer.data());
  return true;
}

bool ClientConnection::bless(HANDLE process) {
  DWORD bytes;
  size_t messageLength = 4 + sizeof(uint64_t);
  _buffer.resize(messageLength);
  uint64_t handle64 = (size_t)process;
  assert(strlen(MsgHeaderBless) == 4);
  std::memcpy(_buffer.data(), MsgHeaderBless, 4);
  std::memcpy(_buffer.data() + 4, &handle64, sizeof(uint64_t));
  log::trace("Writing bless message, size {}", messageLength);
  if (
    !WriteFile(_pipe, _buffer.data(), (DWORD)messageLength, &bytes, nullptr) ||
    bytes != messageLength
  )
  {
    return false;
  }

  _buffer.resize(PipeBufferSize);
  if (!ReadFile(_pipe, _buffer.data(), PipeBufferSize, &bytes, nullptr)) {
    return false;
  }
  _buffer.resize(bytes);
  _buffer.push_back(0);
  log::info("Bless response: {}", _buffer.data());
  return true;
}

// Returns {process, thread}
std::pair<HANDLE, HANDLE> createProcess(int argc, wchar_t *argv[]) {
  STARTUPINFOW si{};
  si.cb = sizeof(STARTUPINFOW);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi;
  const wchar_t *name = L"C:\\Windows\\System32\\cmd.exe";
  std::wstring mutableName{name};
  mutableName.push_back(0);
  if (!CreateProcessW(name, mutableName.data(), nullptr, nullptr, true,
                      CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                      nullptr, nullptr, &si, &pi))
  {
    return {nullptr, nullptr};
  }
  return {pi.hProcess, pi.hThread};
}

void incidentReport() {
  std::wcerr << L"Token request denied; this incident will be reported.\n";
  // TODO: Send email to police.
}

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("stdo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("stdo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  STDO_SCOPEEXIT { spdlog::drop_all(); };

  // if (argc < 2) {
  //   std::wcerr << L"Usage: stdo <program> <args>\n";
  //   return ClientExitInvalidUsage;
  // }

  ClientConnection conn{PipeFullPath};
  if (!conn) {
    log::error("Token server connection failed.");
    return ClientExitServerNotFound;
  }

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD stdinMode;
  GetConsoleMode(hStdin, &stdinMode);
  DWORD newStdinMode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
                       ENABLE_ECHO_INPUT | ENABLE_EXTENDED_FLAGS |
                       ENABLE_QUICK_EDIT_MODE;
  SetConsoleMode(hStdin, newStdinMode);
  STDO_SCOPEEXIT { SetConsoleMode(hStdin, stdinMode); };
  
  std::wstring username{};
  ULONG usernameLength = 0;
  GetUserNameExW(NameSamCompatible, nullptr, &usernameLength);
  if (GetLastError() == ERROR_MORE_DATA) {
    username.resize(usernameLength);
    if (!GetUserNameExW(NameSamCompatible, username.data(), &usernameLength)) {
      std::wcerr << L"Can't get username.\n";
      return ClientExitSystemError;
    }
    // It ends in a null that we don't want printed.
    username.erase(username.cend() - 1);
  } else {
    std::wcerr << L"Can't get username.\n";
    return ClientExitSystemError;
  }

  std::wstring password{};
  std::wcout << L"[stdo] password for " << username << ": ";
  std::wcout.flush();
  {
    SetConsoleMode(hStdin, ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE);
    while (true) {
      wchar_t ch;
      DWORD chRead;
      if (!ReadConsoleW(hStdin, &ch, 1, &chRead, nullptr)) {
        return ClientExitSystemError;
      }
      if (ch == 13 || ch == 10) {
        // Enter
        std::wcout << std::endl;
        break;
      } else if (ch == 8 || ch == 0x7F) {
        // Backspace
        if (password.length() > 0) {
          password.erase(password.cend() - 1);
        }
      } else if (ch == 3) {
        // Ctrl-C
        std::wcout << L"\nCanceled.";
        return ClientExitUserCanceled;
      } else {
        password.push_back((wchar_t)ch);
      }
    }
    SetConsoleMode(hStdin, newStdinMode);
    std::wcin.sync();
  }

  username.push_back(0);
  username.append(password);
  if (!conn.negotiate(username.data(), username.length())) {
    log::trace("Negotiate failed.");
    incidentReport();
    return ClientExitAccessDenied;
  }

  auto [process, thread] = createProcess(argc - 1, argv + 1);
  if (!process) {
    std::wcerr << "Error creating process.\n";
    return ClientExitCreateProcessError;
  }

  if (!conn.bless(process)) {
    log::trace("Bless failed.");
    incidentReport();
    TerminateProcess(process, 1);
    CloseHandle(thread);
    CloseHandle(process);
    return ClientExitAccessDenied;
  }

  ResumeThread(thread);
  CloseHandle(thread);
  WaitForSingleObject(process, INFINITE);
  DWORD exitCode;
  GetExitCodeProcess(process, &exitCode);
  CloseHandle(process);

  return (int)exitCode;
}

