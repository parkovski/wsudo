#include "wsudo/server.h"

using namespace wsudo;
using namespace wsudo::server;

NamedPipeHandleFactory::NamedPipeHandleFactory(LPCWSTR pipeName) noexcept
  : _pipeName{pipeName}
{
  _sidAuth = SECURITY_WORLD_SID_AUTHORITY;
  if (!AllocateAndInitializeSid(&_sidAuth, 1, SECURITY_WORLD_RID, 0, 0, 0, 0,
                                0, 0, 0, &_sid))
  {
    return;
  }

  _explicitAccess.grfAccessPermissions = SYNCHRONIZE | GENERIC_READ |
                                         GENERIC_WRITE;
  _explicitAccess.grfAccessMode = SET_ACCESS;
  _explicitAccess.grfInheritance = NO_INHERITANCE;
  _explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  _explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  _explicitAccess.Trustee.ptstrName = (LPWSTR)&_sid;

  if (!SUCCEEDED(SetEntriesInAclW(1, &_explicitAccess, nullptr, &_acl))) {
    return;
  }

  _securityDescriptor = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
  if (!_securityDescriptor) {
    return;
  }

  if (!InitializeSecurityDescriptor(_securityDescriptor,
                                    SECURITY_DESCRIPTOR_REVISION))
  {
    return;
  }

  if (!SetSecurityDescriptorDacl(_securityDescriptor, true, _acl, false)) {
    return;
  }

  _securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  _securityAttributes.bInheritHandle = false;
  _securityAttributes.lpSecurityDescriptor = _securityDescriptor;

  log::debug("Named pipe security attributes initialized.");
}

HObject NamedPipeHandleFactory::operator()() {
  if (!*this) {
    return HObject{};
  }

  DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  if (_firstInstance) {
    openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
  }

  HANDLE pipe = CreateNamedPipeW(_pipeName, openMode,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                   PIPE_REJECT_REMOTE_CLIENTS,
                                 MaxPipeConnections, PipeBufferSize,
                                 PipeBufferSize, PipeDefaultTimeout,
                                 &_securityAttributes);

  // We consider failing to open the first instance a critical failure,
  // but subsequent failures are just warnings because we still have an open
  // connection.
  if (_firstInstance) {
    if (!pipe) {
      log::critical("Failed to create named pipe '{}'.", to_utf8(_pipeName));
    } else {
      log::info("Listening on '{}'.", to_utf8(_pipeName));
    }
  } else {
    if (!pipe) {
      log::warn("Failed to open named pipe instance for '{}'.",
                to_utf8(_pipeName));
    }
  }

  _firstInstance = false;
  return HObject{pipe};
}

NamedPipeHandleFactory::operator bool() const {
  // _securityAttributes is only set when all initialization succeeded.
  return _securityAttributes.nLength == sizeof(SECURITY_ATTRIBUTES);
}
