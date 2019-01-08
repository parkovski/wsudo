#include "wsudo/events.h"
#include "wsudo/server.h"
#include "wsudo/ntapi.h"

using namespace wsudo;
using namespace wsudo::events;

// Helpers {{{

inline void setOverlappedOffset(LPOVERLAPPED overlapped, size_t offset) {
  overlapped->Pointer = reinterpret_cast<PVOID>(offset);
  // MSDN: Zero unused members before use.
  overlapped->Internal = 0;
  overlapped->InternalHigh = 0;
}

// }}}

EventOverlappedIO::EventOverlappedIO(bool isEventSet) noexcept {
  _overlapped.hEvent = CreateEventW(nullptr, false, isEventSet, nullptr);
}

EventOverlappedIO::~EventOverlappedIO() {
  CloseHandle(_overlapped.hEvent);
}

EventStatus EventOverlappedIO::beginRead() {
  _ioState = IOState::Reading;
  setOverlappedOffset(&_overlapped, _offset);
  _buffer.resize(_offset + ChunkSize);

  if (ReadFile(fileHandle(), _buffer.data() + _offset, ChunkSize,
               nullptr, &_overlapped))
  {
    // Interpret the results.
    return endRead();
  }
  auto error = GetLastError();
  if (error == ERROR_IO_PENDING || error == ERROR_MORE_DATA) {
    log::trace("Read in progress");
    return EventStatus::Ok;
  } else {
    log::error("ReadFile failed: {}", lastErrorString(error));
    _ioState = IOState::Failed;
    return EventStatus::Failed;
  }
}

EventStatus EventOverlappedIO::endRead() {
  DWORD bytesTransferred;
  if (GetOverlappedResult(fileHandle(), &_overlapped, &bytesTransferred, false))
  {
    _offset += bytesTransferred;
    log::trace("Read finished: {} bytes", _offset);
    _buffer.resize(_offset);
    _ioState = IOState::Inactive;
    return EventStatus::Finished;
  }
  auto error = GetLastError();
  if (error == ERROR_IO_PENDING) {
    return EventStatus::Ok;
  } else if (error == ERROR_MORE_DATA) {
    return beginRead();
  }
  log::error("Read failed: {}", lastErrorString(error));
  _ioState = IOState::Failed;
  return EventStatus::Failed;
}

EventStatus EventOverlappedIO::beginWrite() {
  _ioState = IOState::Writing;
  setOverlappedOffset(&_overlapped, _offset);

  if (WriteFile(fileHandle(), _buffer.data() + _offset,
                static_cast<DWORD>(_buffer.size()), nullptr, &_overlapped))
  {
    // Interpret the results.
    return endWrite();
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Write in progress");
    return EventStatus::Ok;
  } else {
    log::error("WriteFile failed: {}", lastErrorString());
    _ioState = IOState::Failed;
    return EventStatus::Failed;
  }
}

EventStatus EventOverlappedIO::endWrite() {
  DWORD bytesTransferred;
  if (GetOverlappedResult(fileHandle(), &_overlapped, &bytesTransferred, false))
  {
    _offset += bytesTransferred;
    if (_offset == _buffer.size()) {
      log::trace("Write finished: {} bytes", _offset);
      _ioState = IOState::Inactive;
      return EventStatus::Finished;
    } else if (_offset > _buffer.size()) {
      log::warn("More data written ({} B) than expected ({} B)",
                _offset, _buffer.size());
      _ioState = IOState::Inactive;
      return EventStatus::Finished;
    } else {
      log::trace("Write in progress: {}%", _offset / _buffer.size());
      return beginWrite();
    }
  }
  if (GetLastError() == ERROR_IO_PENDING) {
    return EventStatus::Ok;
  }
  log::error("Write failed: {}", lastErrorString());
  _ioState = IOState::Failed;
  return EventStatus::Failed;
}

bool EventOverlappedIO::reset() {
  return false;
}

EventStatus EventOverlappedIO::operator()(EventListener &listener) {
  switch (_ioState) {
  case IOState::Inactive:
    return EventStatus::Finished;
  case IOState::Reading:
    return endRead();
  case IOState::Writing:
    return endWrite();
  case IOState::Failed:
    return EventStatus::Failed;
  }

  WSUDO_UNREACHABLE("Invalid IO State");
}

