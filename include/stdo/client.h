#ifndef STDO_CLIENT_H
#define STDO_CLIENT_H

#include "stdo/stdo.h"
#include "stdo/winsupport.h"

#include <vector>

namespace stdo {

constexpr int ClientExitAccessDenied = 225;
constexpr int ClientExitUserCanceled = 226;
constexpr int ClientExitCreateProcessError = 227;
constexpr int ClientExitInvalidUsage = 228;
constexpr int ClientExitSystemError = 229;
constexpr int ClientExitServerNotFound = 230;

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

}

#endif // STDO_CLIENT_H

