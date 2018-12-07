#include "wsudo/server.h"
#include "wsudo/events.h"
#include "wsudo/ntapi.h"

#include <aclapi.h>

#pragma comment(lib, "Advapi32.lib")

using namespace wsudo;
using namespace wsudo::server;

template<typename F>
static
std::enable_if_t<std::is_invocable_r_v<bool, F, LPSECURITY_ATTRIBUTES>, bool>
doWithSecurityAttributes(F fn) {
  Handle<PSID, FreeSid> sidEveryone{};
  SID_IDENTIFIER_AUTHORITY sidAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
  if (!AllocateAndInitializeSid(&sidAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0,
                                0, 0, 0, 0, &sidEveryone))
  {
    return false;
  }

  EXPLICIT_ACCESS_W ea;
  ea.grfAccessPermissions = SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE;
  ea.grfAccessMode = SET_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = (LPWSTR)&sidEveryone;

  Handle<PACL, LocalFree> acl{};
  if (!SUCCEEDED(SetEntriesInAclW(1, &ea, nullptr, &acl))) {
    return false;
  }

  Handle<PSECURITY_DESCRIPTOR, LocalFree> secDesc{
    LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH)
  };
  if (!secDesc) {
    return false;
  }

  if (!InitializeSecurityDescriptor(secDesc, SECURITY_DESCRIPTOR_REVISION)) {
    return false;
  }

  if (!SetSecurityDescriptorDacl(secDesc, true, acl, false)) {
    return false;
  }

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = false;
  secAttr.lpSecurityDescriptor = secDesc;

  return fn(&secAttr);
}

void wsudo::server::serverMain(Config &config) {
  using namespace events;

  HObject pipe{};
  if (!doWithSecurityAttributes([&](LPSECURITY_ATTRIBUTES sa) -> bool {
    pipe = CreateNamedPipeW(config.pipeName.c_str(),
                            PIPE_ACCESS_DUPLEX |
                              FILE_FLAG_FIRST_PIPE_INSTANCE |
                              FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                              PIPE_REJECT_REMOTE_CLIENTS,
                            MaxPipeConnections, PipeBufferSize, PipeBufferSize,
                            PipeDefaultTimeout, sa);
    return !!pipe;
  }))
  {
    config.status = StatusCreatePipeFailed;
    return;
  }

  EventListener listener;
  //listener.push(ClientConnectionHandler{pipe, 1});
  *config.quitEvent = listener.emplace_back(EventCallback {
    [](EventListener &listener) {
      listener.stop();
      return EventStatus::Finished;
    }
  }).event();

  EventStatus status;
  do {
    status = listener.run();
  } while (status == EventStatus::Ok);

  if (status == EventStatus::Failed) {
    config.status = StatusEventFailed;
  } else if (status == EventStatus::Finished) {
    config.status = StatusOk;
  }
}

