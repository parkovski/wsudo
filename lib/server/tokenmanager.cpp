// The order of these three is important thanks to wil.
#include "wsudo/wsudo.h"
#include <AclAPI.h>
#include "wsudo/tokenmanager.h"

using namespace wsudo;

void TokenManager::ScopedSecurityPrivilege::setPrivilegeState(bool enabled)
  noexcept
{
  TOKEN_PRIVILEGES secPriv;
  secPriv.PrivilegeCount = 1;
  secPriv.Privileges[0].Attributes = enabled ? SE_PRIVILEGE_ENABLED : 0;
  if (!LookupPrivilegeValue(nullptr, SE_SECURITY_NAME,
                            &secPriv.Privileges[0].Luid)) {
    log::error("Couldn't find LUID for SeSecurityPrivilege.");
    return;
  }
  if (!AdjustTokenPrivileges(tm->_serverToken.get(), false, &secPriv, 0,
                             nullptr, nullptr)) {
    log::error("Failed to enable SeSecurityPrivilege: 0x{:X} {}",
               GetLastError(), lastErrorString());
    return;
  }
  log::debug("{} SeSecurityPrivilege.", enabled ? "Enabled" : "Disabled");
  ok = enabled;
}

HANDLE TokenManager::getClientProcess(DWORD pid) noexcept {
  const DWORD access =
    PROCESS_DUP_HANDLE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
  auto handle = OpenProcess(access, false, pid);
  if (!handle) {
    log::error("Couldn't open client process: 0x{:X} {}", GetLastError(),
               lastErrorString());
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

HANDLE TokenManager::getClientToken() noexcept {
  HANDLE token;
  const DWORD access = TOKEN_DUPLICATE | TOKEN_READ | ACCESS_SYSTEM_SECURITY;
  if (!OpenProcessToken(_clientProcess.get(), access, &token)) {
    log::error("Couldn't open client process token: 0x{:X} {}", GetLastError(),
               lastErrorString());
    return INVALID_HANDLE_VALUE;
  }
  return token;
}

HANDLE TokenManager::getServerToken() noexcept {
  HANDLE token;
  const DWORD access = TOKEN_DUPLICATE | TOKEN_READ | TOKEN_ADJUST_PRIVILEGES;
  if (!OpenProcessToken(GetCurrentProcess(), access, &token)) {
    log::error("Couldn't open server process token: 0x{:X} {}", GetLastError(),
               lastErrorString());
    return INVALID_HANDLE_VALUE;
  }
  return token;
}

bool TokenManager::modifyMandatoryLabel(PACL pSacl, DWORD subAuthority) {
  log::debug("SACL ACE count = {}.", pSacl->AceCount);
  bool success = false;
  for (WORD i = 0; i < pSacl->AceCount; ++i) {
    void *pAce;
    if (!GetAce(pSacl, i, &pAce)) {
      continue;
    }
    auto *header = static_cast<ACE_HEADER *>(pAce);
    log::debug("ACE {}: Type={}, Flags=0x{:X}, Size={}", i, header->AceType,
               header->AceFlags, header->AceSize);
    if (header->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) {
      auto *mlabel = static_cast<SYSTEM_MANDATORY_LABEL_ACE *>(pAce);
      log::debug("Mandatory Label: Access=0x{:X}", mlabel->Mask);
      auto *sid = reinterpret_cast<PSID>(&mlabel->SidStart);
      auto *bytes = reinterpret_cast<PBYTE>(sid);
      log::debug(
        "SID: Rev={}, SubAuthCnt={}, IdAuth={}-{}-{}-{}-{}-{}, SubAuth0={}",
        bytes[0], bytes[1],
        bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[1] ? *GetSidSubAuthority(sid, 0) : 0
      );

      auto const sidSubAuthCount = *GetSidSubAuthorityCount(sid);
      auto *sidSubAuth = GetSidSubAuthority(sid, sidSubAuthCount - 1);
      if (*sidSubAuth < subAuthority) {
        log::debug("Changing SID SubAuthority from 0x{:X} to 0x{:X}",
                   *sidSubAuth, subAuthority);
        *sidSubAuth = subAuthority;
      }
      //return true;
      success = true;
    }
  }
  log::debug("modifyMandatoryLabel returned {}", success);
  return success;
}

bool TokenManager::createLaunchToken(HANDLE baseToken, DWORD mandatoryLevel) {
  PSID ownerSid;
  PSID groupSid;
  PACL dacl;
  PACL sacl;
  PSECURITY_DESCRIPTOR secDesc;
  SECURITY_INFORMATION secInfo =
    DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION
    | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION;
  if (mandatoryLevel) {
    secInfo |= LABEL_SECURITY_INFORMATION;
  }
  if (!SUCCEEDED(GetSecurityInfo(baseToken, SE_KERNEL_OBJECT, secInfo,
                                 &ownerSid, &groupSid, &dacl, &sacl,
                                 &secDesc)))
  {
    log::error("Couldn't get token security descriptor: 0x{:X} {}",
               GetLastError(), lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { LocalFree(secDesc); };

  if (mandatoryLevel) {
    if (!sacl) {
      log::error("GetSecurityInfo did not provide a SACL.");
    } else if (!modifyMandatoryLabel(sacl, mandatoryLevel)) {
      log::error("Couldn't set security descriptor mandatory level.");
    }
  }

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = true;
  secAttr.lpSecurityDescriptor = secDesc;

  if (!DuplicateTokenEx(baseToken, MAXIMUM_ALLOWED, &secAttr,
                        SecurityImpersonation, TokenPrimary,
                        _launchToken.put()))
  {
    log::error("Couldn't duplicate token: 0x{:X} {}", GetLastError(),
               lastErrorString());
    return false;
  }

  return true;
}

TokenManager::TokenManager(DWORD clientProcessId)
  : _serverToken{getServerToken()},
    _setSecurityPrivilege{*this},
    _clientProcess{getClientProcess(clientProcessId)},
    _clientToken{getClientToken()}
{}

bool TokenManager::createServerLaunchToken() {
  return createLaunchToken(_serverToken.get());
}

bool TokenManager::createClientLaunchToken(DWORD mandatoryLevel) {
  return createLaunchToken(_clientToken.get(), mandatoryLevel);
}

bool TokenManager::applyToken(HANDLE remoteProcess) {
  wil::unique_handle remoteProcessLocal;
  if (!DuplicateHandle(_clientProcess.get(), remoteProcess,
                       GetCurrentProcess(), remoteProcessLocal.put(),
                       PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION,
                       false, 0))
  {
    log::error("Couldn't duplicate remote handle: 0x{:X} {}", GetLastError(),
               lastErrorString());
    return false;
  }

  static LinkedModule ntdll{L"ntdll.dll"};
  static auto call_NtSetInformationProcess =
    ntdll.get<nt::NtSetInformationProcess_t>("NtSetInformationProcess");

  nt::PROCESS_ACCESS_TOKEN processAccessToken{_launchToken.get(), nullptr};
  auto ntstatus =
    call_NtSetInformationProcess(remoteProcessLocal.get(),
                                 nt::ProcessAccessToken,
                                 &processAccessToken,
                                 sizeof(processAccessToken));
  if (!NT_SUCCESS(ntstatus)) {
    log::error("Couldn't assign access token: NTSTATUS=0x{:X}.",
               (ULONG)ntstatus);
    return false;
  }

  wchar_t name[MAX_PATH + 1];
  DWORD nameSize = sizeof(name) / sizeof(name[0]);
  if (!QueryFullProcessImageName(remoteProcessLocal.get(), 0, name, &nameSize))
  {
    name[0] = name[1] = name[2] = L'?';
    name[3] = 0;
  }
  log::info(L"Successfully adjusted remote process token for '{}'.", name);
  return true;
}
