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
    log::trace("IOCP waiting on completion port.");
    if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                   reinterpret_cast<PULONG_PTR>(&key),
                                   &overlapped, INFINITE)) {
      auto err = GetLastError();
      if (!overlapped) {
        if (err == ERROR_ABANDONED_WAIT_0) {
          // Completion port closed.
          log::error("IOCP port closed unexpectedly.");
          return;
        } else {
          // No packet was dequeued.
          log::warn("IOCP missing packet, err=0x{:X} {}", err,
                    lastErrorString(err));
          continue;
        }
      } else {
        switch (err) {
          case ERROR_HANDLE_EOF:
            log::trace("IOCP Dequeued EOF.");
            break;

          case ERROR_MORE_DATA:
            log::trace("IOCP more data pending.");
            break;

          default:
            // Other IO error.
            log::error("IOCP err=0x{:X} {}", err, lastErrorString(err));
            return;
        }
      }
    } else if (overlapped == _quitFlag) {
      log::debug("IOCP quit message, exitCode={}.", bytes);
      return;
    }

    auto coroutine = key->coroutine;
    key->bytesTransferred = bytes;
    log::debug("IOCP {} bytes transferred; resuming coroutine.", bytes);
    try {
      coroutine.resume();
    } catch (std::exception &e) {
      log::error("IOCP exception in coroutine: {}", e.what());
    }
  }
}

CorIO::AsyncFile::AsyncFile(CorIO &corio, wil::unique_hfile file)
  : _file{std::move(file)}, _overlapped{}, _key{}
{
  corio.registerFile(*this);
}

wscoro::Task<bool>
CorIO::AsyncFile::readToEnd(std::string &buffer) noexcept {
  const size_t chunkSize = 8;
  char chunk[chunkSize];
  auto this_coro = co_await wscoro::this_coroutine;

  while (true) {
    _key.coroutine = this_coro;
    prepareOverlapped();
    log::trace("CorIO begin read.");
    ReadFile(_file.get(), chunk, chunkSize, nullptr, &_overlapped);
    co_await std::suspend_always{};

    DWORD bytes;
    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      log::trace("CorIO read finished with {} bytes.", bytes);
      buffer.append(chunk, chunk + bytes);
      co_return true;
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      buffer.append(chunk, chunk + bytes);
      addOffset((size_t)bytes);
      continue;
    } else if (err == ERROR_BROKEN_PIPE) {
      log::warn("CorIO pipe disconnected by client.");
      co_return false;
    }

    log::error("CorIO GetOverlappedResult failed: 0x{:X} {}", err,
                lastErrorString(err));
    co_return false;
  }
}

wscoro::Task<bool>
CorIO::AsyncFile::write(std::span<const char> buffer) noexcept {
  auto this_coro = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  while (bufferOffset < buffer.size()) {
    auto remaining = buffer.size() - bufferOffset;
    assert(remaining < (size_t)std::numeric_limits<DWORD>::max());

    _key.coroutine = this_coro;
    prepareOverlapped();
    log::trace("CorIO begin write.");
    WriteFile(_file.get(), buffer.data() + bufferOffset,
              static_cast<DWORD>(remaining), nullptr, &_overlapped);
    co_await std::suspend_always{};

    DWORD bytes;
    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      log::trace("CorIO write finished with {} bytes.", bytes);
      co_return true;
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      bufferOffset += bytes;
      addOffset((size_t)bytes);
      continue;
    } else if (err == ERROR_BROKEN_PIPE) {
      log::warn("CorIO pipe disconnected by client.");
      co_return false;
    }

    log::error("CorIO GetOverlappedResult failed: 0x{:X} {}", err,
                lastErrorString(err));
    co_return false;
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
  log::trace("CorIO created IO completion port.");
  THROW_LAST_ERROR_IF_NULL(_ioCompletionPort.get());
}

CorIO::~CorIO() {
  log::trace("CorIO finish.");
  wait();
}

void CorIO::run(int nUserThreads) {
  auto runListener = [this] () { this->listener(); };
  if (nUserThreads <= 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    nUserThreads = static_cast<int>(info.dwNumberOfProcessors);
  }
  log::debug("CorIO starting {} threads.", nUserThreads);
  _threads.reserve(nUserThreads);
  for (int i = 0; i < nUserThreads; ++i) {
    _threads.emplace_back(std::thread{runListener});
  }
}

int CorIO::wait() {
  if (!_threads.size()) {
    return -1;
  }

  log::debug("CorIO wait for {} threads.", _threads.size());
  for (auto &thread : _threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  log::trace("CorIO wait finished.");

  _threads.clear();

  return 0;
}

wscoro::Task<> CorIO::enterIOThread() {
  log::trace("CorIO will enter IO thread.");
  CompletionKey key{co_await wscoro::this_coroutine};
  if(!PostQueuedCompletionStatus(_ioCompletionPort.get(), 0,
                                 reinterpret_cast<ULONG_PTR>(&key), nullptr)) {
    auto err = GetLastError();
    log::error("CorIO PostQueuedCompletionStatus failed: 0x{:X} {}", err,
               lastErrorString(err));
    co_return;
  }
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
