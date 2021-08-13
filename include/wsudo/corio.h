#pragma once

#include "wscoro/task.h"

#define NOMINMAX
#include <wil/resource.h>

#include <span>
#include <vector>
#include <cassert>

namespace wsudo {

class CorIO {
  wil::unique_handle _ioCompletionPort;

  void listener() noexcept;

public:
  class FileToken {
    friend class CorIO;

    wil::unique_hfile _file;
    OVERLAPPED _overlapped;
    std::coroutine_handle<> _coroutine;

    FileToken(CorIO &corio, wil::unique_hfile file);

  public:
    FileToken(const FileToken &) = delete;
    FileToken &operator=(const FileToken &) = delete;

    size_t offset() const noexcept {
      return reinterpret_cast<size_t>(_overlapped.Pointer);
    }

    size_t setOffset(size_t offset) noexcept {
      _overlapped.Pointer = reinterpret_cast<void *>(offset);
      return reinterpret_cast<size_t>(_overlapped.Pointer);
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

    wscoro::Task<size_t> read(std::span<char> buffer);
    wscoro::Task<std::vector<char>> readToEnd();
    wscoro::Task<size_t> write(std::span<const char> buffer);
  };

private:
  void registerToken(FileToken &token);

public:
  explicit CorIO(int threads = 0);
  ~CorIO();

  CorIO(const CorIO &) = delete;
  CorIO &operator=(const CorIO &) = delete;

  CorIO(CorIO &&) = default;
  CorIO &operator=(CorIO &&) = default;

  // Moves ownership of fileHandle into the returned token. The token is
  // registered with the associated IO completion port. fileHandle must have
  // been opened with FILE_FLAG_OVERLAPPED.
  FileToken put(wil::unique_hfile fileHandle) {
    return {*this, std::move(fileHandle)};
  }

  // Opens a file with GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  FileToken openForReading(const wchar_t *path, DWORD flags = 0);

  // Opens a file with GENERIC_WRITE, no sharing, OPEN_ALWAYS, and
  // flags | FILE_FLAG_OVERLAPPED. For other configurations use CreateFile
  // directly.
  FileToken openForWriting(const wchar_t *path, DWORD flags = 0);
};

} // namespace wsudo
