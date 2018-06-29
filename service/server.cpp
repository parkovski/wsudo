#include "stdo/stdo.h"
#include "stdo/server.h"

using namespace stdo;
using namespace stdo::server;

EventStatus EventHandlerOverlapped::beginRead(HANDLE hFile) {
  _overlapped->Offset = 0;
  _overlapped->OffsetHigh = 0;
  _buffer.resize(PipeBufferSize);
  if (ReadFile(hFile, _buffer.data(), PipeBufferSize, nullptr,
               _overlapped.get()))
  {
    return EventStatus::Finished;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    return EventStatus::InProgress;
  } else {
    log::error("ReadFile failed.");
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
    return EventStatus::InProgress;
  } else {
    log::error("WriteFile failed.");
    return EventStatus::Failed;
  }
}

EventStatus EventHandlerOverlapped::endReadWrite(HANDLE hFile) {
  DWORD bytesTransferred;
  DWORD error = getOverlappedResult(hFile, &bytesTransferred);
  if (error == ERROR_SUCCESS) {
    _buffer.resize(bytesTransferred);
    return EventStatus::Finished;
  } else if (error == ERROR_IO_INCOMPLETE) {
    return EventStatus::InProgress;
  }
  log::error("GetOverlappedResult failed: 0x{:X}", error);
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
  if (bytes) {
    *bytes = bytesTransferred;
  }
  return error;
}

ClientConnectionHandler::ClientConnectionHandler(HANDLE pipe, int clientId)
  noexcept
  : _clientId{clientId}, _callback{connect(pipe)}
{
}

ClientConnectionHandler::Callback
ClientConnectionHandler::connect(HANDLE pipe) {
  ConnectNamedPipe(pipe, _overlapped.get());

  switch (GetLastError()) {
  case ERROR_IO_PENDING:
    log::trace("Client {}: scheduling client connection callback.", _clientId);
    break;
  case ERROR_PIPE_CONNECTED:
    log::trace("Client {}: already connected; setting read event.", _clientId);
    SetEvent(_overlapped->hEvent);
    break;
  default:
    // We don't have an active connection, so keep the callback and pipe handle
    // null and set the event that will return failed and remove this object
    // from the event queue.
    log::error("Client {}: ConnectNamedPipe failed.", _clientId);
    SetEvent(_overlapped->hEvent);
    return nullptr;
  }

  _connection = pipe;
  return &ClientConnectionHandler::read;
}

ClientConnectionHandler::Callback
ClientConnectionHandler::read() {
  if (getOverlappedResult(_connection) != ERROR_SUCCESS) {
    log::error("Client {}: error finalizing connection.", _clientId);
    return nullptr;
  }
  switch (beginRead(_connection)) {
  case EventStatus::Finished:
    log::trace("Client {}: Read ready; responding.", _clientId);
    return respond();
  case EventStatus::InProgress:
    log::trace("Client {}: Scheduling async response.", _clientId);
    return &ClientConnectionHandler::respond;
  default:
    log::error("Client {}: Read failed.", _clientId);
    return nullptr;
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::respond() {
  switch (endReadWrite(_connection)) {
  case EventStatus::Finished:
    break;
  case EventStatus::InProgress:
    switch (beginRead(_connection)) {
    case EventStatus::Finished:
      break;
    case EventStatus::InProgress:
      // Still more to read.
      log::trace("Client {}: More data awaiting read.", _clientId);
      return &ClientConnectionHandler::read;
    default:
      log::error("Client {}: Read failed.", _clientId);
      return nullptr;
    }
    break;
  case EventStatus::Failed:
    log::error("Client {}: Finalizing read failed.", _clientId);
    return nullptr;
  }

  char header[5];
  std::memcpy(header, _buffer.data(), 4);
  header[4] = 0;
  log::debug("Client {}: received message: {}.", _clientId, header);

  Callback nextCb = &ClientConnectionHandler::reset;
  if (!std::memcmp(header, MsgHeaderCredential, 4)) {
    nextCb = &ClientConnectionHandler::read;
    _buffer.resize(3);
    std::memcpy(_buffer.data(), "Ok", 3);
  } else if (!std::memcmp(header, MsgHeaderBless, 4)) {
    _buffer.resize(3);
    std::memcpy(_buffer.data(), "Ok", 3);
  } else {
    _buffer.resize(6);
    std::memcpy(_buffer.data(), "Error", 6);
  }

  switch (beginWrite(_connection)) {
  case EventStatus::InProgress:
    log::trace("Client {}: Write in progress.", _clientId);
    break;
  case EventStatus::Failed:
    log::error("Client {}: Write failed.", _clientId);
    return nullptr;
  case EventStatus::Finished:
    log::trace("Client {}: Write finished.", _clientId);
    return nextCb(this);
  }

  return nextCb;
}

ClientConnectionHandler::Callback
ClientConnectionHandler::reset() {
  if (endReadWrite(_connection) != EventStatus::Finished) {
    return nullptr;
  }

  log::trace("Client {}: Resetting connection.", _clientId);
  resetBuffer();
  HANDLE pipe = _connection;
  _connection = nullptr;
  return connect(pipe);
}

std::unique_ptr<EventHandler> EventListener::remove(size_t index) {
  auto elem = std::move(_handlers[index]);
  _events.erase(_events.cbegin() + index);
  _handlers.erase(_handlers.cbegin() + index);
  return elem;
}

Status EventListener::eventLoop(DWORD timeout) {
  while (true) {
    log::trace("Waiting on {} events", _events.size());
    auto result = WaitForMultipleObjects(
      (DWORD)_events.size(), &_events[0], false, timeout
    );

    if (result == WAIT_TIMEOUT) {
      log::warn("WaitForMultipleObjects timed out.");
      return StatusTimedOut;
    } else if (result >= WAIT_OBJECT_0 &&
               result < WAIT_OBJECT_0 + _events.size())
    {
      size_t index = (size_t)(result - WAIT_OBJECT_0);
      log::trace("Event #{} signaled.", index);
      if (index == ExitLoopIndex) {
        return StatusOk;
      }
      switch ((*_handlers[index])()) {
      case EventStatus::InProgress:
        log::trace("Event #{} returned InProgress.", index);
        ResetEvent(_events[index]);
        break;
      case EventStatus::Finished:
        log::trace("Event #{} returned Finished.", index);
        remove(index);
        break;
      case EventStatus::Failed:
        log::error("Event #{} returned Failed.", index);
        return StatusEventFailed;
        break;
      }
    } else if (result >= WAIT_ABANDONED_0 &&
               result < WAIT_ABANDONED_0 + _events.size())
      
    {
      size_t index = (size_t)(result - WAIT_ABANDONED_0);
      log::error("Mutex abandoned state signaled for handler #{}.", index);
      throw event_mutex_abandoned_error{remove(index)};
    } else if (result == WAIT_FAILED) {
      auto error = GetLastError();
      log::error("WaitForMultipleObjects failed with 0x{:X}.", error);
      throw event_wait_failed_error{error};
    } else {
      log::critical("WaitForMultipleObjects returned 0x{:X}.", result);
      std::terminate();
    }
  }
}

#define FINISH(exitCode) do { config.status = exitCode; return; } while (false)
void stdo::server::serverMain(Config &config) {
  HObject pipe{CreateNamedPipeW(config.pipeName.c_str(),
    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
    MaxPipeConnections, PipeBufferSize, PipeBufferSize,
    PipeDefaultTimeout, nullptr
  )};
  if (!pipe) {
    FINISH(StatusCreatePipeFailed);
  }

  EventListener listener{QuitHandler{config.quitEvent}};
  listener.push(ClientConnectionHandler{pipe, 1});
  FINISH(listener.eventLoop());
}

