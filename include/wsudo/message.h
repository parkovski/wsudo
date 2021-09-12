#pragma once

#include <variant>
#include <string_view>

namespace wsudo::msg {
  // Invalid message.
  struct Invalid {
    static constexpr const char *code = "INVM";
  };

  // Non-specific success.
  struct Success {
    static constexpr const char *code = "SUCC";
  };

  // Non-specific failure.
  struct Failure {
    static constexpr const char *code = "FAIL";
  };

  // Internal error while processing request.
  struct InternalError {
    static constexpr const char *code = "INTE";
  };

  // Access denied.
  struct AccessDenied {
    static constexpr const char *code = "DENY";
  };

  // Ask if a session is already open.
  struct QuerySession {
    static constexpr const char *code = "QSES";
  };

  // Send user credentials.
  struct Credential {
    static constexpr const char *code = "CRED";

    std::string_view domain;
    std::string_view username;
    std::string_view password;
  };

  // Bless (elevate process).
  struct Bless {
    static constexpr const char *code = "BLES";
  };

  using Message = std::variant<
    Invalid,
    Success,
    Failure,
    InternalError,
    AccessDenied,
    QuerySession,
    Credential,
    Bless
  >;
} // namespace wsudo::msg

namespace wsudo {
  using msg::Message;
}
