#include <Windows.h>
#undef min
#undef max
#include <wil/resource.h>

#include <catch2/catch_all.hpp>

#include "wscoro/task.h"

#include <string>
#include <string_view>
#include <sstream>
#include <thread>

class CorIO {
  wil::unique_handle _ioCompletionPort;

  void listener() {
    DWORD bytes;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped;
    while (true) {
      if (!GetQueuedCompletionStatus(_ioCompletionPort.get(), &bytes,
                                     &completionKey, &overlapped, 0)) {
        if (!overlapped) {
          if (GetLastError() == ERROR_ABANDONED_WAIT_0) {
            // Completion port closed.
            return;
          } else {
            // Other IO error.
            continue;
          }
        } else {
          // Done reading.
        }
      }

      // Do something with the data.
      overlapped->Pointer = (void *)((size_t)overlapped->Pointer + bytes);
      auto co = (std::coroutine_handle<> *)completionKey;
      co->resume();
    }
  }

public:
  explicit CorIO()
    : _ioCompletionPort{CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr,
                                               0, 0)}
  {
    THROW_LAST_ERROR_IF_NULL(_ioCompletionPort.get());
    std::thread{[this] () {
      this->listener();
    }}.detach();
  }

  wscoro::Task<std::string> read(LPCWSTR path) {
    std::string result;
    wil::unique_hfile file{
      CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                 FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)
    };

    std::byte buffer[256];
    OVERLAPPED overlapped;
    size_t offset = 0;
    auto this_coro = co_await wscoro::this_coroutine;

    CreateIoCompletionPort(file.get(), _ioCompletionPort.get(),
                           (size_t)&this_coro, 0);

    // TODO: Loop
    overlapped.Pointer = 0;
    while (true) {
      overlapped.Internal = 0;
      overlapped.InternalHigh = 0;
      overlapped.hEvent = nullptr;
      overlapped.Pointer = (void *)offset;
      ReadFile(file.get(), &buffer[0], (DWORD)sizeof(buffer), nullptr,
              &overlapped);
      co_await std::suspend_always{};
      size_t bytes = (size_t)overlapped.Pointer - offset;
      if (bytes == 0) {
        break;
      }
      result += std::string_view{(char *)buffer, (char *)(buffer + bytes)};
      offset = (size_t)overlapped.Pointer;
    }

    co_return result;
  }
};

TEST_CASE("Coroutine IO", "[corio]") {
  const wchar_t *const gpl3_path = L"..\\LICENSE";
  constexpr size_t gpl3_size_lf = 35149;
  constexpr size_t gpl3_size_crlf = 35823;

  CorIO corio;

  auto task = corio.read(gpl3_path);
  task.resume();
  int waitCycles = 0;
  while (!task.await_ready()) {
    if (++waitCycles == 10) {
      FAIL("Coroutine IO took too long.");
      return;
    }
    Sleep(100);
  }
  auto size = task.await_resume().size();
  if (size == gpl3_size_lf || size == gpl3_size_crlf) {
    SUCCEED("Coroutine IO read the correct number of bytes.");
  } else {
    std::stringstream s;
    s << "GPLv3 should be " << gpl3_size_lf << " (LF) or " << gpl3_size_crlf
      << " (CRLF) bytes; read " << size << ".";
    FAIL(s.str());
  }
}
