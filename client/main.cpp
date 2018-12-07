#include "wsudo/client.h"

#include <spdlog/sinks/stdout_color_sinks.h>
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

using namespace wsudo;

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

bool ClientConnection::negotiate(const char *credentials, size_t length) {
  DWORD bytes;
  size_t messageLength = length + 4;
  _buffer.resize(messageLength);
  assert(strlen(msg::client::Credential) == 4);
  std::memcpy(_buffer.data(), msg::client::Credential, 4);
  std::memcpy(_buffer.data() + 4, credentials, length);
  log::trace("Writing credential message, size {}", messageLength);
  if (
    !WriteFile(_pipe, _buffer.data(), (DWORD)messageLength, &bytes, nullptr) ||
    bytes != messageLength
  )
  {
    log::error("Couldn't write negotiate message.");
    return false;
  }

  _buffer.resize(PipeBufferSize);
  if (!ReadFile(_pipe, _buffer.data(), PipeBufferSize, &bytes, nullptr)) {
    log::error("Couldn't read server response.");
    return false;
  }
  _buffer.resize(bytes);
  return readServerMessage();
}

bool ClientConnection::bless(HANDLE process) {
  DWORD bytes;
  size_t messageLength = 4 + sizeof(HANDLE);
  _buffer.resize(messageLength);
  assert(strlen(msg::client::Bless) == 4);
  std::memcpy(_buffer.data(), msg::client::Bless, 4);
  std::memcpy(_buffer.data() + 4, &process, sizeof(HANDLE));
  log::trace("Writing bless message, size {}", messageLength);
  if (
    !WriteFile(_pipe, _buffer.data(), (DWORD)messageLength, &bytes, nullptr) ||
    bytes != messageLength
  )
  {
    log::error("Couldn't write bless message.");
    return false;
  }

  _buffer.resize(PipeBufferSize);
  if (!ReadFile(_pipe, _buffer.data(), PipeBufferSize, &bytes, nullptr)) {
    log::error("Couldn't read server response.");
    return false;
  }
  _buffer.resize(bytes);
  return readServerMessage();
}

bool ClientConnection::readServerMessage() {
  if (_buffer.size() < 4) {
    std::wcerr << L"Unknown server response.\n";
    return false;
  }
  char header[5];
  std::memcpy(header, _buffer.data(), 4);
  header[4] = 0;
  log::trace("Reading response with code {}.", header);
  if (!std::memcmp(header, msg::server::Success, 4)) {
    // Don't print a success message - just start the process.
    return true;
  }
  if (!std::memcmp(header, msg::server::InvalidMessage, 4)) {
    std::wcerr << L"Invalid message.";
  } else if (!std::memcmp(header, msg::server::InternalError, 4)) {
    std::wcerr << L"Internal server error.";
  } else if (!std::memcmp(header, msg::server::AccessDenied, 4)) {
    std::wcerr << L"Access denied; this incident will be reported.";
    // TODO: Send email to police.
  }
  if (_buffer.size() > 4) {
    _buffer.push_back(0);
    std::cout << ": " << (_buffer.data() + 4) << "\n";
  } else {
    std::wcout << L".\n";
  }
  return false;
}

// FIXME: This is a hack and doesn't actually handle certain cases.
std::wstring fullCommandLine(int argc, wchar_t *argv[]) {
  std::wstring cl;
  for (int i = 0; i < argc; ++i) {
    if (wcschr(argv[i], L' ')) {
      cl.push_back(L'"');
      cl.append(argv[i]);
      cl.push_back(L'"');
    } else {
      cl.append(argv[i]);
    }
    cl.push_back(L' ');
  }

  return cl;
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
  std::wstring commandLine{fullCommandLine(argc, argv)};
  *(commandLine.end() - 1) = 0;
  if (!CreateProcessW(argv[0], commandLine.data(), nullptr, nullptr, true,
                      CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                      nullptr, nullptr, &si, &pi))
  {
    return {nullptr, nullptr};
  }
  return {pi.hProcess, pi.hThread};
}

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  WSUDO_SCOPEEXIT { spdlog::drop_all(); };

  if (argc < 2) {
    std::wcerr << L"Usage: wsudo <program> <args>\n";
    return ClientExitInvalidUsage;
  }

  ClientConnection conn{PipeFullPath};
  if (!conn) {
    std::wcerr << L"Connection to server failed.\n";
    return ClientExitServerNotFound;
  }

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD stdinMode;
  GetConsoleMode(hStdin, &stdinMode);
  DWORD newStdinMode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
                       ENABLE_ECHO_INPUT | ENABLE_EXTENDED_FLAGS |
                       ENABLE_QUICK_EDIT_MODE;
  SetConsoleMode(hStdin, newStdinMode);
  WSUDO_SCOPEEXIT { SetConsoleMode(hStdin, stdinMode); };

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
  std::wcout << L"[wsudo] password for " << username << ": ";
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

  auto u8creds = to_utf8(username);
  u8creds.push_back(0);
  u8creds.append(to_utf8(password));
  if (!conn.negotiate(u8creds.data(), u8creds.length())) {
    return ClientExitAccessDenied;
  }

  auto [process, thread] = createProcess(argc - 1, argv + 1);
  if (!process) {
    std::wcerr << "Error creating process.\n";
    return ClientExitCreateProcessError;
  }

  if (!conn.bless(process)) {
    std::wcerr << L"Server failed to adjust privileges\n";
    TerminateProcess(process, 1);
    CloseHandle(thread);
    CloseHandle(process);
    return ClientExitSystemError;
  }

  ResumeThread(thread);
  CloseHandle(thread);
  WaitForSingleObject(process, INFINITE);
  DWORD exitCode;
  GetExitCodeProcess(process, &exitCode);
  CloseHandle(process);

  return (int)exitCode;
}

