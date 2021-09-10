#pragma once

#include "wscoro/basictasks.h"

#define NOMINMAX
#include <wil/resource.h>

#include <span>
#include <vector>
#include <cassert>
#include <thread>

namespace wsudo {

class CorIO {
  constexpr static LPOVERLAPPED _quitFlag = (LPOVERLAPPED)(size_t)(-1);
  wil::unique_handle _ioCompletionPort;
  std::vector<std::thread> _threads;

  void listener() noexcept;

public:
  struct CompletionKey {
    union {
      /// In - coroutine handle to resume when IO completes.
      std::coroutine_handle<> coroutine;
      /// Out - number of bytes transferred.
      DWORD bytesTransferred;
    };

    CompletionKey() noexcept
      : coroutine{}
    {}

    CompletionKey(std::coroutine_handle<> coroutine) noexcept
      : coroutine{coroutine}
    {}
  };

  class AsyncFile {
    friend class CorIO;

  protected:
    CompletionKey _key;
    wil::unique_hfile _file;
    OVERLAPPED _overlapped;

    void prepareOverlapped() noexcept {
      _overlapped.Internal = 0;
      _overlapped.InternalHigh = 0;
      _overlapped.hEvent = nullptr;
    }

  public:
    AsyncFile(CorIO &corio, wil::unique_hfile file);

    size_t offset() const noexcept {
      return reinterpret_cast<size_t>(_overlapped.Pointer);
    }

    size_t setOffset(size_t offset) noexcept {
      _overlapped.Pointer = reinterpret_cast<void *>(offset);
      return offset;
    }

    size_t addOffset(size_t offset) noexcept {
      // Check overflow.
      assert(offset <= std::numeric_limits<size_t>::max() - this->offset());
      return setOffset(this->offset() + offset);
    }

    size_t addOffset(ptrdiff_t offset) noexcept {
      if (offset < 0) {
        // Check overflow.
        assert(static_cast<size_t>(-offset) <= this->offset());
        return setOffset(this->offset() - static_cast<size_t>(-offset));
      } else {
        return addOffset(static_cast<size_t>(offset));
      }
    }

    /// Write the contents of `buffer` to the file.
    /// \return True if the file is still good, false if the handle is no longer
    /// valid (disconnected pipe).
    /// \throws wil::ResultException
    wscoro::Task<bool> write(std::span<const char> buffer);

    /// Read the entire contents of the file into `buffer`.
    /// \return True if the file is still good, false if the handle is no longer
    /// valid (disconnected pipe).
    /// \throws wil::ResultException
    wscoro::Task<bool> readToEnd(std::string &buffer);

    wscoro::Task<std::string> readToEnd() {
      std::string buffer;
      co_await readToEnd(buffer);
      co_return buffer;
    }
  };

private:
  void registerFile(AsyncFile &file);
  wscoro::Task<> enterIOThread();

public:
  explicit CorIO(int nSystemThreads = 0);
  ~CorIO();

  CorIO(const CorIO &) = delete;
  CorIO &operator=(const CorIO &) = delete;

  CorIO(CorIO &&) = default;
  CorIO &operator=(CorIO &&) = default;

  void run(int nUserThreads = 0);
  int wait();

  /// Run `f` asynchronously on an IO thread.
  template<class F>
  wscoro::FireAndForget postCallback(F f) {
    co_await enterIOThread();
    f();
  }

  /// Run `c` asynchronously on an IO thread.
  template<class C>
  wscoro::FireAndForget postCoroutine(C c) {
    co_await enterIOThread();
    co_await c;
  }

  /// Notify all threads to quit.
  void postQuitMessage(int exitCode);

  // Moves ownership of fileHandle into the returned AsyncFile. The file is
  // registered with the associated IO completion port. fileHandle must have
  // been opened with FILE_FLAG_OVERLAPPED.
  // TFile should be a subclass of AsyncFile.
  template<class TFile = AsyncFile, class ...Args>
  TFile make(HANDLE fileHandle, Args &&...args) {
    return TFile{*this, wil::unique_hfile{fileHandle},
                 std::forward<Args>(args)...};
  }

  // Opens a file with GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  AsyncFile openForReading(const wchar_t *path, DWORD flags = 0);

  // Opens a file with GENERIC_WRITE, no sharing, OPEN_ALWAYS, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  AsyncFile openForWriting(const wchar_t *path, DWORD flags = 0);
};

} // namespace wsudo
