#ifndef WSUDO_WSUDO_H
#define WSUDO_WSUDO_H

// #pragma warning(push)
// #pragma warning(disable:4996) // codecvt deprecated
// #define SPDLOG_WCHAR_FILENAMES
// #define SPDLOG_WCHAR_TO_UTF8_SUPPORT
// #define SPDLOG_NO_NAME
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
// #pragma warning(pop)

#include <cstdint>

namespace wsudo {

/// File path to the client-server communication pipe.
extern const wchar_t *const PipeFullPath;

/// Pipe's buffer size in bytes.
constexpr size_t PipeBufferSize = 1024;

/// Message headers
namespace msg {
  /// Client->Server message headers
  namespace client {
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

extern std::shared_ptr<spdlog::logger> g_outLogger;
extern std::shared_ptr<spdlog::logger> g_errLogger;

template<typename... Args>
static inline void trace(const char *fmt, Args &&...args)
{ g_outLogger->trace(fmt, std::forward<Args>(args)...); }

template<typename... Args>
static inline void debug(const char *fmt, Args &&...args)
{ g_outLogger->debug(fmt, std::forward<Args>(args)...); }

template<typename... Args>
static inline void info(const char *fmt, Args &&...args)
{ g_outLogger->info(fmt, std::forward<Args>(args)...); }

template<typename... Args>
static inline void warn(const char *fmt, Args &&...args)
{ g_errLogger->warn(fmt, std::forward<Args>(args)...); }

template<typename... Args>
static inline void error(const char *fmt, Args &&...args)
{ g_errLogger->error(fmt, std::forward<Args>(args)...); }

template<typename... Args>
static inline void critical(const char *fmt, Args &&...args)
{ g_errLogger->critical(fmt, std::forward<Args>(args)...); }

} // namespace log

// Wrapper for a function that returns a pointer of its own type, enabling
// endless recursive callbacks.
template<typename... Args>
struct recursive_callback {
  using pointer = recursive_callback (*)(Args...);

  // Default (null pointer) constructor.
  constexpr recursive_callback() noexcept
    : function{nullptr}
  {}

  // Implicit conversion from pointer type.
  constexpr recursive_callback(pointer function) noexcept
    : function{function}
  {}

  // Copy
  recursive_callback(const recursive_callback &) = default;

  // Copy assign
  recursive_callback & operator=(const recursive_callback &) = default;

  // Call the function, returning the next callback.
  constexpr inline recursive_callback operator()(Args &&...args) const {
    return function(std::forward<Args>(args)...);
  }

  // Call the function, replacing it with the returned callback.
  constexpr inline bool call_and_swap(Args &&...args) {
    function = function(std::forward<Args>(args)...);
    return !!function;
  }

  // Null check.
  constexpr explicit operator bool() const { return !!function; }

private:
  pointer function;
};

// Wrapper for recursive member function callbacks.
template<typename T, typename... Args>
struct recursive_mem_callback {
  using pointer = recursive_mem_callback (T::*)(Args...);

  // Default (null pointer) constructor.
  constexpr recursive_mem_callback() noexcept
    : function{nullptr}
  {}

  // Implicit conversion from member pointer type.
  constexpr recursive_mem_callback(pointer function) noexcept
    : function{function}
  {}

  // Copy.
  recursive_mem_callback(const recursive_mem_callback &) = default;

  // Copy assign.
  recursive_mem_callback & operator=(const recursive_mem_callback &) = default;

  // Call the function, returning the next callback.
  constexpr inline recursive_mem_callback operator()(T &self, Args &&...args) {
    return (self.*function)(std::forward<Args>(args)...);
  }

  // Call the function, replacing it with the returned callback.
  constexpr inline bool call_and_swap(T &self, Args &&...args) {
    function = self.*function(std::forward<Args>(args)...);
    return !!function;
  }

  // Null check.
  constexpr explicit operator bool() const { return !!function; }

private:
  pointer function;
};

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
#define WSUDO_CONCAT3(a,b,c) WSUDO_CONCAT2(WSUDO_CONCAT2(a,b),c)
#define WSUDO_CONCAT4(a,b,c,d) WSUDO_CONCAT2(WSUDO_CONCAT2(a,b),WSUDO_CONCAT2(c,d))

// Scope destructor.
// Usage: WSUDO_SCOPEEXIT { capture-by-ref lambda body };
#define WSUDO_SCOPEEXIT \
  [[maybe_unused]] auto const &WSUDO_CONCAT4(_scopeExit_, __func__, _, __LINE__) = \
    ::wsudo::detail::ScopeExitHelper{} % [&]()

// Scope destructor that captures the this pointer by value.
#define WSUDO_SCOPEEXIT_THIS \
  [[maybe_unused]] auto const &WSUDO_CONCAT4(_scopeExit_, __func__, _, __LINE__) = \
    ::wsudo::detail::ScopeExitHelper{} % [&, this]()

#ifndef NDEBUG
# include <cassert>
# define WSUDO_UNREACHABLE(why) do { assert(0 && (why)); __assume(0); } while(0)
#else
# define WSUDO_UNREACHABLE(why) __assume(0)
#endif

#endif // WSUDO_WSUDO_H

