#define WSUDO_NO_NT_API
#include "wsudo/wsudo.h"
#include "wsudo/session.h"
#include "wsudo/server.h"
#include <cstdlib>
#include <random>
#include <atomic>

#define NT_SUCCESS(status) ((long)(status) >= 0)

using namespace wsudo;

static void CALLBACK timerRoutine(LPVOID data_, BOOLEAN) {
  auto *data = static_cast<SessionTimerData *>(data_);
  log::trace(L"Timer triggered for {}\\{}", data->session->domain(),
             data->session->username());
  data->sessionManager->expire(data->session.get());
}

Session::Session(SessionManager &sessionManager, std::wstring domain,
                 std::wstring username, std::wstring password,
                 unsigned ttlSeconds)
  : _sessionManager{&sessionManager}
  , _domain{std::move(domain)}
  , _username{std::move(username)}
  , _key{sessionManager.makeKey()}
  , _timer{nullptr}
  , _timerData{nullptr}
  , _ttlExpiresSeconds{ttlSeconds}
{
  WSUDO_SCOPE_EXIT { password.assign(password.length(), L'\0'); };
  if (!LogonUserExW(_username.c_str(), _domain.c_str(), password.c_str(),
                    LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                    _token.put(), _pSid.put(), nullptr, nullptr, nullptr))
  {
    log::debug("LogonUserExW failed: {}", lastErrorString());
    return;
  }
}

Session::~Session() {
  expire();
  if (_timerData) {
    delete _timerData;
  }
}

void Session::expire() {
  if (_timer) {
    auto timer = _timer;
    _timer = nullptr;
    std::atomic_thread_fence(std::memory_order_seq_cst); // Is this necessary?
    DeleteTimerQueueTimer(_sessionManager->timerQueue(), timer, nullptr);
  }
  log::trace(L"Session for {}\\{} expired.", _domain, _username);
}

void Session::refresh(std::shared_ptr<Session> session) {
  if (!_timerData) {
    _timerData = new SessionTimerData{session, _sessionManager};
  }
  if (_timer) {
    ChangeTimerQueueTimer(_sessionManager->timerQueue(), _timer,
                          1000 * _ttlExpiresSeconds, 0);
  } else {
    CreateTimerQueueTimer(
      &_timer, _sessionManager->timerQueue(), timerRoutine, _timerData,
      1000 * _ttlExpiresSeconds, 0,
      WT_EXECUTEINTIMERTHREAD | WT_EXECUTEONLYONCE
    );
  }
  log::trace(L"Session for {}\\{} refreshed. Will trigger in {} secs.",
             _domain, _username, _ttlExpiresSeconds);
}

static void CALLBACK timerRoutineTimeout(LPVOID data_, BOOLEAN) {
  auto *sessionManager = static_cast<SessionManager *>(data_);
  if (sessionManager->hasSession()) {
    return;
  }
  log::debug("No-session timeout elapsed, quitting.");
  sessionManager->quit();
}

SessionManager::SessionManager(Server *server, unsigned defaultTtlSeconds)
                               noexcept
  : _server{server}
  , _defaultTtlSeconds{
    defaultTtlSeconds == 0
      ? DefaultTtlSeconds
      : defaultTtlSeconds
  }
  , _timerQueue{CreateTimerQueue()}
  , _timeoutTimer{nullptr}
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

  CreateTimerQueueTimer(
    &_timeoutTimer, _timerQueue, timerRoutineTimeout, this,
    1000 * DefaultTimeoutSeconds, 0,
    WT_EXECUTEINTIMERTHREAD | WT_EXECUTEONLYONCE
  );

  log::info(L"Session manager initialized for local domain '{}'.",
            _localDomain);
}

SessionManager::~SessionManager() {
  DeleteTimerQueueEx(_timerQueue, nullptr);
}

void SessionManager::quit(int exitCode) {
  _server->quit(exitCode);
}

std::shared_ptr<Session>
SessionManager::find(std::string_view key) {
  auto it = _sessions.find(std::string{key});
  if (it == _sessions.end()) {
    return {};
  }
  it->second->refresh(it->second);
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
  session->refresh(session);

  _sessions.emplace(std::string{session->key()}, session);
  return session;
}

void SessionManager::expire(Session *session) {
  session->expire();
  auto it = _sessions.find(std::string{session->key()});
  if (it == _sessions.end()) {
    return;
  }
  _sessions.erase(it);

  if (_sessions.empty()) {
    _server->quit(0);
  }
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
