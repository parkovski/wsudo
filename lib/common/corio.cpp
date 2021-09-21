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
    DWORD err = ERROR_SUCCESS;
    if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                   reinterpret_cast<PULONG_PTR>(&key),
                                   &overlapped, INFINITE)) {
      err = GetLastError();
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
            break;
        }
      }
    } else if (overlapped == _quitFlag) {
      log::debug("IOCP quit message, exitCode={}.", bytes);
      return;
    }

    auto coroutine = key->coroutine;
    key->result = err;
    log::debug("IOCP {} bytes transferred; resuming coroutine.", bytes);
    try {
      coroutine.resume();
    } catch (std::exception &e) {
      log::error("IOCP exception in coroutine: {}", e.what());
    }
  }
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

void CorIO::postQuitMessage(int exitCode) {
  for (int i = 0; i < _threads.size(); ++i) {
    PostQueuedCompletionStatus(_ioCompletionPort.get(), exitCode, 0,
                               _quitFlag);
  }
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

void CorIO::registerFile(FileBase &file) {
  THROW_LAST_ERROR_IF_NULL(
    CreateIoCompletionPort(file._file.get(), _ioCompletionPort.get(),
                           reinterpret_cast<ULONG_PTR>(&file._key), 0)
  );
  log::trace("CorIO registered file.");
}

// ===== FileBase =====

CorIO::FileBase::FileBase(CorIO &corio, wil::unique_hfile file)
  : _file{std::move(file)}
{
  corio.registerFile(*this);
}

wscoro::Task<size_t> CorIO::FileBase::write(std::string_view buffer) {
  auto this_coro = co_await wscoro::this_coroutine;

  size_t bufferOffset = 0;
  log::trace("CorIO::FileBase::write {} bytes.", buffer.size());
  while (true) {
    auto remaining = buffer.size() - bufferOffset;
    auto bytes =
      remaining <= (size_t)std::numeric_limits<DWORD>::max()
      ? static_cast<DWORD>(remaining)
      : std::numeric_limits<DWORD>::max();

    _key.coroutine = this_coro;
    prepareOverlapped();
    setOffset(0xFFFFFFFF'FFFFFFFF); // Write to the end of the file.
    log::trace("CorIO::FileBase::write queue async IO for {} bytes.", bytes);
    WriteFile(_file.get(), buffer.data() + bufferOffset, bytes, nullptr,
              &_overlapped);
    co_await std::suspend_always{};

    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      bufferOffset += bytes;
      log::trace("CorIO write {} bytes ({} total).", bytes, bufferOffset);
      if (bufferOffset == buffer.size()) {
        co_return bufferOffset;
      } else {
        // Not all the buffer has been written.
        continue;
      }
    }

    auto err = GetLastError();
    switch (err) {
      case ERROR_OPERATION_ABORTED:
        // IO canceled.
        log::warn("CorIO::FileBase::write operation aborted.");
        co_return bufferOffset;

      case ERROR_BROKEN_PIPE:
        // TODO: Can WriteFile return this error?
        log::warn("CorIO::FileBase::write pipe disconnected by client.");
        co_return bufferOffset;

      case ERROR_INVALID_USER_BUFFER:
      case ERROR_NOT_ENOUGH_MEMORY:
        // Possibly too many outstanding async IO requests.
      case ERROR_NOT_ENOUGH_QUOTA:
        // Page lock error, see SetProcessWorkingSetSize.
        [[fallthrough]];

      default:
        log::error("CorIO::FileBase::write failed: 0x{:X} {}", err,
                   lastErrorString(err));
        THROW_WIN32(err);
    }
  }
}

// ===== File =====

size_t CorIO::File::size() const {
  LARGE_INTEGER size;
  if (GetFileSizeEx(_file.get(), &size)) {
    return static_cast<size_t>(size.QuadPart);
  }
  THROW_LAST_ERROR();
}

wscoro::Task<size_t> CorIO::File::read(std::string &buffer, size_t maxBytes) {
  const DWORD chunkSize = 32768;
  char chunk[chunkSize];
  auto this_coro = co_await wscoro::this_coroutine;

  if (maxBytes) {
    log::trace("CorIO::File::read <= {} bytes.", maxBytes);
  } else {
    log::trace("CorIO::File::read to end.");
  }
  setOffset(0);
  while (true) {
    _key.coroutine = this_coro;
    prepareOverlapped();
    DWORD bytes = chunkSize;
    if (maxBytes && maxBytes - offset() < static_cast<size_t>(chunkSize)) {
      bytes = static_cast<DWORD>(maxBytes - offset());
    }
    log::trace("CorIO::File::read queue async IO for {} bytes.", bytes);
    ReadFile(_file.get(), chunk, bytes, nullptr, &_overlapped);
    // Test for sync EOF here?
    co_await std::suspend_always{};

    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      addOffset((size_t)bytes);
      log::trace("CorIO::File::read {} bytes ({} total).", bytes, offset());
      buffer.append(chunk, chunk + bytes);
      if (maxBytes) {
        if (offset() < maxBytes) {
          continue;
        } else if (offset() == maxBytes) {
          co_return maxBytes;
        } else {
          WSUDO_UNREACHABLE("Read more than expected");
        }
      } else {
        continue;
      }
    }

    auto err = GetLastError();
    if (err == ERROR_HANDLE_EOF) {
      if (bytes) {
        buffer.append(chunk, chunk + bytes);
        addOffset((size_t)bytes);
      }
      log::trace("CorIO::File::read {} bytes ({} total); saw EOF.", bytes,
                 offset());
      co_return offset();
    }

    log::error("CorIO::File::read failed: 0x{:X} {}", err,
               lastErrorString(err));
    THROW_WIN32(err);
  }
}

// ===== Pipe =====

DWORD CorIO::Pipe::clientProcessId() const noexcept {
  ULONG pid;
  if (GetNamedPipeClientProcessId(_file.get(), &pid)) {
    return (DWORD)pid;
  }
  return (DWORD)(-1);
}

wscoro::Task<size_t> CorIO::Pipe::read(std::string &buffer) {
  const size_t chunkSize = 256;
  char chunk[chunkSize];
  auto this_coro = co_await wscoro::this_coroutine;

  setOffset(0);
  while (true) {
    _key.coroutine = this_coro;
    prepareOverlapped();
    log::trace("CorIO::Pipe::read queue async IO for {} bytes.", chunkSize);
    ReadFile(_file.get(), chunk, chunkSize, nullptr, &_overlapped);
    co_await std::suspend_always{};

    DWORD bytes;
    if (GetOverlappedResult(_file.get(), &_overlapped, &bytes, false)) {
      addOffset((size_t)bytes);
      buffer.append(chunk, chunk + bytes);
      log::trace("CorIO::Pipe::read {} bytes ({} total); finished.", bytes,
                 offset());
      co_return offset();
    }

    auto err = GetLastError();
    if (err == ERROR_MORE_DATA) {
      buffer.append(chunk, chunk + bytes);
      addOffset((size_t)bytes);
      log::trace("CorIO::Pipe::read {} bytes ({} total); more data available.",
                 bytes, offset());
      continue;
    } else if (err == ERROR_BROKEN_PIPE) {
      log::warn("CorIO::Pipe::read pipe disconnected by client.");
      co_return 0;
    }

    log::error("CorIO::Pipe::read failed: 0x{:X} {}", err,
                lastErrorString(err));
    THROW_WIN32(err);
  }
}

CorIO::File CorIO::openForReading(const wchar_t *path, DWORD flags) {
  HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, flags | FILE_FLAG_OVERLAPPED,
                           nullptr);
  return CorIO::File{
    *this,
    wil::unique_hfile{file}
  };
}

CorIO::File CorIO::openForWriting(const wchar_t *path, DWORD flags) {
  HANDLE file = CreateFile(path, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           flags | FILE_FLAG_OVERLAPPED, nullptr);
  return CorIO::File{
    *this,
    wil::unique_hfile{file}
  };
}
