// The order of these three is important thanks to wil.
#include "wsudo/wsudo.h"
#include <AclAPI.h>
#include "wsudo/tokenmanager.h"

using namespace wsudo;

bool TokenManager::ScopedSecurityPrivilege::setPrivilege(
  HANDLE token, const wchar_t *privilege, bool enabled
) noexcept {
  TOKEN_PRIVILEGES tp;
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = enabled ? SE_PRIVILEGE_ENABLED : 0;
  if (!LookupPrivilegeValue(nullptr, privilege, &tp.Privileges[0].Luid)) {
    log::error(L"Couldn't find LUID for {}.", privilege);
    return false;
  }
  if (!AdjustTokenPrivileges(token, false, &tp, 0,
                             nullptr, nullptr)) {
    log::error(L"Failed to enable {}: (0x{:X}): {}", privilege,
               GetLastError(), lastErrorWString());
    return false;
  }
  log::debug(L"{} {}.", privilege, enabled ? L"enabled" : L"disabled");
  return true;
}

bool TokenManager::ScopedSecurityPrivilege::setPrivilegeState(bool enabled)
  noexcept
{
  bool success =
    setPrivilege(tm->_serverToken.get(), SE_SECURITY_NAME, enabled);
  ok = enabled;
  return success;
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

HANDLE TokenManager::getServerToken() noexcept {
  HANDLE token;
  const DWORD access = TOKEN_DUPLICATE | TOKEN_READ | TOKEN_ADJUST_PRIVILEGES;
  if (!OpenProcessToken(GetCurrentProcess(), access, &token)) {
    log::error("Couldn't open server process token: (0x{:X}): {}",
               GetLastError(), lastErrorString());
    return INVALID_HANDLE_VALUE;
  }
  return token;
}

bool TokenManager::createLaunchToken(HANDLE baseToken) {
  PSID ownerSid;
  PSID groupSid;
  PACL dacl;
  PACL sacl;
  PSECURITY_DESCRIPTOR secDesc;
  SECURITY_INFORMATION secInfo =
    DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION
    | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION;
  if (!SUCCEEDED(GetSecurityInfo(baseToken, SE_KERNEL_OBJECT, secInfo,
                                 &ownerSid, &groupSid, &dacl, &sacl,
                                 &secDesc)))
  {
    log::error("Couldn't get token security descriptor: (0x{:X}): {}",
               GetLastError(), lastErrorString());
    return false;
  }
  WSUDO_SCOPE_EXIT { LocalFree(secDesc); };

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = true;
  secAttr.lpSecurityDescriptor = secDesc;

  DWORD access = TOKEN_READ | TOKEN_WRITE | TOKEN_ASSIGN_PRIMARY;
  if (!DuplicateTokenEx(baseToken, access, &secAttr,
                        SecurityImpersonation, TokenPrimary,
                        _launchToken.put()))
  {
    log::error("Couldn't duplicate token: (0x{:X}) {}",
               GetLastError(), lastErrorString());
    return false;
  }

  return true;
}

TokenManager::TokenManager(DWORD clientProcessId)
  : _serverToken{getServerToken()}
  , _setSecurityPrivilege{*this}
  , _clientProcess{getClientProcess(clientProcessId)}
{}

bool TokenManager::createServerLaunchToken() {
  return createLaunchToken(_serverToken.get());
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
  auto ntstatus = call_NtSetInformationProcess(
    remoteProcessLocal.get(),
    nt::ProcessAccessToken,
    &processAccessToken,
    sizeof(processAccessToken)
  );
  if (!NT_SUCCESS(ntstatus)) {
    log::error("Couldn't assign access token: NTSTATUS=0x{:X}.",
               (ULONG)ntstatus);
    return false;
  }

  wchar_t *name = nullptr;
  DWORD nameSize = MAX_PATH / 2;
  bool ok;
  do {
    nameSize *= 2;
    delete name;
    name = new wchar_t[nameSize];
    ok = QueryFullProcessImageName(remoteProcessLocal.get(), 0, name,
                                   &nameSize);
  } while (!ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  if (!ok) {
    name[0] = name[1] = name[2] = L'?';
    name[3] = 0;
  }
  log::info(L"Successfully adjusted remote process token for '{}'.", name);
  delete name;
  return true;
}
