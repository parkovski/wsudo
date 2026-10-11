#include "wsudo/wsudo.h"
#include "wsudo/client.h"

using namespace wsudo;

Client::Connection::Connection(const std::wstring &pipeName, int maxAttempts)
  : _pipeName{pipeName}
{
  assert(!pipeName.empty());
  assert(maxAttempts > 0);

  connect(maxAttempts);
}

void Client::Connection::connect(int maxAttempts) {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = false;
  sa.lpSecurityDescriptor = nullptr;

  const wchar_t *const cname = _pipeName.c_str();
  int attempts = 0;
  while (++attempts <= maxAttempts) {
    HANDLE pipe = CreateFile(cname, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      _pipe = wil::unique_hfile{pipe};
      log::trace("Connected to remote pipe.");
      return;
    }
    auto err = GetLastError();
    if (err == ERROR_PIPE_BUSY) {
      WaitNamedPipe(cname, NMPWAIT_USE_DEFAULT_WAIT);
    } else {
      THROW_WIN32(err);
    }
  }
  THROW_WIN32(ERROR_PIPE_BUSY);
}

void Client::Connection::sendBuffer() {
  DWORD bytes;
  DWORD totalWritten = 0;
  while (totalWritten < _buffer.length()) {
    if (!WriteFile(_pipe.get(), _buffer.data() + totalWritten,
                   (DWORD)_buffer.length() - totalWritten, &bytes, nullptr)) {
      auto err = GetLastError();
      switch (err) {
      case ERROR_PIPE_NOT_CONNECTED:
      case ERROR_BROKEN_PIPE:
        connect();
        totalWritten = 0;
        continue;

      default:
        THROW_WIN32(err);
      }
    }
    totalWritten += bytes;
  }
  emptyBuffer();
}

void Client::Connection::recvBuffer() {
  constexpr DWORD chunkSize = 256;
  char chunk[chunkSize];

  DWORD bytes;
  emptyBuffer();
  while (true) {
    log::trace("Waiting on ReadFile");
    if (ReadFile(_pipe.get(), chunk, chunkSize, &bytes, nullptr)) {
      _buffer.append(chunk, chunk + bytes);
      break;
    }

    auto err = GetLastError();
    log::trace("err=0x{:X} {} buf={}", (unsigned)err, lastErrorString(err), _buffer);
    switch (err) {
    case ERROR_MORE_DATA:
      _buffer.append(chunk, chunk + bytes);
      continue;

    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_BROKEN_PIPE:
      connect();
      emptyBuffer();
      return;

    default:
      THROW_WIN32(err);
    }
  }
}
