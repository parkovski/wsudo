#include "wsudo/corio.h"
#include <wil/win32_helpers.h>

#include <thread>

using namespace wsudo;

void CorIO::listener() noexcept {
  DWORD bytes;
  LPOVERLAPPED overlapped;
  ULONG_PTR completionKey;

  while (true) {
    if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                   &completionKey, &overlapped, 0)) {
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

    auto token = reinterpret_cast<FileToken *>(completionKey);
    token->addOffset(static_cast<size_t>(bytes));
    token->_coroutine.resume();
  }
}

CorIO::FileToken::FileToken(CorIO &corio, HANDLE file)
  : _file{file}, _overlapped{}, _coroutine{}
{
  corio.registerToken(*this);
}

wscoro::Task<size_t>
CorIO::FileToken::read(std::span<std::byte> buffer) {
  const size_t chunkSize = 1024;

  _coroutine = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    auto readSize = remaining > chunkSize ?  chunkSize : remaining;
    auto prevOffset = offset();

    _overlapped.Internal = 0;
    _overlapped.InternalHigh = 0;
    _overlapped.hEvent = nullptr;
    ReadFile(_file, buffer.data() + bufferOffset, static_cast<DWORD>(readSize),
             nullptr, &_overlapped);

    co_await std::suspend_always{};

    readSize = offset() - prevOffset;
    if (readSize == 0) {
      break;
    }
    bufferOffset += readSize;
  }

  co_return bufferOffset;
}

wscoro::Task<std::vector<std::byte>>
CorIO::FileToken::readToEnd() {
  const size_t chunkSize = 1024;

  LARGE_INTEGER li;
  THROW_LAST_ERROR_IF(!GetFileSizeEx(_file, &li));
  auto size = static_cast<size_t>(li.QuadPart);
  auto buffer = std::vector<std::byte>(size);
  setOffset(0);
  co_await read(std::span{&buffer[0], size});

  co_return buffer;
}

wscoro::Task<size_t>
CorIO::FileToken::write(std::span<const std::byte> buffer) {
  const size_t chunkSize = 1024;

  _overlapped.Internal = 0;
  _overlapped.InternalHigh = 0;
  _overlapped.hEvent = nullptr;
  _coroutine = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    auto writeSize = remaining > chunkSize ?  chunkSize : remaining;
    auto prevOffset = offset();

    _overlapped.Internal = 0;
    _overlapped.InternalHigh = 0;
    _overlapped.hEvent = nullptr;
    WriteFile(_file, buffer.data() + bufferOffset,
              static_cast<DWORD>(writeSize), nullptr, &_overlapped);

    co_await std::suspend_always{};

    writeSize = offset() - prevOffset;
    THROW_LAST_ERROR_IF(writeSize == 0);
    bufferOffset += writeSize;
  }

  co_return bufferOffset;
}

void CorIO::registerToken(FileToken &token) {
  THROW_LAST_ERROR_IF_NULL(
    CreateIoCompletionPort(token._file, _ioCompletionPort.get(),
                           reinterpret_cast<ULONG_PTR>(&token), 0)
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
