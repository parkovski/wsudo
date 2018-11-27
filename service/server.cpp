#include "stdo/server.h"
#include "stdo/ntapi.h"

#include <aclapi.h>

#pragma comment(lib, "Advapi32.lib")

using namespace stdo;
using namespace stdo::server;

std::unique_ptr<EventHandler> EventListener::remove(size_t index) {
  auto elem = std::move(_handlers[index]);
  _events.erase(_events.cbegin() + index);
  _handlers.erase(_handlers.cbegin() + index);
  return elem;
}

Status EventListener::eventLoop(DWORD timeout) {
  while (true) {
    log::trace("Waiting on {} events", _events.size());
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
        log::trace("Event #{} returned InProgress.", index);
        ResetEvent(_events[index]);
        break;
      case EventStatus::Finished:
        log::trace("Event #{} returned Finished.", index);
        remove(index);
        break;
      case EventStatus::Failed:
        log::error("Event #{} returned Failed.", index);
        remove(index);
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
      log::error("WaitForMultipleObjects failed: {}",
                 getSystemStatusString(error));
      throw event_wait_failed_error{error};
    } else {
      log::critical("WaitForMultipleObjects returned 0x{:X}.", result);
      std::terminate();
    }
  }
}

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

#define FINISH(exitCode) do { config.status = exitCode; return; } while (false)
void stdo::server::serverMain(Config &config) {
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
    FINISH(StatusCreatePipeFailed);
  }

  EventListener listener;
  listener.push(ClientConnectionHandler{pipe, 1});
  *config.quitEvent = listener.quitEvent();
  FINISH(listener.eventLoop());
}

