#define WSUDO_NO_NT_API
#include "wsudo/session.h"
#include <NTSecAPI.h>
#include <cstdlib>

#define NT_SUCCESS(status) ((long)(status) >= 0)

using namespace wsudo;
using namespace wsudo::session;

SessionManager::SessionManager(unsigned defaultTtlSeconds) noexcept
  : _defaultTtlSeconds{defaultTtlSeconds},
    _timer{CreateWaitableTimerW(nullptr, false, nullptr)}
{
  NTSTATUS status;
  LSA_OBJECT_ATTRIBUTES attr{{}};
  Handle<LSA_HANDLE, LsaClose> policy;

  status = LsaOpenPolicy(nullptr, &attr, POLICY_VIEW_LOCAL_INFORMATION,
                         &policy);
  if (!NT_SUCCESS(status)) {
    log::error("LsaOpenPolicy failed: {}",
               lastErrorString(LsaNtStatusToWinError(status)));
    return;
  }

  Handle<PPOLICY_ACCOUNT_DOMAIN_INFO, LsaFreeMemory> accountDomain;
  status = LsaQueryInformationPolicy(policy, PolicyAccountDomainInformation,
                                     reinterpret_cast<void **>(&accountDomain));
  if (!NT_SUCCESS(status)) {
    log::error("LsaQueryInformationPolicy failed: {}",
                lastErrorString(LsaNtStatusToWinError(status)));
    return;
  }

  // Note: Length is the size in bytes, not including terminating null (if any).
  // wstring constructor expects a length in wchar_t sized characters.
  _localDomain = std::wstring{accountDomain->DomainName.Buffer,
                              (size_t)(accountDomain->DomainName.Length) >> 1};
  log::info(L"Session manager initialized for local domain '{}'.",
            _localDomain);
}

std::shared_ptr<Session> SessionManager::find(std::wstring_view username,
                                              std::wstring_view domain)
{
  // TODO: Use full user@domain format.
  (void)domain;
  auto it = _sessions.find(username);
  if (it == _sessions.end()) {
    return std::shared_ptr<Session>{};
  }
  return it->second;
}

std::shared_ptr<Session> SessionManager::store(Session &&session) {
  //std::wstring_view domain{session.domain()};
  std::wstring_view name{session.username()};
  if (!session) {
    log::info(L"Failed login attempt for {}.", name);
    return std::shared_ptr<Session>{};
  }
  log::debug(L"Session username: {}.", name);
  auto [it, inserted] = _sessions.try_emplace(
    name, std::make_shared<Session>(std::move(session))
  );
  if (!inserted) {
    log::warn(L"Session already exists for '{}'.", name);
  }
  return it->second;
}

////////////////////////////////////////////////////////////////////////////////
// Session                                                                    //
////////////////////////////////////////////////////////////////////////////////

Session::Session(const SessionManager &, std::wstring_view username,
                 std::wstring_view domain, std::wstring &&password,
                 unsigned ttlSeconds) noexcept
  : _username{username},
    _domain{domain},
    _ttlResetSeconds{ttlSeconds},
    _ttlExpiresSeconds{0}
{
  PVOID pProfileBuffer;
  DWORD profileLength;
  QUOTA_LIMITS quotaLimits;
  if (!LogonUserExW(_username.c_str(), _domain.c_str(), password.c_str(),
                    LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT,
                    &_token, &_pSid, &pProfileBuffer, &profileLength,
                    &quotaLimits))
  {
    log::debug("LogonUserExW failed: {}", lastErrorString());
  }

  // TODO: Set expiration time.
}

Session::Session(const SessionManager &manager, std::wstring_view username,
                 std::wstring_view domain, std::wstring &&password) noexcept
  : Session(manager, username, domain, std::move(password),
            manager.defaultTtlSeconds())
{
}
