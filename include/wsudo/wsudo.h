#ifndef WSUDO_WSUDO_H
#define WSUDO_WSUDO_H

#define WINVER _WIN32_WINNT_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define NTDDI_VERSION NTDDI_WIN7
#define NOMINMAX
#include <Windows.h>
#include "winsupport.h"

// Winternl.h and NTSecAPI.h both define some of the same types so
// we can't include both in the same file. Thanks Microsoft.
#ifndef WSUDO_NO_NT_API
#  include <winternl.h>
#  include "ntapi.h"
#endif

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <type_traits>

namespace wsudo {

/// File path to the client-server communication pipe.
extern const wchar_t *const PipeFullPath;

/// Pipe's buffer size in bytes.
constexpr size_t PipeBufferSize = 1024;

// Maximum concurrent server connections. Being sudo, it's unlikely to have to
// process many things concurrently, but we have to give Windows a number.
constexpr int MaxPipeConnections = 3;

// Pipe timeout, again for Windows.
constexpr int PipeDefaultTimeout = 0;


/// Message headers
namespace msg {
  /// Client->Server message headers
  namespace client {
    /// Query session - server should tell how, if possible, to create the
    /// session.
    extern const char *const QuerySession;
    /// User credentials message
    extern const char *const Credential;
    /// Bless (elevate process) request message
    extern const char *const Bless;
  }

  /// Server->Client message headers
  namespace server {
    /// Success
    extern const char *const Success;
    /// Invalid message
    extern const char *const InvalidMessage;
    /// Internal error (server bug)
    extern const char *const InternalError;
    /// Access denied
    extern const char *const AccessDenied;
  }
} // namespace msg

namespace log {

// Logger that prints to stdout.
extern std::shared_ptr<spdlog::logger> g_outLogger;

// Logger that prints to stderr.
extern std::shared_ptr<spdlog::logger> g_errLogger;

/// Print to stdout with no prefix.
template<typename... Args>
static inline void print(const char *fmt, Args &&...args)
{ fmt::print(stdout, fmt, std::forward<Args>(args)...); }

/// Print to stderr with no prefix.
template<typename... Args>
static inline void eprint(const char *fmt, Args &&...args)
{ fmt::print(stderr, fmt, std::forward<Args>(args)...); }

/// Trace logger.
template<typename... Args>
static inline void trace(const char *fmt, Args &&...args)
{ g_outLogger->trace(fmt, std::forward<Args>(args)...); }

/// Debug logger.
template<typename... Args>
static inline void debug(const char *fmt, Args &&...args)
{ g_outLogger->debug(fmt, std::forward<Args>(args)...); }

/// Info logger.
template<typename... Args>
static inline void info(const char *fmt, Args &&...args)
{ g_outLogger->info(fmt, std::forward<Args>(args)...); }

/// Warning logger.
template<typename... Args>
static inline void warn(const char *fmt, Args &&...args)
{ g_errLogger->warn(fmt, std::forward<Args>(args)...); }

/// Error logger.
template<typename... Args>
static inline void error(const char *fmt, Args &&...args)
{ g_errLogger->error(fmt, std::forward<Args>(args)...); }

/// Critical logger.
template<typename... Args>
static inline void critical(const char *fmt, Args &&...args)
{ g_errLogger->critical(fmt, std::forward<Args>(args)...); }

#ifndef SPDLOG_WCHAR_TO_UTF8_SUPPORT
#error "Requires spdlog wchar_t logging feature"
#endif

/// Print to stdout with no prefix (wchar_t version).
template<typename... Args>
static inline void print(const wchar_t *fmt, Args &&...args)
{ fmt::print(stdout, fmt, std::forward<Args>(args)...); }

/// Print to stderr with no prefix (wchar_t version).
template<typename... Args>
static inline void eprint(const wchar_t *fmt, Args &&...args)
{ fmt::print(stderr, fmt, std::forward<Args>(args)...); }

/// Trace logger (wchar_t version).
template<typename... Args>
static inline void trace(const wchar_t *fmt, Args &&...args)
{ g_outLogger->trace(fmt, std::forward<Args>(args)...); }

/// Debug logger (wchar_t version).
template<typename... Args>
static inline void debug(const wchar_t *fmt, Args &&...args)
{ g_outLogger->debug(fmt, std::forward<Args>(args)...); }

/// Info logger (wchar_t version).
template<typename... Args>
static inline void info(const wchar_t *fmt, Args &&...args)
{ g_outLogger->info(fmt, std::forward<Args>(args)...); }

/// Warning logger (wchar_t version).
template<typename... Args>
static inline void warn(const wchar_t *fmt, Args &&...args)
{ g_errLogger->warn(fmt, std::forward<Args>(args)...); }

/// Error logger (wchar_t version).
template<typename... Args>
static inline void error(const wchar_t *fmt, Args &&...args)
{ g_errLogger->error(fmt, std::forward<Args>(args)...); }

/// Critical logger (wchar_t version).
template<typename... Args>
static inline void critical(const wchar_t *fmt, Args &&...args)
{ g_errLogger->critical(fmt, std::forward<Args>(args)...); }

} // namespace log

namespace detail {
  /// RAII wrapper to run a function when the scope exits.
  template<typename OnExit,
           typename = std::enable_if_t<std::is_invocable_v<OnExit>>>
  class ScopeExit {
    OnExit _onExit;
  public:
    explicit ScopeExit(OnExit onExit) : _onExit(std::move(onExit)) {}
    ScopeExit &operator=(const ScopeExit &) = delete;
    ~ScopeExit() {
      _onExit();
    }
  };

  struct ScopeExitHelper{};

  // Use an arbitrary high precedence operator, even though there shouldn't
  // be anything else in these statements anyways.
  template<typename OnExit>
  ScopeExit<OnExit> operator%(const detail::ScopeExitHelper &, OnExit onExit) {
    return ScopeExit<OnExit>{std::move(onExit)};
  }
}

} // namespace wsudo

#define WSUDO_CONCAT_IMPL(a,b) a##b
#define WSUDO_CONCAT2(a,b) WSUDO_CONCAT_IMPL(a,b)

// Scope destructor.
// Usage: WSUDO_SCOPEEXIT { capture-by-ref lambda body };
#define WSUDO_SCOPEEXIT \
  [[maybe_unused]] auto const &WSUDO_CONCAT2(_scopeExit_, __LINE__) = \
    ::wsudo::detail::ScopeExitHelper{} % [&]()

// Scope destructor that captures the this pointer by value.
#define WSUDO_SCOPEEXIT_THIS \
  [[maybe_unused]] auto const &WSUDO_CONCAT2(_scopeExit_, __LINE__) = \
    ::wsudo::detail::ScopeExitHelper{} % [&, this]()

#ifndef NDEBUG
# define WSUDO_UNREACHABLE(why) do { assert(0 && (why)); __assume(0); } while(0)
#else
# define WSUDO_UNREACHABLE(why) __assume(0)
#endif

#endif // WSUDO_WSUDO_H
