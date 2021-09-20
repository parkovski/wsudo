#include "wsudo/client.h"

using namespace wsudo;

Client::Connection::Connection(const std::wstring &pipeName, int maxAttempts) {
  assert(!pipeName.empty());
  assert(maxAttempts > 0);

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = false;
  sa.lpSecurityDescriptor = nullptr;

  const wchar_t *const cname = pipeName.c_str();
  int attempts = 0;
  while (++attempts <= maxAttempts) {
    HANDLE pipe = CreateFile(cname, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      _pipe = wil::unique_hfile{pipe};
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
  DWORD bytes = (DWORD)_buffer.length();
  if (!WriteFile(_pipe.get(), _buffer.data(), bytes, &bytes, nullptr)) {
    log::error("WriteFile failed.");
    THROW_LAST_ERROR();
  }
  emptyBuffer();
}

void Client::Connection::recvBuffer() {
  constexpr DWORD chunkSize = 256;
  char chunk[chunkSize];

  DWORD bytes;
  emptyBuffer();
  while (true) {
    if (ReadFile(_pipe.get(), chunk, chunkSize, &bytes, nullptr)) {
      _buffer.append(chunk, chunk + bytes);
      break;
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      _buffer.append(chunk, chunk + bytes);
      continue;
    }

    log::error("ReadFile failed.");
    THROW_WIN32(err);
  }
}
