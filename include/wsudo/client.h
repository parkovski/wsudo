#ifndef WSUDO_CLIENT_H
#define WSUDO_CLIENT_H

#include "wsudo.h"
#include "message.h"

#include <wil/resource.h>

#include <vector>

struct ClientTest;

namespace wsudo {

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

  friend struct ::ClientTest;

  // Fills _username and, if applicable, _domain.
  void lookupUsername();

  // Disable echo and read a password from the console. Returns false if
  // reading was interrupted (e.g. by Ctrl-C).
  bool readConsolePassword(std::string &password) const;

  bool resolveProgramPath();

  static std::wstring escapeCommandLineArg(std::wstring_view arg);

  std::wstring createCommandLine() const;

  PROCESS_INFORMATION createSuspendedProcess() const;

  bool userHasActiveSession();

  bool validateCredentials(std::string &password);

  bool bless(HANDLE process);

  int resume(PROCESS_INFORMATION &pi, bool wait);

public:
  explicit Client(const std::wstring &pipeName, int argc,
                  const wchar_t *const *argv);

  HRESULT operator()();
};

} // namespace wsudo

#endif // WSUDO_CLIENT_H
