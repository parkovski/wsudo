#include "wsudo/server.h"
#include "wsudo/session.h"
#include "wsudo/corio.h"

#pragma comment(lib, "Advapi32.lib")

using namespace wsudo;
using namespace wsudo::server;

void wsudo::server::serverMain(Config &config) {
  using namespace events;

  session::SessionManager sessionManager{60 * 10};

  NamedPipeHandleFactory pipeHandleFactory{config.pipeName.c_str()};
  if (!pipeHandleFactory) {
    config.status = StatusCreatePipeFailed;
    return;
  }

  EventListener listener;
  *config.quitEvent = CreateEventW(nullptr, true, false, nullptr);
  listener.emplace(*config.quitEvent, [](EventListener &listener) {
    listener.stop();
    return EventStatus::Finished;
  });

  for (int id = 1; id <= MaxPipeConnections; ++id) {
    listener.emplace<ClientConnectionHandler>(pipeHandleFactory(), id,
                                              sessionManager);
  }

  EventStatus status = listener.run();

  if (status == EventStatus::Failed) {
    config.status = StatusEventFailed;
  } else {
    config.status = StatusOk;
  }
}

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

  log::trace("Created pipe security attributes");

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
  log::trace("Created {}pipe handle", first ? "first " : "");
  return pipe;
}

Server::Server(std::wstring pipeName)
  : _pipeName{std::move(pipeName)}
{}

class Connection : public CorIO::AsyncFile {
  std::string _buffer;

public:
  using CorIO::AsyncFile::AsyncFile;

  class Bootstrap;
  wscoro::Task<bool> connect(Bootstrap &bootstrap);
  wscoro::Task<bool> connect();
  bool disconnect();
  wscoro::FireAndForget run(Bootstrap *bootstrap = nullptr);

  class Bootstrap {
    CorIO *_corio;
    std::coroutine_handle<> _coroutine;
    wil::unique_event _event;
    std::thread _thread;

    void loop() {
      log::debug("Begin connection bootstrap loop.");
      while (true) {
        auto result = WaitForSingleObject(_event.get(), INFINITE);
        _event.ResetEvent();
        if (!_coroutine) {
          log::trace("Connection bootstrap finished.");
          return;
        }
        log::debug("Connection bootstrap entering main loop.");
        _corio->postCallback([=] () { _coroutine.resume(); });
      }
    }

  public:
    explicit Bootstrap(CorIO &corio)
      : _corio{&corio},
        _event{CreateEvent(nullptr, true, false, nullptr)},
        _thread{[this] () { this->loop(); }}
    {}

    ~Bootstrap() {
      if (_thread.joinable()) {
        log::trace("Bootstrap destroy");
        quit();
      }
    }

    HANDLE prepare(std::coroutine_handle<> coroutine) {
      _coroutine = coroutine;
      return _event.get();
    }

    void quit() {
      _coroutine = nullptr;
      _event.SetEvent();
      _thread.join();
    }
  };
};

wscoro::Task<bool> Connection::connect(Bootstrap &bootstrap) {
  _overlapped.Internal = 0;
  _overlapped.InternalHigh = 0;
  _overlapped.Pointer = nullptr;
  // Setting the low order bit ensures that the IOCP won't be notified.
  _overlapped.hEvent = reinterpret_cast<HANDLE>(1 | reinterpret_cast<size_t>(
    bootstrap.prepare(co_await wscoro::this_coroutine)
  ));
  if (ConnectNamedPipe(_file.get(), &_overlapped)) {
    log::trace("Pipe connected synchronously.");
    SetEvent(_overlapped.hEvent);
  } else {
    auto err = GetLastError();
    switch (err) {
      case ERROR_IO_PENDING:
        log::trace("Waiting for pipe connection.");
        break;

      case ERROR_PIPE_CONNECTED:
        log::trace("Pipe client was already connected.");
        SetEvent(_overlapped.hEvent);
        break;

      default:
        log::error("Pipe connection failed: 0x{:X} {}", err, lastErrorString(err));
        co_return false;
    }
  }

  co_await std::suspend_always{};
  co_return true;
}

wscoro::Task<bool> Connection::connect() {
  _overlapped.Internal = 0;
  _overlapped.InternalHigh = 0;
  _overlapped.Pointer = nullptr;
  _overlapped.hEvent = nullptr;
  _key.coroutine = co_await wscoro::this_coroutine;
  if (ConnectNamedPipe(_file.get(), &_overlapped)) {
    log::trace("Pipe connected synchronously.");
  } else {
    auto err = GetLastError();
    switch (err) {
      case ERROR_IO_PENDING:
        log::trace("Waiting for pipe connection.");
        break;

      case ERROR_PIPE_CONNECTED:
        log::trace("Pipe client was already connected.");
        break;

      default:
        log::error("Pipe connection failed: 0x{:X} {}", err, lastErrorString(err));
        co_return false;
    }
  }

  co_await std::suspend_always{};
  co_return true;
}

bool Connection::disconnect() {
  if (DisconnectNamedPipe(_file.get())) {
    log::trace("Pipe disconnected.");
    return true;
  }
  auto err = GetLastError();
  if (err == ERROR_PIPE_NOT_CONNECTED) {
    log::trace("Pipe was already disconnected.");
    return true;
  }
  log::error("Pipe disconnect error 0x{:X} {}", err, lastErrorString(err));
  return false;
}

wscoro::FireAndForget Connection::run(Bootstrap *bootstrap) {
  auto prewrite = [this] () -> bool {
    log::info("Client sent: {}", _buffer);
    _buffer.clear();
    return true;
  };

  while (
    (bootstrap
      ? co_await connect(*bootstrap)
      : co_await connect())
    && co_await readToEnd(_buffer)
    && prewrite()
    && co_await write({msg::server::AccessDenied, msg::server::AccessDenied + 4})
    && disconnect()
  );
}

HRESULT Server::operator()(int nUserThreads, int nSystemThreads) {
  log::debug("Server start; user threads = {}; system threads = {}.",
             nUserThreads, nSystemThreads);

  CorIO corio(nSystemThreads);
  _quitHandle = &corio;
  corio.run(nUserThreads);

  auto pipe = initPipe(true);
  auto conn = corio.make<Connection>(pipe);

  if (options.useConnectionBootstrap) {
    Connection::Bootstrap boot(corio);
    conn.run(&boot);
    corio.wait();
  } else {
    conn.run();
    corio.wait();
  }

  return S_OK;
}

void Server::quit() {
  if (auto corio = static_cast<CorIO *>(_quitHandle)) {
    corio->postQuitMessage(0);
    log::trace("Posted quit message.");
  } else {
    log::error("Quit called without quit handle set.");
    std::terminate();
  }
}
