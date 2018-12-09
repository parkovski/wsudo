#ifndef WSUDO_SERVER_H
#define WSUDO_SERVER_H

#include "wsudo/wsudo.h"
#include "wsudo/winsupport.h"
#include "wsudo/events.h"

#include <memory>
#include <type_traits>
#include <vector>
#include <string_view>

namespace wsudo::server {

// Maximum concurrent server connections. Being sudo, it's unlikely to have to
// process many things concurrently, but we have to give Windows a number.
constexpr int MaxPipeConnections = 10;

// Pipe timeout, again for Windows.
constexpr int PipeDefaultTimeout = 0;

// Server status codes
enum Status : int {
  StatusUnset = -1,
  StatusOk = 0,
  StatusCreatePipeFailed,
  StatusTimedOut,
  StatusEventFailed,
};

inline const char *statusToString(Status status) {
  switch (status) {
  default: return "unknown status";
  case StatusUnset: return "status not set";
  case StatusOk: return "ok";
  case StatusCreatePipeFailed: return "pipe creation failed";
  case StatusTimedOut: return "timed out";
  case StatusEventFailed: return "event failed";
  }
}

// Event handlers {{{

using HPipeConnection = Handle<HANDLE, DisconnectNamedPipe>;

// Waits for a client connection, then pushes the session onto the event loop.
class ClientListener final : public events::EventOverlappedIO {
public:
  explicit ClientListener() noexcept;

  bool reset() override;
  EventStatus operator()(EventListener &) override;
};

class ClientSession final : public events::EventOverlappedIO {
public:
  explicit ClientSession(HPipeConnection pipe, int clientId) noexcept;

  bool reset() override;
  EventStatus operator()(EventListener &) override;

private:
  using Callback = recursive_mem_callback<ClientListener>;

  Callback readRequest();
  Callback writeResponse();

  int _clientId;
  HPipeConnection _connection;
  Callback _callback;
  // HObject _userToken;
};

// Event handlers }}}

#if 0
class EventHandlerOverlapped : public EventHandler {
protected:
  std::unique_ptr<OVERLAPPED> _overlapped;
  std::vector<char> _buffer;
  size_t _messageOffset;

  // Buffer size will stop doubling here and grow linearly. This is the amount
  // of times the buffer will double, so the actual size will be this number
  // times PipeBufferSize.
  const size_t _bufferDoublingLimit = 4;

  EventStatus beginRead(HANDLE hFile);
  EventStatus continueRead(HANDLE hFile);
  EventStatus beginWrite(HANDLE hFile);
  EventStatus continueWrite(HANDLE hFile);

  // Returns MoreData when EOF is not reached.
  EventStatus endReadWrite(HANDLE hFile);

public:
  explicit EventHandlerOverlapped() noexcept
    : _overlapped{std::make_unique<OVERLAPPED>()}, _messageOffset{0}
  {
    _overlapped->hEvent = CreateEventW(nullptr, true, false, nullptr);
    _buffer.reserve(PipeBufferSize);
  }

  EventHandlerOverlapped(EventHandlerOverlapped &&other) = default;

  ~EventHandlerOverlapped() {
    if (_overlapped) {
      CloseHandle(_overlapped->hEvent);
    }
  }

  EventHandlerOverlapped &operator=(EventHandlerOverlapped &&other) = default;

  HANDLE event() const override { return _overlapped->hEvent; }
  void resetEvent() { ResetEvent(_overlapped->hEvent); }
};

using HPipeConnection = Handle<HANDLE, DisconnectNamedPipe>;
class ClientConnectionHandler : public EventHandlerOverlapped {
  struct Callback {
    using Function = Callback (ClientConnectionHandler::*)();

    Callback() noexcept : function(nullptr) {}
    Callback(Function function) noexcept : function(function) {}
    Callback(const Callback &) = default;
    Callback &operator=(const Callback &) = default;

    Callback operator()(ClientConnectionHandler *self) {
      return (self->*function)();
    }

    explicit operator bool() const { return !!function; }

  private:
    Function function;
  };

  int _clientId;
  HPipeConnection _connection{};
  Callback _callback{};
  HObject _userToken{};

  void createResponse(const char *header,
                      std::string_view message = std::string_view{});

  Callback connect(HANDLE pipe);
  Callback finishConnect();
  Callback read();
  Callback finishRead();
  Callback respond();
  template<bool Loop>
  Callback finishRespond();
  Callback reset();

  // Returns true to read another message, false to reset the connection.
  bool dispatchMessage();
  bool tryToLogonUser(const char *username, const char *password);
  bool bless(HANDLE remoteHandle);

public:
  explicit ClientConnectionHandler(HANDLE pipe, int clientId) noexcept;

  void reset2();
  bool isWaitingForConnection();

  EventStatus operator()(EventListener &) override {
    if (!_callback || !(_callback = _callback(this))) {
      return EventStatus::Failed;
    }
    return EventStatus::InProgress;
  }
};
#endif

struct Config {
  // Named pipe filename.
  std::wstring pipeName;

  // Pointer to global quit event handle.
  HANDLE *quitEvent;

  // Server status return value.
  Status status = StatusUnset;

  explicit Config(std::wstring pipeName, HANDLE *quitEvent)
    : pipeName(std::move(pipeName)), quitEvent(quitEvent)
  {}
};

void serverMain(Config &config);

} // namespace wsudo::server

#endif // WSUDO_SERVER_H

