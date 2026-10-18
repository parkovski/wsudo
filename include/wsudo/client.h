#ifndef WSUDO_CLIENT_H
#define WSUDO_CLIENT_H

#include "message.h"

#include <wil/resource.h>

struct ClientTest;

namespace wsudo {

class Client {
public:
  class Connection {
    constexpr static int MaxConnectAttempts = 3;
    std::wstring _pipeName;
    wil::unique_hfile _pipe;
    std::string _buffer;

  public:
    // Throws HRESULT on connection failure.
    explicit Connection(const std::wstring &pipeName,
                        int maxAttempts = MaxConnectAttempts);

    void connect(int maxAttempts = MaxConnectAttempts);
    void disconnect();

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

  struct Options {
    bool showHelp;
    bool startService;
    bool keepEnvironment;
    bool useShim;
    bool runAsSystem;
    bool detach;
    std::wstring commandLine;

    Options() noexcept;

    bool parse(int argc, const wchar_t *const *argv) noexcept;
  };

private:
  Connection _conn;
  std::string _domain;
  std::string _username;
  std::string _key;

  // Fills _username and, if applicable, _domain.
  void lookupUsername();

  std::wstring getUserDirectory() const;

  std::wstring getUserDirectoryFileName(std::wstring_view filename) const;

  std::string
  readPrivateFile(std::wstring_view name, bool create = false) const;

  void writePrivateFile(std::wstring_view name, std::string_view text) const;

  // Disable echo and read a password from the console. Returns false if
  // reading was interrupted (e.g. by Ctrl-C).
  bool readConsolePassword(std::string &password) const;

  PROCESS_INFORMATION createSuspendedProcess(std::wstring &commandLine);

  bool userHasActiveSession();

  bool validateCredentials(std::string &password);

  bool bless(HANDLE process);

  int resume(PROCESS_INFORMATION &pi, bool wait);

public:
  explicit Client(const std::wstring &pipeName);

  int operator()(std::wstring &commandLine, bool wait);
};

} // namespace wsudo

#endif // WSUDO_CLIENT_H
