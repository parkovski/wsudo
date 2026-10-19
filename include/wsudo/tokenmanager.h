#pragma once

#include <wil/resource.h>

namespace wsudo {

// This class manages the user's sensitive information and decides whether to
// allow the user to obtain the requested token for the new process. This
// decision is made based on the following criteria:
// - The calling (client) process.
// - The requested process to launch.
// - The requested token privileges.
// - The authentication account (domain, username, password) which may be
//   different than the caller and/or the requested launch account. E.g.
//   MyRestrictedAccount requests to use the credentials from MyAdminAccount
//   to launch a process as SYSTEM.
// - The owner of the calling process, including:
//   - Permissions and groups on the client token.
//   - The mandatory level of the calling token. For example, a user may be
//     allowed to elevate from medium to high which includes normal command
//     line usage, but not allowed to elevate from low which excludes
//     sandboxed processes.
//   - The session the client token originated from.
class TokenManager {
  class ScopedSecurityPrivilege {
    TokenManager *tm;
    bool system;
    bool ok;

    bool setPrivilege(const wchar_t *privilege, bool enabled) noexcept;
    bool setPrivilegeState(bool enabled) noexcept;

  public:
    explicit ScopedSecurityPrivilege(TokenManager &tm, bool system) noexcept
      : tm{&tm}
      , system{system}
    {
      setPrivilegeState(true);
    }

    ~ScopedSecurityPrivilege() {
      setPrivilegeState(false);
    }

    explicit operator bool() const noexcept { return ok; }
  };

  // Token for the current process, needed to adjust privileges.
  wil::unique_handle _serverToken;
  // Enable SE_SECURITY_NAME for the lifetime of this object. It must be
  // initialized after and destroyed before _serverToken, and likewise
  // initialized before _clientProcess.
  ScopedSecurityPrivilege _setSecurityPrivilege;
  // Calling process handle.
  wil::unique_handle _clientProcess;
  // Token generated to apply to the new process.
  wil::unique_handle _launchToken;

  // Returns INVALID_HANDLE_VALUE on failure.
  static HANDLE getClientProcess(DWORD pid) noexcept;

  HANDLE getServerToken() noexcept;
  HANDLE getClientToken() noexcept;

  bool createLaunchToken(HANDLE baseToken);

public:
  // Creates a TokenManager for the identity that owns the client process.
  TokenManager(DWORD clientProcessId);

  // Duplicates the server's token.
  bool createServerLaunchToken();

  bool createClientLaunchToken();

  // Set the remote process's token to the currently stored token.
  bool applyToken(HANDLE remoteProcess);
};

} // namespace wsudo
