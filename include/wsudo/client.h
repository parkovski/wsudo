#ifndef WSUDO_CLIENT_H
#define WSUDO_CLIENT_H

#include "wsudo.h"

#include <vector>

namespace wsudo {

// Codes returned by client main indicating the reason for exiting.
enum ClientExitCode {
  ClientExitOk                  = 0,
  ClientExitAccessDenied        = 225,
  ClientExitUserCanceled        = 226,
  ClientExitCreateProcessError  = 227,
  ClientExitInvalidUsage        = 228,
  ClientExitSystemError         = 229,
  ClientExitServerNotFound      = 230,
};

// Returns a description of a ClientExitCode.
static inline const char *clientExitToString(ClientExitCode code) {
  switch (code) {
    default:
      return "unknown";
    case ClientExitOk:
      return "ok";
    case ClientExitAccessDenied:
      return "access denied";
    case ClientExitUserCanceled:
      return "user canceled";
    case ClientExitCreateProcessError:
      return "error creating process";
    case ClientExitInvalidUsage:
      return "invalid usage";
    case ClientExitSystemError:
      return "system error";
    case ClientExitServerNotFound:
      return "server not found";
  }
}

class ClientConnection {
  HObject _pipe;
  std::vector<char> _buffer;

  constexpr static int MaxConnectAttempts = 3;

  void connect(
    LPSECURITY_ATTRIBUTES secAttr,
    const wchar_t *pipeName,
    int attempts
  );

public:
  explicit ClientConnection(const wchar_t *pipeName);

  bool good() const { return !!_pipe; }
  explicit operator bool() const { return good(); }

  bool negotiate(const char *credentials, size_t length);
  bool bless(HANDLE process);

  bool readServerMessage();
};

} // namespace wsudo

#endif // WSUDO_CLIENT_H

