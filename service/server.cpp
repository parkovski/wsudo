#include "stdo/stdo.h"
#include "stdo/winsupport.h"
#include "stdo/server.h"

#include <vector>
#include <cassert>
#include <memory>
#include <type_traits>
#include <exception>

namespace stdo::server {

enum class EventStatus {
  Finished,
  InProgress,
  Failed,
};

class EventHandler {
public:
  virtual ~EventHandler() { }

  virtual HANDLE event() const = 0;
  virtual EventStatus operator()(class EventListener &) = 0;
};

class QuitHandler : public EventHandler {
  HANDLE _quitEvent;

public:
  QuitHandler(HANDLE quitEvent) noexcept
    : _quitEvent(quitEvent)
  {}

  QuitHandler(QuitHandler &&) = default;

  HANDLE event() const override { return _quitEvent; }

  EventStatus operator()(EventListener &) override {
    return EventStatus::Finished;
  }
};

class event_mutex_abandoned_error : public std::exception {
  std::unique_ptr<EventHandler> _handler;
public:
  explicit event_mutex_abandoned_error(std::unique_ptr<EventHandler> handler)
    noexcept
    : _handler(std::move(handler))
  {}

  const char *what() const noexcept override {
    return "Mutex abandoned";
  }

  const EventHandler *handler() const noexcept {
    return _handler.get();
  }

  // Allow you to take the handler and do something with it.
  std::unique_ptr<EventHandler> &handler() noexcept {
    return _handler;
  }
};

class event_wait_failed_error : public std::exception {
  DWORD _lastError;
public:
  explicit event_wait_failed_error(DWORD lastError) noexcept
    : _lastError(lastError)
  {}

  const char *what() const noexcept override {
    return "Mutex abandoned";
  }

  DWORD getLastError() const noexcept {
    return _lastError;
  }
};

class EventListener {
  std::vector<HANDLE> _events;
  std::vector<std::unique_ptr<EventHandler>> _handlers;
  constexpr static size_t ExitLoopIndex = 0;

  std::unique_ptr<EventHandler> remove(size_t index) {
    _events.erase(_events.cbegin() + index);
    return std::move(*_handlers.erase(_handlers.cbegin() + index));
  }

public:
  template<
    typename H,
    typename = std::enable_if_t<std::is_convertible_v<H &, EventHandler &>>
  >
  explicit EventListener(H exitLoopHandler)
    : _events(MaxPipeConnections + 1),
      _handlers(MaxPipeConnections + 1)
  {
    push(std::move(exitLoopHandler));
  }

  template<typename H>
  std::enable_if_t<std::is_convertible_v<H &, EventHandler &>>
  push(H handler) {
    _events.emplace_back(handler.event());
    _handlers.emplace_back(std::make_unique<H>(std::move(handler)));
  }

  Status eventLoop(DWORD timeout = INFINITE) {
    while (true) {
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
          break;
        case EventStatus::Finished:
          remove(index);
          break;
        case EventStatus::Failed:
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
};

class ClientConnectedHandler : public EventHandler {
  OVERLAPPED _overlapped;

  ClientConnectedHandler() {
    _overlapped.hEvent = CreateEventW(nullptr, true, false, nullptr);
  }

public:
  ClientConnectedHandler(ClientConnectedHandler &&other)
    : _overlapped{other._overlapped}
  {
    other._overlapped.hEvent = nullptr;
  }

  ~ClientConnectedHandler() {
    if (_overlapped.hEvent) {
      CloseHandle(_overlapped.hEvent);
    }
  }

  static EventStatus create(HANDLE pipe, EventListener &listener) {
    ClientConnectedHandler handler;
    ConnectNamedPipe(pipe, &handler._overlapped);
    switch (GetLastError()) {
    case ERROR_IO_PENDING:
      log::trace("Scheduling overlapped IO event.");
      listener.push(std::move(handler));
      return EventStatus::InProgress;
    case ERROR_PIPE_CONNECTED:
      log::trace("Pipe already connected, calling handler immediately.");
      handler(listener);
      return EventStatus::Finished;
    default:
      log::warn("Couldn't connect to pipe.");
      return EventStatus::Failed;
    }
  }

  HANDLE event() const override { return _overlapped.hEvent; }

  EventStatus operator()(EventListener &listener) override {
    return EventStatus::Finished;
  }
};

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
  FINISH(listener.eventLoop());
}

} // namespace stdo::server

