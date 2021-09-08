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

void Server::initSecurity() {
  SID_IDENTIFIER_AUTHORITY worldAuthority = SECURITY_WORLD_SID_AUTHORITY;
  THROW_LAST_ERROR_IF(
    !AllocateAndInitializeSid(
      &worldAuthority, 1,
      SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
      &_sid
    )
  );

  EXPLICIT_ACCESS ea;
  ea.grfAccessPermissions = SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE;
  ea.grfAccessMode = SET_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = (LPWSTR)&_sid;

  PACL pacl;
  THROW_IF_FAILED((HRESULT)SetEntriesInAcl(1, &ea, nullptr, &pacl));
  _acl = wil::unique_hlocal_ptr<ACL>{pacl};

  _securityDescriptor = wil::make_unique_hlocal<SECURITY_DESCRIPTOR>(
    SECURITY_DESCRIPTOR_MIN_LENGTH);

  THROW_LAST_ERROR_IF(
    !InitializeSecurityDescriptor(_securityDescriptor.get(),
                                  SECURITY_DESCRIPTOR_REVISION)
  );

  THROW_LAST_ERROR_IF(
    !SetSecurityDescriptorDacl(_securityDescriptor.get(), true, _acl.get(), false)
  );

  _securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  _securityAttributes.bInheritHandle = false;
  _securityAttributes.lpSecurityDescriptor = _securityDescriptor.get();
}

void Server::initPipe() {
  _pipe.reset(CreateNamedPipe(
    _pipeName.c_str(),
    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
    MaxPipeConnections,
    PipeBufferSize,
    PipeBufferSize,
    PipeDefaultTimeout,
    &_securityAttributes
  ));
  THROW_LAST_ERROR_IF(_pipe.get() == INVALID_HANDLE_VALUE);
}

Server::Server(std::wstring pipeName)
  : _pipeName{std::move(pipeName)}
{
  initSecurity();
  initPipe();
}

Status Server::operator()(int threads) {
  CorIO corio(threads);
  return StatusOk;
}
