#define WSUDO_NO_NT_API
#include "wsudo/tokenmanager.h"

# include <NTSecAPI.h>
// wil thinks this needs to be defined for the LSA types, but MSDN says to
// use NTSecAPI instead, which doesn't include this header.
# define _NTLSA_

using namespace wsudo;

bool TokenManager::logon(const std::wstring &domain,
                         const std::wstring &username,
                         std::wstring &password) {
  WSUDO_SCOPEEXIT {
    password.assign(password.length(), L'\0');
    password.clear();
  };

  PVOID pProfileBuffer;
  DWORD profileLength;
  QUOTA_LIMITS quotaLimits;
  HANDLE userToken;
  PSID pSid;
  // LOGON32_LOGON_NETWORK should be used if just verifying that the
  // credentials work. If we actually intend to use the returned token,
  // network tokens have some restrictions and we should use
  // LOGON32_LOGON_INTERACTIVE instead.
  // We can also specify a set of token groups to add to the returned token
  // by using LogonUserExExW which needs to be imported from Advapi32.dll with
  // a LinkedModule.
  if (LogonUserEx(username.c_str(), domain.c_str(), password.c_str(),
                  LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT,
                  &userToken, &pSid, &pProfileBuffer, &profileLength,
                  &quotaLimits)) {
    return true;
  }
  return false;
}
