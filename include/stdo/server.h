#ifndef STDO_SERVER_H
#define STDO_SERVER_H

#include "stdo/winsupport.h"

#include <memory>
#include <type_traits>
#include <exception>
#include <vector>

namespace stdo::server {

constexpr int MaxPipeConnections = 5;
constexpr int PipeInBufferSize = 1024;
constexpr int PipeOutBufferSize = 1024;
constexpr int PipeDefaultTimeout = 0;

enum Status : int {
  StatusUnset = -1,
  StatusOk = 0,
  StatusCreatePipeFailed,
  StatusTimedOut,
  StatusEventFailed,
};

inline const char *statusToString(Status status) {
  switch (status) {
  case StatusUnset: return "status not set";
  case StatusOk: return "ok";
  case StatusCreatePipeFailed: return "pipe creation failed";
  case StatusTimedOut: return "timed out";
  default: return "unknown error";
  }
}

class EventListener;
class EventHandler;

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

enum class EventStatus {
  Finished,
  InProgress,
  Failed,
};

class EventHandler {
public:
  virtual ~EventHandler() { }

  virtual HANDLE event() const = 0;
  virtual EventStatus operator()(EventListener &) = 0;
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

class EventHandlerOverlapped : public EventHandler {
protected:
  std::unique_ptr<OVERLAPPED> _overlapped;

public:
  explicit EventHandlerOverlapped() noexcept
    : _overlapped(std::make_unique<OVERLAPPED>())
  {
    _overlapped->hEvent = CreateEventW(nullptr, true, false, nullptr);
  }

  EventHandlerOverlapped(EventHandlerOverlapped &&other) = default;

  ~EventHandlerOverlapped() {
    if (_overlapped) {
      CloseHandle(_overlapped->hEvent);
    }
  }

  EventHandlerOverlapped &operator=(EventHandlerOverlapped &&other) = default;

  HANDLE event() const override { return _overlapped->hEvent; }
};

class ClientConnectedHandler : public EventHandlerOverlapped {
  Handle<HANDLE, DisconnectNamedPipe> _connection{};
  int _state;

  ClientConnectedHandler() noexcept
    : _state(0)
  { }

public:
  static EventStatus create(HANDLE pipe, EventListener &listener);

  HANDLE pipe() const {
    return _connection;
  }

  void finish() {
    _state = 2;
    SetEvent(_overlapped->hEvent);
  }

  EventStatus operator()(EventListener &listener) override;
};

class ClientRecvHandler : public EventHandlerOverlapped {
  std::vector<unsigned char> _buffer;
  std::reference_wrapper<ClientConnectedHandler> _connection;

  ClientRecvHandler(ClientConnectedHandler &connection) noexcept
    : _connection(connection)
  {
    _buffer.resize(PipeOutBufferSize);
  }

  // Returns positive on successful read, 0 for read pending, and -1 for error.
  long read();

public:
  ClientRecvHandler(ClientRecvHandler &&) = default;
  ClientRecvHandler &operator=(ClientRecvHandler &&) = default;

  static EventStatus create(
    ClientConnectedHandler &connection,
    EventListener &listener
  ) noexcept;

  EventStatus operator()(EventListener &listener) override;
};

class EventListener {
  std::vector<HANDLE> _events;
  std::vector<std::unique_ptr<EventHandler>> _handlers;
  constexpr static size_t ExitLoopIndex = 0;

  std::unique_ptr<EventHandler> remove(size_t index);

public:
  template<
    typename H,
    typename = std::enable_if_t<std::is_convertible_v<H &, EventHandler &>>
  >
  explicit EventListener(H exitLoopHandler)
    : _events{},
      _handlers{}
  {
    _events.reserve(MaxPipeConnections + 1);
    _handlers.reserve(MaxPipeConnections + 1);
    push(std::move(exitLoopHandler));
  }

  template<typename H>
  std::enable_if_t<std::is_convertible_v<H &, EventHandler &>>
  push(H handler) {
    _events.emplace_back(handler.event());
    _handlers.emplace_back(std::make_unique<H>(std::move(handler)));
  }

  Status eventLoop(DWORD timeout = INFINITE);
};

struct Config {
  std::wstring pipeName;
  Status status = StatusUnset;
  HObject quitEvent;

  Config(std::wstring pipeName, HObject quitEvent)
    : pipeName(std::move(pipeName.insert(0, L"\\\\.\\pipe\\"))),
      quitEvent(std::move(quitEvent))
  {}

  void quit() {
    SetEvent(quitEvent);
  }
};

void serverMain(Config &config);

} // namespace stdo::server

#endif // STDO_SERVER_H

