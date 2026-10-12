#define WSUDO_NO_NT_API
#include "wsudo/wsudo.h"
#include "wsudo/session.h"
#include <cstdlib>
#include <random>

#define NT_SUCCESS(status) ((long)(status) >= 0)

using namespace wsudo;

Session::Session(SessionManager &sessionManager, std::wstring domain,
                 std::wstring username, std::wstring password,
                 unsigned ttlSeconds)
  : _sessionManager{&sessionManager}
  , _domain{std::move(domain)}
  , _username{std::move(username)}
  , _key{sessionManager.makeKey()}
  , _ttlResetSeconds{ttlSeconds}
  , _ttlExpiresSeconds{ttlSeconds}
{
  if (!LogonUserExW(_username.c_str(), _domain.c_str(), password.c_str(),
                    LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                    _token.put(), _pSid.put(), nullptr, nullptr, nullptr))
  {
    log::debug("LogonUserExW failed: {}", lastErrorString());
  }

  password.assign(password.length(), L'\0');
}

SessionManager::SessionManager(unsigned defaultTtlSeconds) noexcept
  : _defaultTtlSeconds{
    defaultTtlSeconds == 0
      ? DefaultTtlSeconds
      : defaultTtlSeconds
  }
  , _timer{CreateWaitableTimerW(nullptr, false, nullptr)}
  , _localDomain{}
{
  NTSTATUS status;
  LSA_OBJECT_ATTRIBUTES attr{};
  wil::unique_hlsa policy;

  status = LsaOpenPolicy(nullptr, &attr, POLICY_VIEW_LOCAL_INFORMATION,
                         policy.put());
  if (!NT_SUCCESS(status)) {
    log::error("LsaOpenPolicy failed: {}",
               lastErrorString(LsaNtStatusToWinError(status)));
    return;
  }

  PPOLICY_ACCOUNT_DOMAIN_INFO domainRaw;
  status = LsaQueryInformationPolicy(
    policy.get(), PolicyAccountDomainInformation,
    reinterpret_cast<void **>(&domainRaw)
  );
  if (!NT_SUCCESS(status)) {
    log::error("LsaQueryInformationPolicy failed: {}",
                lastErrorString(LsaNtStatusToWinError(status)));
    return;
  }
  wil::unique_lsamem_ptr<POLICY_ACCOUNT_DOMAIN_INFO> domain{domainRaw};

  // Note: Length is the size in bytes, not including terminating null (if any).
  // wstring constructor expects a length in wchar_t sized characters.
  _localDomain = std::wstring{domain->DomainName.Buffer,
                              (size_t)(domain->DomainName.Length) >> 1};
  log::info(L"Session manager initialized for local domain '{}'.",
            _localDomain);
}

std::shared_ptr<Session>
SessionManager::find(std::string_view key) {
  auto it = _sessions.find(std::string{key});
  if (it == _sessions.end()) {
    return {};
  }
  return it->second;
}

std::shared_ptr<Session>
SessionManager::create(std::wstring domain, std::wstring username,
                       std::wstring password, unsigned ttlSeconds) {
  if (ttlSeconds == 0) {
    ttlSeconds = _defaultTtlSeconds;
  }

  auto session = std::make_shared<Session>(
    *this, std::move(domain), std::move(username), std::move(password),
    ttlSeconds
  );
  if (!*session) {
    return {};
  }

  _sessions.emplace(std::string{session->key()}, session);
  return session;
}

std::string SessionManager::makeKey() const {
  std::string key;
  key.reserve(KeyLength);
  static std::random_device random;
  static std::uniform_int_distribution<unsigned> distribution(0, 63);
  static const char *const base64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  for (unsigned i = 0; i < KeyLength; ++i) {
    key.push_back(base64[distribution(random)]);
  }
  return key;
}
