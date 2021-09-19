#pragma once

#include <variant>
#include <string>
#include <string_view>

namespace wsudo::msg {
  struct Message;

  // Invalid message.
  struct Invalid {
    static constexpr std::string_view code{"INVM"};

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Non-specific success.
  struct Success {
    static constexpr std::string_view code{"SUCC"};

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Non-specific failure. Reason can be empty.
  struct Failure {
    static constexpr std::string_view code{"FAIL"};

    std::string_view reason;

    Failure() = default;

    constexpr explicit Failure(std::string_view reason) noexcept
      : reason{reason}
    {}

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Internal error while processing request.
  struct InternalError {
    static constexpr std::string_view code{"INTE"};

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Access denied.
  struct AccessDenied {
    static constexpr std::string_view code{"DENY"};

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Ask if a session is already open.
  struct QuerySession {
    static constexpr std::string_view code{"QSES"};

    std::string_view domain;
    std::string_view username;

    constexpr explicit QuerySession(std::string_view domain,
                                    std::string_view username) noexcept
      : domain{domain}, username{username}
    {}

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Send user credentials.
  struct Credential {
    static constexpr std::string_view code{"CRED"};

    std::string_view domain;
    std::string_view username;
    std::string_view password;

    constexpr explicit Credential(std::string_view domain,
                                  std::string_view username,
                                  std::string_view password) noexcept
      : domain{domain}, username{username}, password{password}
    {}

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // Bless (elevate process).
  struct Bless {
    static constexpr std::string_view code{"BLES"};

    void *hRemoteProcess;

    constexpr explicit Bless(void *hRemoteProcess) noexcept
      : hRemoteProcess{hRemoteProcess}
    {}

    static bool parse(Message &m, std::string_view buffer) noexcept;
    void serialize(std::string &buffer) const;
  };

  // This is a view; it does not hold its own buffer memory.
  struct Message : std::variant<
    Invalid,
    Success,
    Failure,
    InternalError,
    AccessDenied,
    QuerySession,
    Credential,
    Bless
  > {
    using variant::variant;

    // If the buffer cannot be decoded, an Invalid message is created.
    explicit Message(std::string_view buffer) noexcept;

    void serialize(std::string &buffer) const;
  };
} // namespace wsudo::msg

namespace wsudo {
  using msg::Message;
}
