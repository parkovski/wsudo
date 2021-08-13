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

    HANDLE _file;
    OVERLAPPED _overlapped;
    std::coroutine_handle<> _coroutine;

  public:
    explicit FileToken(CorIO &corio, HANDLE file);

    FileToken(const FileToken &) = delete;
    FileToken &operator=(const FileToken &) = delete;

    FileToken(FileToken &&) = delete;
    FileToken &operator=(FileToken &&) = delete;

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

    wscoro::Task<size_t> read(std::span<std::byte> buffer);
    wscoro::Task<std::vector<std::byte>> readToEnd();
    wscoro::Task<size_t> write(std::span<const std::byte> buffer);
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
};

} // namespace wsudo
