#include "wsudo/server.h"
#include "wsudo/corio.h"

#pragma comment(lib, "Advapi32.lib")

using namespace wsudo;

HRESULT Server::initSecurity() noexcept {
  SID_IDENTIFIER_AUTHORITY worldAuthority = SECURITY_WORLD_SID_AUTHORITY;
  RETURN_IF_WIN32_BOOL_FALSE(
    AllocateAndInitializeSid(
      &worldAuthority, 1,
      SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
      _sid.put()
    )
  );

  EXPLICIT_ACCESS ea;
  ea.grfAccessPermissions = SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE;
  ea.grfAccessMode = SET_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(_sid.get());

  PACL pacl;
  RETURN_IF_WIN32_ERROR(SetEntriesInAcl(1, &ea, nullptr, &pacl));
  _acl = wil::unique_hlocal_ptr<ACL>{pacl};

  _securityDescriptor = wil::make_unique_hlocal_nothrow<SECURITY_DESCRIPTOR>(
    SECURITY_DESCRIPTOR_MIN_LENGTH);
  RETURN_IF_NULL_ALLOC(_securityDescriptor.get());

  RETURN_IF_WIN32_BOOL_FALSE(
    InitializeSecurityDescriptor(_securityDescriptor.get(),
                                 SECURITY_DESCRIPTOR_REVISION)
  );

  RETURN_IF_WIN32_BOOL_FALSE(
    SetSecurityDescriptorDacl(_securityDescriptor.get(), true, _acl.get(),
                              false)
  );

  _securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  _securityAttributes.bInheritHandle = false;
  _securityAttributes.lpSecurityDescriptor = _securityDescriptor.get();

  log::trace("Server created pipe security attributes");

  return S_OK;
}

HANDLE Server::initPipe(bool first) {
  DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  if (first) {
    THROW_IF_FAILED(initSecurity());
    openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
  }
  HANDLE pipe = CreateNamedPipe(
    _pipeName.c_str(),
    openMode,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
    MaxPipeConnections,
    PipeBufferSize,
    PipeBufferSize,
    PipeDefaultTimeout,
    &_securityAttributes
  );
  THROW_LAST_ERROR_IF(pipe == INVALID_HANDLE_VALUE);
  log::trace("Server created {}pipe handle", first ? "first " : "");
  return pipe;
}

Server::Server(std::wstring pipeName)
  : _pipeName{std::move(pipeName)}
{}

HRESULT Server::operator()(int nUserThreads, int nSystemThreads) {
  log::debug("Server start; user threads = {}; system threads = {}.",
             nUserThreads, nSystemThreads);

  CorIO corio(nSystemThreads);
  _quitHandle = &corio;
  corio.run(nUserThreads);

  auto c1 = corio.make<Connection>(initPipe(true), *this);
  auto c2 = corio.make<Connection>(initPipe(), *this);
  c1.run();
  c2.run();

  corio.wait();

  return S_OK;
}

void Server::quit() {
  if (auto corio = static_cast<CorIO *>(_quitHandle)) {
    corio->postQuitMessage(0);
    log::trace("Server posted quit message.");
  } else {
    log::error("Server quit called without quit handle set.");
    std::terminate();
  }
}

wscoro::Task<bool> Server::dispatch(Connection &conn) {
  while (true) {
    auto message = co_await conn.recv();

    std::string_view header{msg::Invalid::code};
    if (conn.buffer().length() >= 4) {
      header = std::string_view{conn.buffer()}.substr(0, 4);
    }
    log::debug("Received message {}.", header);

    if (std::holds_alternative<msg::QuerySession>(message)) {
      co_await conn.send(msg::Failure{});
    } else if (std::holds_alternative<msg::Credential>(message)) {
      co_await conn.send(msg::AccessDenied{});
    } else {
      co_await conn.send(msg::Invalid{});
      break;
    }
  }

  co_return true;
}

// ===== Connection =====

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
