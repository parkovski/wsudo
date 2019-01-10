#ifndef WSUDO_SESSION_H
#define WSUDO_SESSION_H

#include "wsudo.h"

#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace wsudo::session {

class Session;

class SessionManager {
public:
  explicit SessionManager(unsigned defaultTtlSeconds) noexcept;
  SessionManager(const SessionManager &) = delete;
  SessionManager &operator=(const SessionManager &) = delete;
  SessionManager(SessionManager &&) = default;
  SessionManager &operator=(SessionManager &&) = default;

  std::shared_ptr<Session> find(std::wstring_view username,
                                std::wstring_view domain = {});

  template<typename... Args>
  std::shared_ptr<Session> create(Args &&...args) {
    return store(Session(*this, std::forward<Args>(args)...));
  }

  unsigned defaultTtlSeconds() const {
    return _defaultTtlSeconds;
  }

private:
  std::shared_ptr<Session> store(Session &&session);

  unsigned _defaultTtlSeconds;
  HObject _timer;
  std::wstring _localDomain;
  std::unordered_map<std::wstring_view, std::shared_ptr<Session>> _sessions;
};

class Session {
public:
  ~Session();

  std::wstring_view username() const {
    return _username;
  }

  std::wstring_view domain() const {
    return _domain;
  }

  HANDLE token() const {
    return _token;
  }

  PSID psid() const {
    return _pSid;
  }

  explicit operator bool() const {
    return !!_token;
  }

private:
  friend class SessionManager;

  // Password is a move reference in the hopes that we will get the only copy
  // and erase it after we're done with it.
  Session(const SessionManager &manager, std::wstring_view username,
          std::wstring_view domain, std::wstring &&password,
          unsigned ttlSeconds) noexcept;

  Session(const SessionManager &manager, std::wstring_view username,
          std::wstring_view domain, std::wstring &&password) noexcept;

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  Session(Session &&) = default;
  Session &operator=(Session &&) = default;

  const std::wstring _username;
  const std::wstring _domain;
  HObject _token;
  HLocalPtr<PSID> _pSid;
  // The amount of time this session will be kept open without being referenced.
  // Each time the session is used, its lifetime is reset to this value.
  unsigned _ttlResetSeconds;
  // The time when this session expires if left untouched.
  unsigned _ttlExpiresSeconds;
};

} // namespace wsudo::session

#endif // WSUDO_SESSION_H_
