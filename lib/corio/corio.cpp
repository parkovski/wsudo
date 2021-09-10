#include "wsudo/wsudo.h"
#include "wsudo/corio.h"
#include <wil/result.h>

#include <thread>
#include <vector>

using namespace wsudo;

void CorIO::listener() noexcept {
  DWORD bytes;
  LPOVERLAPPED overlapped;
  CompletionKey *key;

  while (true) {
    log::trace("Waiting on completion port.");
    if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                   reinterpret_cast<PULONG_PTR>(&key),
                                   &overlapped, INFINITE)) {
      if (!overlapped) {
        if (GetLastError() == ERROR_ABANDONED_WAIT_0) {
          log::debug("Port closed.");
          // Completion port closed.
          return;
        } else {
          // No packet was dequeued.
          log::debug("No overlapped, err={}.", lastErrorString());
          continue;
        }
      } else {
        switch (GetLastError()) {
          case ERROR_SUCCESS:
            log::debug("Success.");
            break;

          case ERROR_HANDLE_EOF:
            log::debug("EOF.");
            // IO done.
            break;

          default:
            log::debug("Overlapped, err={}.", lastErrorString());
            // Other IO error.
            break;
        }
      }
    } else if (overlapped == _quitFlag) {
      log::debug("Quit message ec={}.", bytes);
      return;
    }

    auto coroutine = key->coroutine;
    key->bytesTransferred = bytes;
    log::debug("{} bytes transferred; resuming coroutine.", bytes);
    try {
      coroutine.resume();
    } catch (std::exception &e) {
      log::error("Exception in IO thread: {}", e.what());
    }
  }
}

CorIO::AsyncFile::AsyncFile(CorIO &corio, wil::unique_hfile file)
  : _file{std::move(file)}, _overlapped{}, _key{}
{
  corio.registerFile(*this);
}

wscoro::Task<bool>
CorIO::AsyncFile::readToEnd(std::string &buffer) {
  const size_t chunkSize = 8;
  char chunk[chunkSize];
  auto this_coro = co_await wscoro::this_coroutine;

  while (true) {
    _key.coroutine = this_coro;
    prepareOverlapped();
    ReadFile(_file.get(), chunk, chunkSize, nullptr, &_overlapped);
    co_await std::suspend_always{};

    DWORD bytes;
    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      log::trace("Read finished with {} bytes.", bytes);
      buffer.append(chunk, chunk + bytes);
      co_return true;
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      buffer.append(chunk, chunk + bytes);
      addOffset((size_t)bytes);
      continue;
    } else if (err == ERROR_BROKEN_PIPE) {
      log::warn("Pipe disconnected by client.");
      co_return false;
    }

    log::error("GetOverlappedResult failed: 0x{:X} {}", err,
                lastErrorString(err));
    THROW_WIN32(err);
  }
}

wscoro::Task<bool>
CorIO::AsyncFile::write(std::span<const char> buffer) {
  auto this_coro = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    assert(remaining < (size_t)std::numeric_limits<DWORD>::max());

    _key.coroutine = this_coro;
    prepareOverlapped();
    WriteFile(_file.get(), buffer.data() + bufferOffset,
              static_cast<DWORD>(remaining), nullptr, &_overlapped);
    co_await std::suspend_always{};

    DWORD bytes;
    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      log::trace("Write finished with {} bytes.", bytes);
      co_return true;
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      bufferOffset += bytes;
      addOffset((size_t)bytes);
      continue;
    } else if (err == ERROR_BROKEN_PIPE) {
      log::warn("Pipe disconnected by client.");
      co_return false;
    }

    log::error("GetOverlappedResult failed: 0x{:X} {}", err,
                lastErrorString(err));
    THROW_WIN32(err);
  }
}

void CorIO::registerFile(AsyncFile &file) {
  THROW_LAST_ERROR_IF_NULL(
    CreateIoCompletionPort(file._file.get(), _ioCompletionPort.get(),
                           reinterpret_cast<ULONG_PTR>(&file._key), 0)
  );
  log::trace("CorIO registered file.");
}

CorIO::CorIO(int nSystemThreads)
  : _ioCompletionPort{
      CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0,
                             static_cast<DWORD>(nSystemThreads))
    }
{
  THROW_LAST_ERROR_IF_NULL(_ioCompletionPort.get());
}

CorIO::~CorIO() {
  wait();
}

void CorIO::run(int nUserThreads) {
  auto runListener = [this] () { this->listener(); };
  if (nUserThreads <= 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    nUserThreads = static_cast<int>(info.dwNumberOfProcessors);
  }
  _threads.reserve(nUserThreads);
  for (int i = 0; i < nUserThreads; ++i) {
    _threads.emplace_back(std::thread{runListener});
  }
}

int CorIO::wait() {
  for (auto &thread : _threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  return 0;
}

wscoro::Task<> CorIO::enterIOThread() {
  log::trace("CorIO will enter IO thread.");
  CompletionKey key{co_await wscoro::this_coroutine};
  THROW_LAST_ERROR_IF(
    !PostQueuedCompletionStatus(_ioCompletionPort.get(), 0,
                                reinterpret_cast<ULONG_PTR>(&key), nullptr)
  );
  co_await std::suspend_always{};
  log::trace("CorIO entered IO thread.");
}

void CorIO::postQuitMessage(int exitCode) {
  for (int i = 0; i < _threads.size(); ++i) {
    PostQueuedCompletionStatus(_ioCompletionPort.get(), exitCode, 0,
                               _quitFlag);
  }
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
