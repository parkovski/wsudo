#include "wsudo/server.h"
#include "wsudo/tokenmanager.h"

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
      co_await conn.send(msg::Success{});
      message = co_await conn.recv();
      if (std::holds_alternative<msg::Bless>(message)) {
        auto clientPid = conn.clientProcessId();
        auto handle = std::get<msg::Bless>(message).hRemoteProcess;
        TokenManager tm(clientPid);
        if (tm.createServerLaunchToken() && tm.applyToken(handle)) {
          message = msg::Success{};
        } else {
          message = msg::Failure{};
        }
      } else {
        message = msg::Invalid{};
      }
      co_await conn.send(message);
      break;
    } else if (std::holds_alternative<msg::Credential>(message)) {
      co_await conn.send(msg::AccessDenied{});
    } else {
      co_await conn.send(msg::Invalid{});
      break;
    }
  }

  co_return true;
}
