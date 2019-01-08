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

  log::trace("Pipe handle factory initialized for {}", to_utf8(pipeName));
}

HObject NamedPipeHandleFactory::operator()() {
  if (!*this) {
    return HObject{};
  }

  DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
  if (_firstInstance) {
    openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
    _firstInstance = false;
  }

  return HObject{CreateNamedPipeW(_pipeName, openMode,
                                  PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                    PIPE_REJECT_REMOTE_CLIENTS,
                                  MaxPipeConnections, PipeBufferSize,
                                  PipeBufferSize, PipeDefaultTimeout,
                                  &_securityAttributes)};
}

NamedPipeHandleFactory::operator bool() const {
  // _securityAttributes is only set when all initialization succeeded.
  return _securityAttributes.nLength == sizeof(SECURITY_ATTRIBUTES);
}
