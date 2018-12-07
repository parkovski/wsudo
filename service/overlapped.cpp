#include "wsudo/server.h"
#include "wsudo/ntapi.h"

using namespace wsudo;
using namespace wsudo::server;

template<int WordSize = sizeof(size_t)>
void setOverlappedOffset(LPOVERLAPPED overlapped, size_t offset);

template<>
void setOverlappedOffset<4>(LPOVERLAPPED overlapped, size_t offset) {
  overlapped->Offset = offset;
  overlapped->OffsetHigh = 0;
}

template<>
void setOverlappedOffset<8>(LPOVERLAPPED overlapped, size_t offset) {
  overlapped->Offset = static_cast<DWORD>(offset);
  overlapped->OffsetHigh = static_cast<DWORD>(offset >> 4);
}

EventStatus EventHandlerOverlapped::beginRead(HANDLE hFile) {
  _messageOffset = 0;
  return continueRead(hFile);
}

EventStatus EventHandlerOverlapped::continueRead(HANDLE hFile) {
  _buffer.reserve(_buffer.capacity() < _bufferDoublingLimit * PipeBufferSize
                  ? _buffer.capacity() * 2
                  : _buffer.capacity() + PipeBufferSize);

  setOverlappedOffset<>(_overlapped.get(), _messageOffset);

  DWORD bytesRead;
  if (ReadFile(hFile, _buffer.data() + _messageOffset, PipeBufferSize,
               &bytesRead, _overlapped.get()) &&
      bytesRead > 0)
  {
    _messageOffset += bytesRead;
    return EventStatus::Finished;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Read in progress");
    return EventStatus::InProgress;
  } else {
    log::error("ReadFile failed: {}", getLastErrorString());
    return EventStatus::Failed;
  }
}

EventStatus EventHandlerOverlapped::beginWrite(HANDLE hFile) {
  _messageOffset = 0;
  return continueWrite(hFile);
}

EventStatus EventHandlerOverlapped::continueWrite(HANDLE hFile) {
  setOverlappedOffset<>(_overlapped.get(), _messageOffset);

  DWORD bytesWritten;
  if (_buffer.size() - _messageOffset > PipeBufferSize) {
    bytesWritten = PipeBufferSize;
  } else {
    bytesWritten = _buffer.size();
  }

  if (WriteFile(hFile, _buffer.data() + _messageOffset, bytesWritten,
                &bytesWritten, _overlapped.get()) &&
      bytesWritten > 0)
  {
    _messageOffset += bytesWritten;
    return EventStatus::Finished;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Write in progress");
    return EventStatus::InProgress;
  } else {
    log::error("WriteFile failed: {}", getLastErrorString());
    return EventStatus::Failed;
  }
}

EventStatus EventHandlerOverlapped::endReadWrite(HANDLE hFile) {
  DWORD bytesTransferred;
  DWORD error = GetOverlappedResult(hFile, _overlapped.get(),
                                    &bytesTransferred, false);
  if (error == ERROR_SUCCESS) {
    log::trace("IO successful, {} bytes transferred", bytesTransferred);
    _messageOffset += bytesTransferred;
    return EventStatus::MoreData;
  } else if (error == ERROR_HANDLE_EOF) {
    log::trace("EOF reached, {} bytes transferred", bytesTransferred);
    _messageOffset += bytesTransferred;
    return EventStatus::Finished;
  } else if (error == ERROR_IO_INCOMPLETE) {
    log::trace("IO in progress");
    return EventStatus::InProgress;
  }
  log::error("GetOverlappedResult failed: {}", getSystemStatusString(error));
  return EventStatus::Failed;
}
