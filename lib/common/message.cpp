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

// InitSession and QuerySession have the same params and parsing rules.
template<class M>
static bool parseSessionMessage(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() < 3) {
    return false;
  }

  std::string_view domain{"."};
  std::string_view username{};
  std::string_view key{};
  // Allow an empty domain but require a non-empty username and key.
  for (size_t i = 0; i < buffer.length(); ++i) {
    if (buffer[i] == '\\') {
      domain = buffer.substr(0, i);
      if (domain.empty()) {
        domain = ".";
      }
      for (size_t j = i + 1; j < buffer.length(); ++j) {
        if (buffer[j] == '\0') {
          username = buffer.substr(i + 1, j - i - 1);
          key = buffer.substr(j + 1);
          goto done_parsing;
        }
      }
    } else if (buffer[i] == '\0') {
      username = buffer.substr(0, i);
      key = buffer.substr(i + 1);
      goto done_parsing;
    }
  }

done_parsing:
  if (username.empty() || key.empty()) {
    return false;
  }

  m = M{domain, username, key};
  return true;
}

template<class M>
static void serializeSessionMessage(const M &m, std::string &buffer) {
  if (m.domain.empty() || m.domain == ".") {
    buffer
      .append(m.username)
      .append(1, '\0')
      .append(m.key);
  } else {
    buffer
      .append(m.domain)
      .append(1, '\\')
      .append(m.username)
      .append(1, '\0')
      .append(m.key);
  }
}

bool InitSession::parse(Message &m, std::string_view buffer) noexcept {
  return parseSessionMessage<InitSession>(m, buffer);
}

void InitSession::serialize(std::string &buffer) const {
  serializeSessionMessage(*this, buffer);
}

bool QuerySession::parse(Message &m, std::string_view buffer) noexcept {
  return parseSessionMessage<QuerySession>(m, buffer);
}

void QuerySession::serialize(std::string &buffer) const {
  serializeSessionMessage(*this, buffer);
}

bool Credential::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() < 2) {
    return false;
  }

  std::string_view domain{"."};
  std::string_view username{};
  std::string_view password{};
  // Allow an empty domain and/or password but require a non-empty username.
  for (size_t i = 0; i < buffer.length(); ++i) {
    if (buffer[i] == '\\') {
      domain = buffer.substr(0, i);
      if (domain.empty()) {
        domain = ".";
      }
      for (size_t j = i + 1; j < buffer.length(); ++j) {
        if (buffer[j] == '\0') {
          username = buffer.substr(i + 1, j - i - 1);
          password = buffer.substr(j + 1);
          goto done_parsing;
        }
      }
      if (username.empty()) {
        username = buffer.substr(i + 1);
        goto done_parsing;
      }
    } else if (buffer[i] == '\0') {
      username = buffer.substr(0, i);
      password = buffer.substr(i + 1);
      goto done_parsing;
    }
  }
  if (username.empty()) {
    username = buffer;
  }

done_parsing:
  if (username.empty()) {
    return false;
  }

  m = Credential{domain, username, password};
  return true;
}

void Credential::serialize(std::string &buffer) const {
  if (!domain.empty() && domain != ".") {
    buffer.append(domain).append(1, '\\');
  }
  buffer.append(username);
  if (!password.empty()) {
    buffer.append(1, '\0').append(password);
  }
}

bool Bless::parse(Message &m, std::string_view buffer) noexcept {
  if (buffer.length() != sizeof(void *)) {
    return false;
  }
  m = Bless{*reinterpret_cast<void *const *>(buffer.data())};
  return true;
}

void Bless::serialize(std::string &buffer) const {
  // string seems to not like using append here. Probably should switch to
  // vector<char> for the buffer.
  buffer.resize(4 + sizeof(void *));
  *reinterpret_cast<void **>(buffer.data() + 4) = hRemoteProcess;
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
