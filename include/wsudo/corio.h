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

private:
  wscoro::Task<> enterIOThread();

  class FileBase;
  void registerFile(FileBase &file);

public:
  class FileBase {
  protected:
    friend class CorIO;

    CompletionKey _key;
    wil::unique_hfile _file;
    OVERLAPPED _overlapped;

    void prepareOverlapped() noexcept {
      _overlapped.Internal = 0;
      _overlapped.InternalHigh = 0;
      _overlapped.hEvent = nullptr;
    }

    explicit FileBase(CorIO &corio, wil::unique_hfile file);

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

    wscoro::Task<size_t> write(std::string_view buffer);
  };

  class File : public FileBase {
  public:
    explicit File(CorIO &corio, wil::unique_hfile file)
      : FileBase(corio, std::move(file))
    {}

    size_t size() const;
    wscoro::Task<size_t> read(std::string &buffer, size_t maxBytes = 0);
  };

  class Pipe : public FileBase {
  public:
    explicit Pipe(CorIO &corio, wil::unique_hfile file)
      : FileBase(corio, std::move(file))
    {}

    wscoro::Task<size_t> read(std::string &buffer);
  };

  /// Moves ownership of fileHandle into the returned object. The file is
  /// registered with the associated IO completion port by FileBase::FileBase.
  /// \param TFile Should be a subclass of FileBase.
  /// \param fileHandle A file opened with FILE_FLAG_OVERLAPPED.
  /// \param args Any other TFile constructor arguments.
  template<class TFile, class ...Args>
  std::enable_if_t<std::is_base_of_v<FileBase, TFile>, TFile>
  make(HANDLE fileHandle, Args &&...args) {
    return TFile{*this, wil::unique_hfile{fileHandle},
                 std::forward<Args>(args)...};
  }

  // Opens a file with GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  File openForReading(const wchar_t *path, DWORD flags = 0);

  // Opens a file with GENERIC_WRITE, no sharing, OPEN_ALWAYS, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  File openForWriting(const wchar_t *path, DWORD flags = 0);
};

} // namespace wsudo
