#include "wsudo/message.h"

using namespace wsudo::msg;

bool Invalid::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() == 0) {
    m = Invalid{};
    return true;
  }
  return false;
}

void Invalid::serialize(std::string &buffer) const {}

bool Success::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() == 0) {
    m = Success{};
    return true;
  }
  return false;
}

void Success::serialize(std::string &buffer) const {}

bool Failure::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length()) {
    m = Failure{buffer};
  } else {
    m = Failure{};
  }
  return true;
}

void Failure::serialize(std::string &buffer) const {
  buffer.append(reason);
}

bool InternalError::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() == 0) {
    m = InternalError{};
    return true;
  }
  return false;
}

void InternalError::serialize(std::string &buffer) const {}

bool AccessDenied::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() == 0) {
    m = AccessDenied{};
    return true;
  }
  return false;
}

void AccessDenied::serialize(std::string &buffer) const {}

bool QuerySession::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() <= 1) {
    return false;
  }
  // Allow an empty domain but require a non-empty username.
  for (size_t isep = 0; isep < buffer.length() - 1; ++isep) {
    if (buffer[isep] == '\\') {
      m = QuerySession{buffer.substr(0, isep), buffer.substr(isep + 1)};
      return true;
    }
  }
  return false;
}

void QuerySession::serialize(std::string &buffer) const {
  buffer.append(domain).append(1, '\\').append(username);
}

bool Credential::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() <= 2) {
    return false;
  }
  // Allow domain and password to be empty.
  for (size_t isep = 0; isep < buffer.length() - 2; ++isep) {
    if (buffer[isep] == '\\') {
      if (buffer[isep + 1] == 0) {
        // Username can't be empty.
        break;
      }
      for (size_t jsep = isep + 2; jsep < buffer.length(); ++jsep) {
        if (buffer[jsep] == 0) {
          m = Credential{buffer.substr(0, isep),
                         buffer.substr(isep + 1, jsep - isep - 1),
                         buffer.substr(jsep + 1)};
          return true;
        }
      }
      break;
    }
  }
  return false;
}

void Credential::serialize(std::string &buffer) const {
  buffer.append(domain).append(1, '\\').append(username).append(1, '\0')
    .append(password);
}

bool Bless::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() != sizeof(void *)) {
    return false;
  }
  m = Bless{*reinterpret_cast<void *const *>(buffer.data())};
  return true;
}

void Bless::serialize(std::string &buffer) const {
  char *handle = static_cast<char *>(hRemoteProcess);
  buffer.append(handle, handle + sizeof(void *));
}

namespace {
  template<class M, class V, size_t Sz = std::variant_size_v<V>, size_t I = 0>
  struct Parser;

  template<class M, class V, size_t Sz>
  struct Parser<M, V, Sz, Sz> {
    bool operator()(M &, std::string_view) const noexcept {
      return false;
    }
  };

  template<class M, class V, size_t Sz, size_t I>
  struct Parser {
    bool operator()(M &m, std::string_view buffer) const noexcept {
      using type = std::variant_alternative_t<I, V>;
      if (buffer.substr(0, 4) != type::code) {
        return Parser<M, V, Sz, I + 1>{}(m, buffer);
      }

      return type::parse(m, buffer.substr(4));
    }
  };
}

Message::Message(std::string_view buffer) noexcept
  : variant{Invalid{}}
{
  if (buffer.length() >= 4) {
    // If this fails, we're already initialized to the Invalid state.
    Parser<Message, variant>{}(*this, buffer);
  }
}

void Message::serialize(std::string &buffer) const {
  std::visit([&buffer] (auto &&m) {
    buffer.append(std::remove_cvref_t<decltype(m)>::code);
    m.serialize(buffer);
  }, *this);
}
