#include "stdo/stdo.h"
#include "stdo/server.h"

namespace stdo::server {

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
      switch ((*_handlers[index])(*this)) {
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

EventStatus ClientConnectedHandler::create(HANDLE pipe, EventListener &listener) {
  ClientConnectedHandler handler;
  ConnectNamedPipe(pipe, handler._overlapped.get());
  handler._connection = pipe;
  switch (GetLastError()) {
  case ERROR_IO_PENDING:
    log::trace("Scheduling overlapped IO event.");
    listener.push(std::move(handler));
    return EventStatus::InProgress;
  case ERROR_PIPE_CONNECTED:
    log::trace("Pipe already connected, calling handler immediately.");
    return handler(listener);
  default:
    log::warn("Couldn't connect to pipe.");
    return EventStatus::Failed;
  }
}

EventStatus ClientConnectedHandler::operator()(EventListener &listener) {
  switch (_state) {
  case 0: {
    DWORD bytesTransferred;
    if (GetOverlappedResult(pipe(), _overlapped.get(), &bytesTransferred, false)) {
      log::trace("Pipe connected, trying to read.");
      _state = 1;
      return ClientRecvHandler::create(*this, listener);
    } else {
      return EventStatus::Failed;
    }
  }
  case 1:
    return EventStatus::InProgress;
  case 2:
    return EventStatus::Finished;
  default:
    return EventStatus::Failed;
  }
}

long ClientRecvHandler::read() {
  DWORD bytesRead;
  if (ReadFile(_connection.get().pipe(), _buffer.data(), PipeOutBufferSize,
               &bytesRead, _overlapped.get()) && bytesRead > 0) {
    _buffer.resize(bytesRead + 1);
    _buffer.back() = 0;
    log::trace("Read {} bytes from client", bytesRead);
    DWORD bytesWritten;
    WriteFile(_connection.get().pipe(), "sup", 3, &bytesWritten, nullptr);
    return (long)bytesRead;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    log::trace("Read pending...");
    return 0;
  } else {
    log::warn("Read failed.");
    return -1;
  }
}

EventStatus ClientRecvHandler::create(
  ClientConnectedHandler &connection,
  EventListener &listener
) noexcept
{
  ClientRecvHandler recv{connection};
  auto status = recv(listener);
  if (status == EventStatus::InProgress) {
    log::trace("Scheduling read callback");
    listener.push(std::move(recv));
  }
  return status;
}

EventStatus ClientRecvHandler::operator()(EventListener &listener) {
  long result = read();
  if (result == 0) {
    return EventStatus::InProgress;
  } else if (result < 0) {
    return EventStatus::Failed;
  }
  log::info("Received message: {}", reinterpret_cast<char *>(_buffer.data()));
  _connection.get().finish();
  return EventStatus::Finished;
}

#define FINISH(exitCode) do { config.status = exitCode; return; } while (false)
void serverMain(Config &config) {
  HObject pipe{CreateNamedPipeW(config.pipeName.c_str(),
    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
    MaxPipeConnections, PipeOutBufferSize, PipeInBufferSize,
    PipeDefaultTimeout, nullptr
  )};
  if (!pipe) {
    FINISH(StatusCreatePipeFailed);
  }

  EventListener listener{QuitHandler{config.quitEvent}};
  ClientConnectedHandler::create(pipe, listener);
  FINISH(listener.eventLoop());
}

} // namespace stdo::server

