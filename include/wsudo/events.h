#ifndef WSUDO_EVENTS_H
#define WSUDO_EVENTS_H

#include <vector>
#include <cstdint>

#include "wsudo/winsupport.h"

/**
 * Windows Event Server/Client
 * Uses WaitForMultipleObjects to execute callbacks on event completion.
 */

namespace wsudo::events {

// Status codes for the listener to manage individual handlers.
enum class EventStatus {
  // The event completed its work for this step.
  Ok,
  // The event has nothing more to do and should be removed from the list.
  Finished,
  // The event's state is invalid and it should be removed from the list.
  Failed,
};

class EventListener;

// The callback in operator() is triggered when the event is signaled.
class EventHandler {
public:
  virtual ~EventHandler() = 0;

  // The Windows event that should trigger this event.
  virtual HANDLE event() const = 0;

  // Optional - return true if the state was reset. The default implementation
  // does nothing and returns false.
  virtual bool reset();

  // Event handler implementation.
  virtual EventStatus operator()(EventListener &) = 0;
};

// Lambda wrapper event handler.
template<
  typename F,
  bool AllowReset = false,
  typename = std::is_invocable_r<EventStatus, F, EventListener &>
>
class EventCallback final : public EventHandler {
  F _callback;
  HObject _event;

public:
  EventCallback(F &&callback) noexcept
    : _callback{std::move(callback)},
      _event{CreateEventW(nullptr, true, false, nullptr)}
  {}

  HANDLE event() const override { return _event; }
  bool reset() override {
    if constexpr (AllowReset) {
      ResetEvent(_event);
      return true;
    } else {
      return false;
    }
  }
  EventStatus operator()(EventListener &listener) override {
    return _callback(listener);
  }
};

class EventOverlappedIO : public EventHandler {
  OVERLAPPED _overlapped;
  std::vector<uint8_t> buffer;
  size_t _readOffset;
  size_t _writeOffset;

  const size_t BufferDoublingLimit = 4;

  EventStatus beginRead(HANDLE hFile) {
    _readOffset = 0;
    continueRead(hFile);
  }
  EventStatus continueRead(HANDLE hFile);
  EventStatus beginWrite(HANDLE hFile) {
    _writeOffset = 0;
    continueWrite(hFile);
  }
  EventStatus continueWrite(HANDLE hFile);

public:
  explicit EventOverlappedIO() noexcept;

  EventOverlappedIO(const EventOverlappedIO &) = delete;
  EventOverlappedIO &operator=(const EventOverlappedIO &) = delete;

  ~EventOverlappedIO();

  bool reset() override;

  HANDLE event() const override { return _overlapped.hEvent; }

  // Subclasses should call this first to handle chunked reading/writing.
  EventStatus operator()(EventListener &) override;
};

class EventListener final {
  // List of events to pass to WaitForMultipleObjects.
  std::vector<HANDLE> _events;
  // List of handlers, must be kept in sync with the event list.
  std::vector<std::unique_ptr<EventHandler>> _handlers;
  // Active flag.
  bool _running = true;

  // Remove an event handler from the list.
  void remove(size_t index);

public:
  explicit EventListener() = default;
  EventListener(const EventListener &) = delete;
  EventListener(EventListener &&) = default;

  EventListener &operator=(const EventListener &) = delete;
  EventListener &operator=(EventListener &&) = default;

  // Move a unique_ptr into the event listener.
  template<typename H>
  std::enable_if_t<
    std::is_convertible_v<H &, EventHandler &>,
    H &
  >
  emplace_back(std::unique_ptr<H> &&handler) {
    _events.emplace_back(handler->event());
    return *_handlers.emplace_back(std::move(handler));
  }

  template<typename H>
  std::enable_if_t<
    std::conjunction_v<
      std::is_convertible<H &, EventHandler &>,
      std::is_nothrow_move_constructible<H>
    >,
    H &
  >
  emplace_back(H &&handler) {
    return emplace_back(std::make_unique<H>(std::move(handler)));
  }

  // Construct a handler in place.
  template<typename H, typename Arg1, typename Arg2, typename... Args>
  std::enable_if_t<
    std::conjunction_v<
      std::is_convertible<H &, EventHandler &>,
      std::is_nothrow_constructible<H, Arg1, Arg2, Args...>
    >,
    H &
  >
  emplace_back(Arg1 &&arg1, Arg2 &&arg2, Args &&...args) {
    return emplace_back(std::make_unique<H>(
      std::forward<Arg1>(arg1),
      std::forward<Arg2>(arg2),
      std::forward<Args>(args)...
    ));
  }

  // Run one iteration of the event loop.
  EventStatus next(DWORD timeout = INFINITE);

  // Run the event loop until a quit is triggered. Returns Ok when the loop
  // was suspended but no failure or finish was triggered.
  EventStatus run(DWORD timeout = INFINITE);

  bool isRunning() const { return _running; }
  void stop() { _running = false; }
};

} // namespace wsudo::events

#endif // WSUDO_EVENTS_H
