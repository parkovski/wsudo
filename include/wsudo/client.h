#ifndef WSUDO_CLIENT_H
#define WSUDO_CLIENT_H

#include "wsudo.h"
#include "message.h"

#include <wil/resource.h>

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
  wil::unique_hfile _pipe;
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

class Client {
public:
  class Connection {
    constexpr static int MaxConnectAttempts = 3;
    wil::unique_hfile _pipe;
    std::string _buffer;

  public:
    // Throws HRESULT on connection failure.
    explicit Connection(const std::wstring &pipeName,
                        int maxAttempts = MaxConnectAttempts);

    const std::string &buffer() const noexcept {
      return _buffer;
    }

    std::string &emptyBuffer() noexcept {
      _buffer.clear();
      return _buffer;
    }

    void sendBuffer();
    void recvBuffer();

    void send(const Message &message) {
      message.serialize(emptyBuffer());
      sendBuffer();
    }

    Message recv() {
      recvBuffer();
      return Message{_buffer};
    }
  };

private:
  Connection _conn;
  std::wstring _program;
  int _argc1;
  const wchar_t *const *_argv1;
  std::string _domain;
  std::string _username;

  // Fills _username and, if applicable, _domain.
  void lookupUsername();

  // Disable echo and read a password from the console. Returns false if
  // reading was interrupted (e.g. by Ctrl-C).
  bool readConsolePassword(std::string &password) const;

  bool resolveProgramPath();

  static void escapeCommandLineArg(std::wstring &arg);

  std::wstring createCommandLine() const;

  PROCESS_INFORMATION createSuspendedProcess() const;

  bool userHasActiveSession();

  bool validateCredentials(std::string &password);

  bool bless(HANDLE process);

  int resume(PROCESS_INFORMATION &pi, bool wait);

public:
  explicit Client(std::wstring &pipeName, int argc,
                  const wchar_t *const *argv);

  HRESULT operator()();
};

} // namespace wsudo

#endif // WSUDO_CLIENT_H
