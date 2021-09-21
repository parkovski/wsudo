#include "wsudo/tokenmanager.h"

#include <AclAPI.h>

using namespace wsudo;

TokenManager::TokenManager(ULONG clientProcessId)
  : _clientProcessId{clientProcessId}
{}

TokenManager::~TokenManager() {
  if (_token) {
    CloseHandle(_token);
  }
}

bool TokenManager::logon(std::wstring_view domain, std::wstring_view username,
                         std::wstring_view password) {
  // See session.cpp
  return false;
}

bool TokenManager::createServerToken() {

  HANDLE currentToken;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_READ,
                        &currentToken))
  {
    log::error("Couldn't open server token: {}", lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { CloseHandle(currentToken); };

  PSID ownerSid;
  PSID groupSid;
  PACL dacl;
  PACL sacl;
  PSECURITY_DESCRIPTOR secDesc;
  if (!SUCCEEDED(GetSecurityInfo(currentToken, SE_KERNEL_OBJECT,
                                 DACL_SECURITY_INFORMATION |
                                   SACL_SECURITY_INFORMATION |
                                   GROUP_SECURITY_INFORMATION |
                                   OWNER_SECURITY_INFORMATION,
                                 &ownerSid, &groupSid, &dacl, &sacl,
                                 &secDesc)))
  {
    log::error("Couldn't get security info: {}", lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { LocalFree(secDesc); };

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = true;
  secAttr.lpSecurityDescriptor = secDesc;

  HANDLE newToken;
  if (!DuplicateTokenEx(currentToken, MAXIMUM_ALLOWED, &secAttr,
                        SecurityImpersonation, TokenPrimary, &newToken))
  {
    log::error("Couldn't duplicate token: {}", lastErrorString());
    return false;
  }

  _token = newToken;
  log::info("Duplicated server token for client.");
}

bool TokenManager::createRemoteToken(int level) {
  const DWORD access =
    PROCESS_DUP_HANDLE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
  auto clientProcess = OpenProcess(access, false, _clientProcessId);
  if (!clientProcess) {
    log::error("Couldn't open client process: {}", lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { CloseHandle(clientProcess); };

  // ...

  return false;
}

bool TokenManager::createSystemToken() {
  return false;
}

bool TokenManager::applyToken(HANDLE remoteProcess) {
  const DWORD access = PROCESS_DUP_HANDLE | PROCESS_VM_READ;
  log::info("client process: {}", _clientProcessId);
  auto clientProcess = OpenProcess(access, false, _clientProcessId);
  if (!clientProcess) {
    log::error("Couldn't open client process: {}", lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { CloseHandle(clientProcess); };

  HANDLE remoteProcessLocal;
  if (!DuplicateHandle(clientProcess, remoteProcess, GetCurrentProcess(),
                       &remoteProcessLocal, PROCESS_SET_INFORMATION, false, 0))
  {
    log::error("Couldn't duplicate remote handle: {}", lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { CloseHandle(remoteProcessLocal); };

  static LinkedModule ntdll{L"ntdll.dll"};
  auto call_NtSetInformationProcess =
    ntdll.get<nt::NtSetInformationProcess_t>("NtSetInformationProcess");

  nt::PROCESS_ACCESS_TOKEN processAccessToken{_token, nullptr};
  auto ntstatus =
    call_NtSetInformationProcess(remoteProcessLocal, nt::ProcessAccessToken,
                                 &processAccessToken,
                                 sizeof(processAccessToken));
  if (!NT_SUCCESS(ntstatus)) {
    log::error("Couldn't assign access token: NTSTATUS=0x{:X}.", ntstatus);
  }

  log::info("Successfully adjusted remote process token.");
  return true;
}
