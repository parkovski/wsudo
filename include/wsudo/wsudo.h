#ifndef WSUDO_WSUDO_H
#define WSUDO_WSUDO_H

#define WINVER _WIN32_WINNT_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define NTDDI_VERSION NTDDI_WIN7
#define NOMINMAX
#include <Windows.h>
#include "int/winsupport.h"

// Winternl.h and NTSecAPI.h both define some of the same types so
// we can't include both in the same file. Thanks Microsoft.
#ifndef WSUDO_NO_NT_API
#  include <winternl.h>
#  include "int/ntapi.h"
#endif

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <type_traits>

#include "log.h"

namespace wsudo {

/// File path to the client-server communication pipe.
extern const wchar_t *const PipeFullPath;

/// Pipe's buffer size in bytes.
constexpr size_t PipeBufferSize = 128;

// Maximum concurrent server connections. Being sudo, it's unlikely to have to
// process many things concurrently, but we have to give Windows a number.
constexpr int MaxPipeConnections = 3;

// Pipe timeout, again for Windows.
constexpr int PipeDefaultTimeout = 0;

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
