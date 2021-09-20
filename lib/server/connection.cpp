#include "wsudo/server.h"

using namespace wsudo;

wscoro::Task<bool> Server::Connection::connect() {
  _overlapped.Internal = 0;
  _overlapped.InternalHigh = 0;
  _overlapped.Pointer = nullptr;
  _overlapped.hEvent = nullptr;
  _key.coroutine = co_await wscoro::this_coroutine;
  log::trace("ServerConnection connect to pipe.");
  if (ConnectNamedPipe(_file.get(), &_overlapped)) {
    log::trace("ServerConnection pipe connected synchronously.");
  } else {
    auto err = GetLastError();
    switch (err) {
      case ERROR_IO_PENDING:
        log::trace("ServerConnection connect pending.");
        break;

      case ERROR_PIPE_CONNECTED:
        log::trace("ServerConnection client was already connected.");
        break;

      default:
        log::error("ServerConnection connection failed: 0x{:X} {}", err,
                   lastErrorString(err));
        co_return false;
    }
  }

  co_await std::suspend_always{};
  co_return true;
}

bool Server::Connection::disconnect() {
  if (DisconnectNamedPipe(_file.get())) {
    log::trace("ServerConnection pipe disconnected.");
    return true;
  }
  auto err = GetLastError();
  if (err == ERROR_PIPE_NOT_CONNECTED) {
    log::trace("ServerConnection pipe was already disconnected.");
    return true;
  }
  log::error("ServerConnection pipe disconnect error 0x{:X} {}", err,
             lastErrorString(err));
  return false;
}

wscoro::FireAndForget Server::Connection::run() {
  while (
       co_await connect()
    && co_await _server->dispatch(*this)
    && disconnect()
  );
  clear();
}

wscoro::Task<> Server::Connection::send(const Message &message) {
  clear();
  message.serialize(_buffer);
  WSUDO_SCOPEEXIT_THIS { clear(); };
  co_await write(_buffer);
}

wscoro::Task<Message> Server::Connection::recv() {
  clear();
  co_await read(_buffer);
  co_return Message{_buffer};
}
