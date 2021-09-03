#include "wsudo/corio.h"
#include <wil/win32_helpers.h>

#include <thread>

using namespace wsudo;

void CorIO::listener() noexcept {
  DWORD bytes;
  LPOVERLAPPED overlapped;
  CompletionKey *key;

  while (true) {
    if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                   reinterpret_cast<PULONG_PTR>(&key),
                                   &overlapped, 0)) {
      if (!overlapped) {
        if (GetLastError() == ERROR_ABANDONED_WAIT_0) {
          // Completion port closed.
          return;
        } else {
          // No packet was dequeued.
          continue;
        }
      } else {
        switch (GetLastError()) {
          case ERROR_HANDLE_EOF:
            // IO done.
            break;

          default:
            // Other IO error.
            break;
        }
      }
    }

    auto coroutine = key->coroutine;
    key->bytesTransferred = bytes;
    coroutine.resume();
  }
}

CorIO::AsyncFile::AsyncFile(CorIO &corio, wil::unique_hfile file)
  : _file{std::move(file)}, _overlapped{}, _key{}
{
  corio.registerFile(*this);
}

wscoro::Task<size_t>
CorIO::AsyncFile::read(std::span<char> buffer) {
  const size_t chunkSize = 1024;

  auto this_coro = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    auto readSize = remaining > chunkSize ?  chunkSize : remaining;

    _overlapped.Internal = 0;
    _overlapped.InternalHigh = 0;
    _overlapped.hEvent = nullptr;
    _key.coroutine = this_coro;
    ReadFile(_file.get(), buffer.data() + bufferOffset,
             static_cast<DWORD>(readSize), nullptr, &_overlapped);
    co_await std::suspend_always{};

    if (_key.bytesTransferred == 0) {
      break;
    }
    addOffset((size_t)_key.bytesTransferred);
    bufferOffset += _key.bytesTransferred;
  }

  co_return bufferOffset;
}

wscoro::Task<std::vector<char>>
CorIO::AsyncFile::readToEnd() {
  const size_t chunkSize = 1024;

  LARGE_INTEGER li;
  THROW_LAST_ERROR_IF(!GetFileSizeEx(_file.get(), &li));
  auto size = static_cast<size_t>(li.QuadPart);
  auto buffer = std::vector<char>(size);
  setOffset(0);
  size = co_await read(std::span{&buffer[0], size});
  if (size < buffer.size()) {
    buffer.resize(size);
  }

  co_return buffer;
}

wscoro::Task<size_t>
CorIO::AsyncFile::write(std::span<const char> buffer) {
  const size_t chunkSize = 1024;

  auto this_coro = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    auto writeSize = remaining > chunkSize ?  chunkSize : remaining;

    _overlapped.Internal = 0;
    _overlapped.InternalHigh = 0;
    _overlapped.hEvent = nullptr;
    _key.coroutine = this_coro;
    WriteFile(_file.get(), buffer.data() + bufferOffset,
              static_cast<DWORD>(writeSize), nullptr, &_overlapped);
    co_await std::suspend_always{};

    THROW_LAST_ERROR_IF(writeSize == 0);
    addOffset((size_t)_key.bytesTransferred);
    bufferOffset += _key.bytesTransferred;
  }

  co_return bufferOffset;
}

void CorIO::registerFile(AsyncFile &file) {
  THROW_LAST_ERROR_IF_NULL(
    CreateIoCompletionPort(file._file.get(), _ioCompletionPort.get(),
                           reinterpret_cast<ULONG_PTR>(&file._key), 0)
  );
}

CorIO::CorIO(int threads)
  : _ioCompletionPort{CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0,
                                             0)} {
  THROW_LAST_ERROR_IF_NULL(_ioCompletionPort.get());
  if (threads <= 0) {
    threads = 1;
  }
  for (int i = 0; i < threads; ++i) {
    std::thread{[this] () {
      this->listener();
    }}.detach();
  }
}

CorIO::~CorIO() {
}

wscoro::Task<DWORD> CorIO::postMessage(DWORD bytesTransferred) {
  CompletionKey key{co_await wscoro::this_coroutine};
  THROW_LAST_ERROR_IF(
    !PostQueuedCompletionStatus(_ioCompletionPort.get(), bytesTransferred,
                                reinterpret_cast<ULONG_PTR>(&key), nullptr)
  );
  co_await std::suspend_always{};
  co_return key.bytesTransferred;
}

CorIO::AsyncFile CorIO::openForReading(const wchar_t *path, DWORD flags) {
  HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, flags | FILE_FLAG_OVERLAPPED,
                           nullptr);
  return {
    *this,
    wil::unique_hfile{file}
  };
}

CorIO::AsyncFile CorIO::openForWriting(const wchar_t *path, DWORD flags) {
  HANDLE file = CreateFile(path, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           flags | FILE_FLAG_OVERLAPPED, nullptr);
  return {
    *this,
    wil::unique_hfile{file}
  };
}
