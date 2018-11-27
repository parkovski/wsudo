#include "stdo/server.h"
#include "stdo/ntapi.h"

using namespace stdo;
using namespace stdo::server;

// TODO: These don't currently handle larger data than the buffer.

EventStatus EventHandlerOverlapped::beginRead(HANDLE hFile) {
  _overlapped->Offset = 0;
  _overlapped->OffsetHigh = 0;
  _buffer.resize(PipeBufferSize);
  if (ReadFile(hFile, _buffer.data(), PipeBufferSize, nullptr,
               _overlapped.get()))
  {
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
  _overlapped->Offset = 0;
  _overlapped->OffsetHigh = 0;
  if (WriteFile(hFile, _buffer.data(), (DWORD)_buffer.size(), nullptr,
                _overlapped.get()))
  {
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
  DWORD error = getOverlappedResult(hFile, &bytesTransferred);
  if (error == ERROR_SUCCESS) {
    log::trace("IO finished");
    _buffer.resize(bytesTransferred);
    return EventStatus::Finished;
  } else if (error == ERROR_IO_INCOMPLETE) {
    log::trace("IO in progress");
    return EventStatus::InProgress;
  }
  log::error("GetOverlappedResult failed: {}", getSystemStatusString(error));
  return EventStatus::Failed;
}

DWORD EventHandlerOverlapped::getOverlappedResult(HANDLE hFile, DWORD *bytes) {
  DWORD bytesTransferred;
  DWORD error;
  if (
    GetOverlappedResult(hFile, _overlapped.get(), &bytesTransferred, false) ||
    (error = GetLastError()) == ERROR_HANDLE_EOF
  )
  {
    error = ERROR_SUCCESS;
    ResetEvent(_overlapped->hEvent);
  } else {
    bytesTransferred = 0;
  }
  log::trace("Overlapped: Transferred {} bytes", bytesTransferred);
  if (bytes) {
    *bytes = bytesTransferred;
  }
  return error;
}
