#include "wsudo/events.h"
#include "wsudo/server.h"
#include "wsudo/ntapi.h"

using namespace wsudo;
using namespace wsudo::events;

template<int WordSize = sizeof(size_t)>
static inline void setOverlappedOffset(LPOVERLAPPED overlapped, size_t offset);

template<>
inline void setOverlappedOffset<4>(LPOVERLAPPED overlapped, size_t offset) {
  overlapped->Offset = static_cast<DWORD>(offset);
  overlapped->OffsetHigh = 0;
}

template<>
inline void setOverlappedOffset<8>(LPOVERLAPPED overlapped, size_t offset) {
  overlapped->Offset = static_cast<DWORD>(offset);
  overlapped->OffsetHigh = static_cast<DWORD>(offset >> 4);
}

EventOverlappedIO::EventOverlappedIO() noexcept
  : _overlapped{},
    _readOffset{0},
    _writeOffset{0}
{
  _overlapped.hEvent = CreateEventW(nullptr, true, false, nullptr);
}

EventOverlappedIO::~EventOverlappedIO() {
  CloseHandle(_overlapped.hEvent);
}

EventStatus EventOverlappedIO::beginRead(HANDLE hFile) {
  setOverlappedOffset<>(&_overlapped, _readOffset);

  if (ReadFile(hFile, _buffer.data() + _readOffset, PipeBufferSize,
               nullptr, &_overlapped))
  {
    // Interpret the results.
    return endRead(hFile);
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Read in progress");
    return EventStatus::Ok;
  } else {
    log::error("ReadFile failed: {}", getLastErrorString());
    return EventStatus::Failed;
  }
}

EventStatus EventOverlappedIO::endRead(HANDLE hFile) {
  _buffer.reserve(_buffer.capacity() + PipeBufferSize);

  // TODO: How to know how much was read?
  // Count at beginning? Null? What is the maximum size of a single message?
  // Just read till we're done? Do we get an EOF message?
  DWORD bytesTransferred;
  if (GetOverlappedResult(hFile, &_overlapped, &bytesTransferred, false)) {
    _readOffset += bytesTransferred;
    log::trace("Read finished: {} bytes", bytesTransferred);
    return EventStatus::Finished;
  }
}

EventStatus EventOverlappedIO::beginWrite(HANDLE hFile) {
  setOverlappedOffset<>(&_overlapped, _writeOffset);

  DWORD writeSize;
  if (_buffer.size() - _writeOffset > PipeBufferSize) {
    writeSize = PipeBufferSize;
  } else {
    writeSize = static_cast<DWORD>(_buffer.size());
  }

  if (WriteFile(hFile, _buffer.data() + _writeOffset, writeSize,
                nullptr, &_overlapped))
  {
    // Interpret the results.
    return endWrite(hFile);
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Write in progress");
    return EventStatus::Ok;
  } else {
    log::error("WriteFile failed: {}", getLastErrorString());
    return EventStatus::Failed;
  }
}

EventStatus EventOverlappedIO::endWrite(HANDLE hFile) {
  DWORD bytesTransferred;
  if (GetOverlappedResult(hFile, &_overlapped, &bytesTransferred, false)) {
    _writeOffset += bytesTransferred;
    if (_writeOffset == _buffer.size()) {
      log::trace("Write finished: {} bytes", _writeOffset);
      return EventStatus::Finished;
    } else if (_writeOffset > _buffer.size()) {
      log::warn("More data written ({} B) than expected ({} B)",
                _writeOffset, _buffer.size());
      return EventStatus::Finished;
    } else {
      log::trace("Write in progress: {}%", _writeOffset / _buffer.size());
      return EventStatus::Ok;
    }
  }
}

