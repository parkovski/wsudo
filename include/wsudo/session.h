#ifndef WSUDO_SESSION_H
#define WSUDO_SESSION_H

#include "wsudo.h"
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

class SessionManager {
  unsigned _defaultTtlSeconds;
  wil::unique_handle _timer;
  std::wstring _localDomain;
  std::unordered_map<std::wstring_view, std::shared_ptr<Session>> _sessions;

public:
  explicit SessionManager(unsigned defaultTtlSeconds = 0) noexcept;

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
};

class Session {
  friend class SessionManager;

  // Password is a move reference in the hopes that we will get the only copy
  // and erase it after we're done with it.
  Session(const SessionManager &manager, std::wstring_view username,
          std::wstring_view domain, std::wstring &&password,
          unsigned ttlSeconds) noexcept;

  Session(const SessionManager &manager, std::wstring_view username,
          std::wstring_view domain, std::wstring &&password) noexcept;

public:
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  Session(Session &&) = default;
  Session &operator=(Session &&) = default;

  std::wstring_view username() const {
    return _username;
  }

  std::wstring_view domain() const {
    return _domain;
  }

  HANDLE token() const {
    return _token.get();
  }

  PSID psid() const {
    return _pSid.get();
  }

  explicit operator bool() const {
    return !!_token;
  }

private:
  const std::wstring _username;
  const std::wstring _domain;
  wil::unique_handle _token;
  wil::unique_sid _pSid;
  // The amount of time this session will be kept open without being referenced.
  // Each time the session is used, its lifetime is reset to this value.
  unsigned _ttlResetSeconds;
  // The time when this session expires if left untouched.
  unsigned _ttlExpiresSeconds;
};

} // namespace wsudo

#endif // WSUDO_SESSION_H_
