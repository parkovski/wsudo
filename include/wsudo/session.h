#ifndef WSUDO_SESSION_H
#define WSUDO_SESSION_H

#ifdef WSUDO_NO_NT_API
# include <NTSecAPI.h>
// wil thinks this needs to be defined for the LSA types, but MSDN says to
// use NTSecAPI instead, which doesn't include this header.
# define _NTLSA_
#endif

#include <wil/resource.h>

#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace wsudo {

class Session;
class SessionManager;
class Server;

struct SessionTimerData {
  std::shared_ptr<Session> session;
  SessionManager *sessionManager;
};

class Session {
public:
  explicit Session(SessionManager &manager, std::wstring domain,
                   std::wstring username, std::wstring password,
                   unsigned ttlSeconds);

  ~Session();

  std::wstring_view domain() const noexcept {
    return _domain;
  }

  std::wstring_view username() const noexcept {
    return _username;
  }

  std::string_view key() const noexcept {
    return _key;
  }

  HANDLE token() const noexcept {
    return _token.get();
  }

  PSID psid() const noexcept {
    return _pSid.get();
  }

  explicit operator bool() const noexcept {
    return !!_token;
  }

  void expire();

  void refresh(std::shared_ptr<Session> session);

private:
  SessionManager *_sessionManager;
  const std::wstring _domain;
  const std::wstring _username;
  const std::string _key;
  wil::unique_handle _token;
  wil::unique_sid _pSid;
  HANDLE _timer;
  SessionTimerData *_timerData;
  // The time when this session expires if left untouched.
  unsigned _ttlExpiresSeconds;
};


class SessionManager {
  constexpr static unsigned DefaultTtlSeconds = 5 * 60;
  Server *_server;
  unsigned _defaultTtlSeconds;
  HANDLE _timerQueue;
  std::wstring _localDomain;
  std::unordered_map<std::string, std::shared_ptr<Session>> _sessions;
  static constexpr unsigned KeyLength = 64;

public:
  explicit SessionManager(Server *server, unsigned defaultTtlSeconds = 0)
                          noexcept;

  SessionManager(const SessionManager &) = delete;
  SessionManager &operator=(const SessionManager &) = delete;

  SessionManager(SessionManager &&) = delete;
  SessionManager &operator=(SessionManager &&) = delete;

  ~SessionManager();

  std::shared_ptr<Session> find(std::string_view key);

  std::shared_ptr<Session>
  create(std::wstring domain, std::wstring username,
         std::wstring password, unsigned ttlSeconds = 0);

  void expire(Session *session);

  std::string makeKey() const;

  unsigned defaultTtlSeconds() const {
    return _defaultTtlSeconds;
  }

  HANDLE timerQueue() const { return _timerQueue; }

  explicit operator bool() const noexcept { return !_localDomain.empty(); }
};

} // namespace wsudo

#endif // WSUDO_SESSION_H_
